

#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <memory>

#include <minidpdk/util.hh>
#include <osv/mmu.hh>
#include <osv/types.h>

namespace minidpdk {
class slab_allocator;

using rte_mbuf_extbuf_free_callback_t = void (*)(void *addr, void *opaque);

struct rte_mbuf_ext_shared_info {
  void *fcb_opaque;
  rte_mbuf_extbuf_free_callback_t free_cb;
  uint16_t refcnt;
};

struct mbuf {
  // next in chain
  mbuf *next;

  // address of the mbuf structure
  char *buf_addr;


  // memory pool allocated from
  slab_allocator *pool;

  // IO Virtual Address
  uintptr_t iova;


  // offset of the dataroom
  uint16_t data_offset;

  // size of the packet(chained)
  // size of the used fraction of the dataroom
  // total length of the buffer
  uint16_t pkt_len, data_len, buf_len;

  // reference count
  uint16_t refcnt;

  // length of the l2 header
  // length of the l3 header
  // length of the l4 header
  // number of segments in the chain
  uint16_t l2_len : 5;
  uint16_t l3_len : 6;
  uint16_t l4_len : 5;
  uint16_t nb_segs;

  // rss hash
  struct {
    uint64_t rss;
  } hash;

  // NIC offload flags
  uint64_t ol_flags;

  rte_mbuf_ext_shared_info *shinfo;

  // type of the packet
  uint32_t packet_type;

  mbuf() = default;
  mbuf(mbuf *next, slab_allocator *sb, uintptr_t iova, uint32_t size,
       uint16_t nb_segs, uint16_t data_len, uint16_t headroom)
      : next(next), buf_addr(reinterpret_cast<char *>(this)),
        pool(sb), iova(iova + sizeof(mbuf) + headroom), data_offset(headroom),
        pkt_len(), data_len(data_len), buf_len(size), refcnt(1),
        nb_segs(nb_segs), shinfo(nullptr) {}

  uint8_t *buf_start() {
    return reinterpret_cast<uint8_t *>(buf_addr) + sizeof(mbuf);
  }

  template <typename T> T *data(size_t offset = 0) {
    return reinterpret_cast<T *>(buf_start() + data_offset + offset);
  }

  const void *read(uint32_t off, uint32_t len, void *buf) {
    if (off + len <= data_len)
      return data<uint8_t *>(off);

    auto *seg = this;
    while (seg && off >= seg->data_len) {
      off -= seg->data_len;
      seg = seg->next;
    }

    uint32_t copied = 0;
    while (seg && copied < len) {
      auto *src = seg->data<uint8_t>() + off;
      auto n = std::min<uint32_t>(seg->data_len - off, len - copied);
      std::memcpy(static_cast<uint8_t *>(buf) + copied, src, n);
      copied += n;
      off = 0;
      seg = seg->next;
    }

    return copied == len ? buf : nullptr;
  }

  mbuf *last_seg() {
    auto *seg = this;
    while (seg->next)
      seg = seg->next;
    return seg;
  }

  template <typename T> T *prepend() {
    data_offset -= sizeof(T);
    data_len += sizeof(T);
    iova -= sizeof(T);
    return data<T>();
  }

  void adj(uint16_t len) {
    data_offset += len;
    data_len -= len;
    iova += len;
  }
};

struct obj_header {
  obj_header *next;
  uintptr_t iova;
};

inline void mbuf_free(mbuf *buf);

struct alignas(64) slab {
  slab *next;
  slab *prev;
  obj_header *freelist;
  uint32_t inuse;
  uint32_t padding;
  uintptr_t iova;

  static void list_remove(slab *s) {
    s->prev->next = s->next;
    s->next->prev = s->prev;
  }

  slab() : next(nullptr), prev(nullptr), freelist(nullptr), inuse() {}
};

static_assert(sizeof(slab) % 64 == 0, "");

inline void mbuf_free(mbuf *buf);

using init_fn_t = void (*)(mbuf **, uint16_t, void *);
struct slab_cache {
  static constexpr size_t kDefaultCacheSize = 128;
  struct slab_list {
    slab head, tail;
    slab_list() : head(), tail() {
      head.next = &tail;
      tail.prev = &head;
    }

    void list_push(slab *s) {
      s->next = head.next;
      s->prev = &head;
      head.next->prev = s;
      head.next = s;
    }

    bool empty() const { return head.next == &tail; }

    slab *front() { return head.next; }
  };

  slab_list partial;
  slab_list full;
  std::array<obj_header *, kDefaultCacheSize> mag;
  unsigned top = 0;
  size_t obj_size;

  slab_cache(size_t obj_size) : partial(), full(), obj_size(obj_size) {}
};

inline void mbuf_free(mbuf *buf);

using mbuf_ptr = std::unique_ptr<mbuf, decltype(&mbuf_free)>;
class slab_allocator {
public:
  static constexpr size_t kDefaultHeadroom = 128;
  static constexpr size_t kMaxDataLen = 1500 + 20;
  static constexpr size_t kDefaultSize =
      kMaxDataLen + kDefaultHeadroom + sizeof(mbuf);
  static_assert(kDefaultSize % 64 == 0, "");
  static constexpr size_t kSlabSize = 2 * 1024 * 1024;

public:
  slab_allocator(unsigned size, void* priv = nullptr, init_fn_t init_fn = nullptr) : cache(kDefaultSize), priv(priv), init_fn(init_fn) { alloc_new_slab(cache); }

