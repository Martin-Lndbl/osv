#ifndef BYPASS_MEM_H
#define BYPASS_MEM_H

#include "osv/mmu-defs.hh"
#include "osv/pagealloc.hh"
#include "osv/virt_to_phys.hh"
#include <atomic>
#include <bypass/util.hh>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <osv/types.h>

#define RTE_MBUF_F_RX_VLAN (1ULL << 0)

#define RTE_MBUF_F_RX_RSS_HASH (1ULL << 1)

#define RTE_MBUF_F_RX_FDIR (1ULL << 2)

#define RTE_MBUF_F_RX_OUTER_IP_CKSUM_BAD (1ULL << 5)

#define RTE_MBUF_F_RX_VLAN_STRIPPED (1ULL << 6)

#define RTE_MBUF_F_RX_IP_CKSUM_MASK ((1ULL << 4) | (1ULL << 7))

#define RTE_MBUF_F_RX_IP_CKSUM_UNKNOWN 0
#define RTE_MBUF_F_RX_IP_CKSUM_BAD (1ULL << 4)
#define RTE_MBUF_F_RX_IP_CKSUM_GOOD (1ULL << 7)
#define RTE_MBUF_F_RX_IP_CKSUM_NONE ((1ULL << 4) | (1ULL << 7))

#define RTE_MBUF_F_RX_L4_CKSUM_MASK ((1ULL << 3) | (1ULL << 8))

#define RTE_MBUF_F_RX_L4_CKSUM_UNKNOWN 0
#define RTE_MBUF_F_RX_L4_CKSUM_BAD (1ULL << 3)
#define RTE_MBUF_F_RX_L4_CKSUM_GOOD (1ULL << 8)
#define RTE_MBUF_F_RX_L4_CKSUM_NONE ((1ULL << 3) | (1ULL << 8))

#define RTE_MBUF_F_RX_IEEE1588_PTP (1ULL << 9)

#define RTE_MBUF_F_RX_IEEE1588_TMST (1ULL << 10)

#define RTE_MBUF_F_RX_FDIR_ID (1ULL << 13)

#define RTE_MBUF_F_RX_FDIR_FLX (1ULL << 14)

#define RTE_MBUF_F_RX_QINQ_STRIPPED (1ULL << 15)

#define RTE_MBUF_F_RX_LRO (1ULL << 16)

/* There is no flag defined at offset 17. It is free for any future use. */

#define RTE_MBUF_F_RX_SEC_OFFLOAD (1ULL << 18)

#define RTE_MBUF_F_RX_SEC_OFFLOAD_FAILED (1ULL << 19)

#define RTE_MBUF_F_RX_QINQ (1ULL << 20)

#define RTE_MBUF_F_RX_OUTER_L4_CKSUM_MASK ((1ULL << 21) | (1ULL << 22))

#define RTE_MBUF_F_RX_OUTER_L4_CKSUM_UNKNOWN 0
#define RTE_MBUF_F_RX_OUTER_L4_CKSUM_BAD (1ULL << 21)
#define RTE_MBUF_F_RX_OUTER_L4_CKSUM_GOOD (1ULL << 22)
#define RTE_MBUF_F_RX_OUTER_L4_CKSUM_INVALID ((1ULL << 21) | (1ULL << 22))

/* add new RX flags here, don't forget to update RTE_MBUF_F_FIRST_FREE */

#define RTE_MBUF_F_FIRST_FREE (1ULL << 23)
#define RTE_MBUF_F_LAST_FREE (1ULL << 40) #

/* add new TX flags here, don't forget to update RTE_MBUF_F_LAST_FREE  */

#define RTE_MBUF_F_TX_OUTER_UDP_CKSUM (1ULL << 41)

#define RTE_MBUF_F_TX_UDP_SEG (1ULL << 42)

#define RTE_MBUF_F_TX_SEC_OFFLOAD (1ULL << 43)

#define RTE_MBUF_F_TX_MACSEC (1ULL << 44)

#define RTE_MBUF_F_TX_TUNNEL_VXLAN (0x1ULL << 45)
#define RTE_MBUF_F_TX_TUNNEL_GRE (0x2ULL << 45)
#define RTE_MBUF_F_TX_TUNNEL_IPIP (0x3ULL << 45)
#define RTE_MBUF_F_TX_TUNNEL_GENEVE (0x4ULL << 45)
#define RTE_MBUF_F_TX_TUNNEL_MPLSINUDP (0x5ULL << 45)
#define RTE_MBUF_F_TX_TUNNEL_VXLAN_GPE (0x6ULL << 45)
#define RTE_MBUF_F_TX_TUNNEL_GTP (0x7ULL << 45)
#define RTE_MBUF_F_TX_TUNNEL_ESP (0x8ULL << 45)
#define RTE_MBUF_F_TX_TUNNEL_IP (0xDULL << 45)
#define RTE_MBUF_F_TX_TUNNEL_UDP (0xEULL << 45)
/* add new TX TUNNEL type here */
#define RTE_MBUF_F_TX_TUNNEL_MASK (0xFULL << 45)

