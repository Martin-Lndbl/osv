#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>

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

  rte_mbuf_ext_shared_info *shinfo;

  // memory pool allocated from
  slab_allocator *pool;

  // NIC offload flags
  uint64_t ol_flags;

  // IO Virtual Address
  uintptr_t iova;

  // rss hash
  struct {
    uint64_t rss;
  } hash;

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

  // type of the packet
  uint32_t packet_type;

  mbuf() = default;
  mbuf(mbuf *next, slab_allocator *sb, uintptr_t iova, uint32_t size,
       uint16_t nb_segs, uint16_t data_len, uint16_t headroom)
      : next(next), buf_addr(reinterpret_cast<char *>(this)), shinfo(nullptr),
        pool(sb), iova(iova + sizeof(mbuf) + headroom), data_offset(headroom),
        pkt_len(), data_len(data_len), buf_len(size), refcnt(1),
        nb_segs(nb_segs) {}

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

struct slab {
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
  size_t obj_size;

  slab_cache(size_t obj_size)
      : partial(), full(), obj_size(obj_size) {}
};

inline void mbuf_free(mbuf *buf);

using init_fn_t = void (*)(mbuf **, uint16_t, void *);
class slab_allocator {
public:
  static constexpr size_t kDefaultHeadroom = 128;
  static constexpr size_t kMaxDataLen = 1816;
  static constexpr size_t kDefaultSize =
      kMaxDataLen + kDefaultHeadroom + sizeof(mbuf);
  static constexpr size_t kSlabSize = 2 * 1024 * 1024;

public:
  slab_allocator(unsigned size, size_t obj_size = kDefaultSize,
                 init_fn_t init_fn = nullptr, void *priv = nullptr)
      : cache(obj_size), elems(size), init_fn(init_fn), priv(priv) {
    for (auto &obj : elems)
      obj = alloc_default(0);
    top = size;
  }

  int alloc_bulk(mbuf **bufs, unsigned n) {
    if (top < n)
      return -ENOENT;
    for (unsigned i = 0; i < n; ++i) {
      auto *obj = elems[--top];
      auto iova = obj->iova;
      bufs[i] = new (obj)
          mbuf{nullptr, this, iova, kDefaultSize, 1, 0, kDefaultHeadroom};
    }
    return 0;
  }

  mbuf *alloc_single() {
    if (!top)
      return nullptr;
    auto *obj = elems[--top];
    auto iova = obj->iova;
    return new (obj)
          mbuf{nullptr, this, iova, kDefaultSize, 1, 0, kDefaultHeadroom};
  }

  void free_single_mbuf(mbuf *obj) {
    auto iptr = reinterpret_cast<intptr_t>(obj);
    auto *slb = reinterpret_cast<slab *>(iptr & ~(kSlabSize - 1));
    auto *hdr = reinterpret_cast<obj_header *>(obj);
    hdr->iova = slb->iova + (iptr - reinterpret_cast<intptr_t>(slb));
    assert(top < elems.size());
    elems[top++] = hdr;
  }

  void free_mbuf(mbuf *obj) {
    auto *obj_ptr = obj;
    while (obj_ptr) {
      auto *next = obj_ptr->next;
      free_single_mbuf(obj_ptr);
      obj_ptr = next;
    }
  }

  constexpr size_t get_data_size() const { return kDefaultSize; }

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
  obj_header *alloc_default(uint16_t data_len) {
    assert(data_len <= kMaxDataLen);
    obj_header *obj;
    if (cache.partial.empty())
        alloc_new_slab(cache);
    auto *s = cache.partial.front();
    obj = s->freelist;
    s->freelist = obj->next;
    return obj;
  }

  void alloc_new_slab(slab_cache &c) {
    auto *region = memory::alloc_huge_page(mmu::huge_page_size);
    assert(region != nullptr);
    auto *s = static_cast<slab *>(region);
    auto *base = reinterpret_cast<uint8_t *>(region) + sizeof(slab);
    s->iova = mmu::virt_to_phys(s);
    s->freelist = new (base) obj_header;
    size_t space = kSlabSize - sizeof(slab);
    size_t off = 0;
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
    assert(!cache.partial.empty());
  }
  slab_cache cache;
  std::vector<obj_header *> elems;
  unsigned top = 0;

public:
  init_fn_t init_fn;
  void *priv;
};

inline mbuf *alloc_mbuf(slab_allocator *sb, size_t size) {
  auto *mb = sb->alloc_single();
  mb->data_len = mb->pkt_len = size;
  return mb;
}

inline void mbuf_free(mbuf *buf) {
  assert(buf->pool);
  assert(buf->refcnt >= 1);
  --buf->refcnt;
  buf->pool->free_mbuf(buf);
}

} // namespace minidpdk