  mbuf *alloc_default(uint16_t data_len) {
    assert(data_len <= kMaxDataLen);
    obj_header *obj = nullptr;
    if (cache.top) {
      obj = cache.mag[--cache.top];
    } else {
      if (cache.partial.empty())
        alloc_new_slab(cache);
      auto *s = cache.partial.front();
      obj = s->freelist;
      s->freelist = obj->next;
      ++s->inuse;
      if (!s->freelist) {
        slab::list_remove(s);
        cache.full.list_push(s);
      }
    }
    assert(obj);
    return new (obj) mbuf{nullptr, this,     obj->iova,       kDefaultSize,
                          1,       data_len, kDefaultHeadroom};
  }

  int alloc_bulk(void** pkts, unsigned n){
      auto from_cache = std::min<unsigned>(cache.top, n);
      if(from_cache){
          cache.top -= from_cache;
          std::memcpy(pkts, cache.mag.data() + cache.top, from_cache * sizeof(void*));
          for(auto i = 0u; i < from_cache; ++i){
              auto *obj = reinterpret_cast<obj_header*>(pkts[i]);
              rte_prefetch0_write(pkts + 3);
              new (obj) mbuf{nullptr, this,     obj->iova,       kDefaultSize,
                          1,       0, kDefaultHeadroom};
          }
      }
      for(auto i = from_cache; i < n; ++i)
          pkts[i] = alloc_default(0);
      return 0;
  }

  mbuf* alloc_single(){
      return alloc_default(0);
  }

  void alloc_new_slab(slab_cache &c) {
    auto *region = memory::alloc_huge_page(mmu::huge_page_size);
    assert(region != nullptr);
    auto *s = new(region) slab();
    auto *base = reinterpret_cast<uint8_t *>(region) + sizeof(slab);
    s->iova = mmu::virt_to_phys(s);
    size_t off = color;
    s->freelist = new (base + off) obj_header;
    size_t space = kSlabSize - sizeof(slab);
    while (off + 2 * c.obj_size <= space) {
      auto *obj = reinterpret_cast<obj_header *>(base + off);
      obj->next = new (base + off + c.obj_size) obj_header;
      obj->iova = s->iova + sizeof(slab) + off;
      off += c.obj_size;
    }
    auto *obj = reinterpret_cast<obj_header *>(base + off);
    obj->next = nullptr;
    obj->iova = s->iova + sizeof(slab) + off;
    c.partial.list_push(s);
    color = (color + 64) & 1023;
    assert(off + sizeof(slab) <= kSlabSize);
    assert(!cache.partial.empty());
  }

  __inline void free_single_mbuf(mbuf *obj) {
    auto iptr = reinterpret_cast<intptr_t>(obj);
    auto *slb = reinterpret_cast<slab *>(iptr & ~(kSlabSize - 1));
    bool was_full = !slb->freelist;
    auto *hdr = reinterpret_cast<obj_header *>(obj);
    hdr->iova = slb->iova + (reinterpret_cast<uintptr_t>(obj) -
                             reinterpret_cast<uintptr_t>(slb));
    if (cache.top < slab_cache::kDefaultCacheSize) {
      hdr->next = nullptr;
      cache.mag[cache.top++] = hdr;
    } else {
      hdr->next = slb->freelist;
      slb->freelist = hdr;
      --slb->inuse;
      if (was_full) {
        slab::list_remove(slb);
        cache.partial.list_push(slb);
      }
    }
  }

  void free_mbuf(mbuf *obj) {
    assert(obj->refcnt == 0);
    auto *obj_ptr = obj;
    while (obj_ptr) {
      auto *next = obj_ptr->next;
      free_single_mbuf(obj_ptr);
      obj_ptr = next;
    }
  }

  constexpr size_t get_data_size() const { return kDefaultSize; }

  mbuf_ptr alloc_default_safe(uint16_t data_len) {
    auto *pkt = alloc_default(data_len);
    return mbuf_ptr(pkt, mbuf_free);
  }

  ~slab_allocator() {
    auto free_slabs = [](slab_cache::slab_list &list) {
      auto *s = list.head.next;
      while (s != &list.tail) {
        auto *next = s->next;
        memory::free_huge_page(s, mmu::huge_page_size);
        s = next;
      }
    };
    free_slabs(cache.partial);
    free_slabs(cache.full);
  }

private:
  slab_cache cache;
  unsigned color = 0;
public:
  void* priv;
  init_fn_t init_fn;
};

inline mbuf *alloc_mbuf(slab_allocator *sb, size_t size) {
  return sb->alloc_default(size);
}

inline void mbuf_free(mbuf *buf) {
  assert(buf->pool);
  assert(buf->refcnt >= 1);
  --buf->refcnt;
  if (buf->refcnt)
    return;
  buf->pool->free_mbuf(buf);
}

inline mbuf_ptr mbuf_take_owner_ship(mbuf *pkt) {
  return mbuf_ptr(pkt, &mbuf_free);
}
} // namespace minidpdk