#define RTE_MBUF_F_TX_QINQ (1ULL << 49)

#define RTE_MBUF_F_TX_TCP_SEG (1ULL << 50)

#define RTE_MBUF_F_TX_IEEE1588_TMST (1ULL << 51)

/*
 * Bits 52+53 used for L4 packet type with checksum enabled: 00: Reserved,
 * 01: TCP checksum, 10: SCTP checksum, 11: UDP checksum. To use hardware
 * L4 checksum offload, the user needs to:
 *  - fill l2_len and l3_len in mbuf
 *  - set the flags RTE_MBUF_F_TX_TCP_CKSUM, RTE_MBUF_F_TX_SCTP_CKSUM or
 *    RTE_MBUF_F_TX_UDP_CKSUM
 *  - set the flag RTE_MBUF_F_TX_IPV4 or RTE_MBUF_F_TX_IPV6
 */

#define RTE_MBUF_F_TX_L4_NO_CKSUM (0ULL << 52)

#define RTE_MBUF_F_TX_TCP_CKSUM (1ULL << 52)

#define RTE_MBUF_F_TX_SCTP_CKSUM (2ULL << 52)

#define RTE_MBUF_F_TX_UDP_CKSUM (3ULL << 52)

#define RTE_MBUF_F_TX_L4_MASK (3ULL << 52)

#define RTE_MBUF_F_TX_IP_CKSUM (1ULL << 54)

#define RTE_MBUF_F_TX_IPV4 (1ULL << 55)

#define RTE_MBUF_F_TX_IPV6 (1ULL << 56)

#define RTE_MBUF_F_TX_VLAN (1ULL << 57)

#define RTE_MBUF_F_TX_OUTER_IP_CKSUM (1ULL << 58)

#define RTE_MBUF_F_TX_OUTER_IPV4 (1ULL << 59)

#define RTE_MBUF_F_TX_OUTER_IPV6 (1ULL << 60)

#define RTE_MBUF_F_TX_OFFLOAD_MASK                                             \
  (RTE_MBUF_F_TX_OUTER_IPV6 | RTE_MBUF_F_TX_OUTER_IPV4 |                       \
   RTE_MBUF_F_TX_OUTER_IP_CKSUM | RTE_MBUF_F_TX_VLAN | RTE_MBUF_F_TX_IPV6 |    \
   RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_L4_MASK |       \
   RTE_MBUF_F_TX_IEEE1588_TMST | RTE_MBUF_F_TX_TCP_SEG | RTE_MBUF_F_TX_QINQ |  \
   RTE_MBUF_F_TX_TUNNEL_MASK | RTE_MBUF_F_TX_MACSEC |                          \
   RTE_MBUF_F_TX_SEC_OFFLOAD | RTE_MBUF_F_TX_UDP_SEG |                         \
   RTE_MBUF_F_TX_OUTER_UDP_CKSUM)

#define RTE_MBUF_F_EXTERNAL (1ULL << 61)

#define RTE_MBUF_F_INDIRECT (1ULL << 62)
#define RTE_MBUF_PRIV_ALIGN 8

#define RTE_MBUF_DEFAULT_DATAROOM 2048
#define RTE_MBUF_DEFAULT_BUF_SIZE (RTE_MBUF_DEFAULT_DATAROOM)

class rte_pktmbuf_pool;
struct rte_mbuf;

template <typename T, T alignment> static constexpr T align(T val) {
  return (val + alignment - 1) & ~(alignment - 1);
}

#define rte_free free

void rte_pktmbuf_free(rte_mbuf *mbuf);
void rte_mbuf_raw_free(rte_mbuf *mbuf);
void rte_pktmbuf_free_bulk(rte_mbuf **pkts, uint16_t size);
void rte_pktmbuf_read(rte_mbuf *, uint32_t, uint32_t, uint8_t *);

struct rte_mbuf {
  // next in chain
  rte_mbuf *next;

  // address of the mbuf structure
  char *buf_addr;

  // timestamp
  uint64_t ts;

  // application private data
  void *priv;

  // memory pool allocated from
  rte_pktmbuf_pool *pool;

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
  std::atomic<uint16_t> refcnt;

  // length of the l2 header
  // length of the l3 header
  // length of the l4 header
  // number of segments in the chain
  uint16_t l2_len, l3_len, l4_len, nb_segs;

  // type of the packet
  uint32_t packet_type;

  void headroom_adj(uint32_t size) {
    assert(data_offset + size < buf_len);
    data_offset += size;
    pkt_len -= size;
    data_len -= size;
  }

  template <typename T> T *prepend() {
    data_offset -= sizeof(T);
    pkt_len += sizeof(T);
    data_len += sizeof(T);
    return reinterpret_cast<T *>(buf_addr + data_offset);
  }
};

#define rte_pktmbuf_mtod(m, t) reinterpret_cast<t>(m->buf_addr + m->data_offset)
#define rte_pktmbuf_mtod_offset(m, t, o) reinterpret_cast<t>(m->buf_addr + m->data_offset + o)

using rte_mempool = rte_pktmbuf_pool;
struct pageheader {
  pageheader *next;
  uintptr_t phys;
};

template <typename T> struct objheader {
  objheader *next;
  T obj;
};
template <typename Obj> struct Pool {
  static constexpr uint16_t kMaxHeadRoomSize = 128;
  static constexpr uint16_t kOffsetInHugePage = sizeof(pageheader);
  using element_type = Obj;

  pageheader *memory;
  objheader<element_type> *objs;
  uint64_t elems, inuse, alloc_size;

  Pool(uint32_t size, uint32_t elems)
      : memory(nullptr), objs(nullptr), elems(elems), inuse(0) {
    alloc_size = align<uint64_t, RTE_CACHE_LINE_SIZE>(
        size + kMaxHeadRoomSize + sizeof(objheader<element_type>));
    uint32_t offset = mmu::huge_page_size;
    for (uint32_t i = 0; i < elems; ++i) {
      if (mmu::huge_page_size - offset < alloc_size) {
        auto *page = static_cast<pageheader *>(
            memory::alloc_huge_page(mmu::huge_page_size));
        offset = kOffsetInHugePage;
        page->next = memory;
        memory = page;
        page->phys = mmu::virt_to_phys(page);
        assert((memory->phys & (mmu::huge_page_size - 1)) == 0);
      }

      auto *data = reinterpret_cast<char *>(memory) + offset;
      auto *obj = reinterpret_cast<objheader<element_type> *>(data);
      new (&obj->obj) element_type{};
      obj->obj.buf_len = alloc_size;
      obj->obj.buf_addr = reinterpret_cast<char *>(&obj->obj);
      obj->obj.data_offset = sizeof(element_type) + kMaxHeadRoomSize;
      obj->obj.iova = memory->phys + offset + sizeof(objheader<element_type>) +
                      kMaxHeadRoomSize;
      obj->next = objs;

      objs = obj;
      offset += alloc_size;
    }
  }

  void put(element_type *obj) {
    objheader<element_type> *header =
        reinterpret_cast<objheader<element_type> *>(
            reinterpret_cast<uint8_t *>(obj) -
            offsetof(objheader<element_type>, obj));
    header->next = objs;
    objs = header;
    ++elems;
    --inuse;
  }

  uint32_t can_alloc(uint32_t n) { return n <= elems; }

  int get(element_type **elems, uint32_t n) {
    assert(can_alloc(n));
    for (uint32_t i = 0; i < n; ++i) {
      elems[i] = &objs->obj;
      objs = objs->next;
    }
    elems -= n;
    inuse += n;
    return 0;
  }

  ~Pool() {
    for (auto *it = memory; it;) {
      auto *to_free = it;
      it = it->next;
      memory::free_huge_page(to_free, mmu::huge_page_size);
    }
  }
};

class rte_pktmbuf_pool {
  static constexpr uint32_t cache_size = 256;

public:
  rte_pktmbuf_pool(const char *name, uint32_t size, uint32_t elems,
                   uint32_t flags)
      : pool_impl(size, elems), data_size(elems) {
    (void)name;
    (void)flags;
  }

  ~rte_pktmbuf_pool();

  rte_pktmbuf_pool(const rte_pktmbuf_pool &) = delete;
  rte_pktmbuf_pool(rte_pktmbuf_pool &&) noexcept = delete;

  static rte_pktmbuf_pool *rte_pktmbuf_pool_create(const char *name,
                                                   uint32_t size,
                                                   uint32_t elems,
                                                   uint32_t flags);
  static void rte_pktmbuf_pool_delete(rte_pktmbuf_pool *pb_pool);

  int alloc_bulk(rte_mbuf **pkts, uint16_t nb);
  void free_bulk(rte_mbuf **pkts, uint16_t nb);

  uint64_t get_stat() const { return malloc_stat; }
  uint32_t get_data_size() const { return data_size; }

  template <typename F> void init(F &&fun) {
    auto *obj = pool_impl.objs;
    for (; obj; obj = obj->next)
      fun(&obj->obj);
  }

private:
  Pool<rte_mbuf> pool_impl;
  uint32_t data_size;
  uint64_t malloc_stat = 0;
};

#endif // !BYPASS_MEM_H
