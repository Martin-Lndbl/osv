#pragma once
#include "debug.hh"

#include <cstddef>
#include <cstdint>

#include "bypass/mem.hh"

class connection;

struct message : public rte_mbuf {
  void *data() { return rte_pktmbuf_mtod(this, void *); }
  uint16_t len() { return data_len; }

  uint64_t* get_ts() { return &ts; }

  void set_size(uint16_t len) { pkt_len = len; }

  void inc_refcnt() { ++refcnt; }

  template <typename T> T *move_headroom() {
    prepend<T>();
    return rte_pktmbuf_mtod(this, T *);
  }

  void shrink_headroom(uint16_t len) { headroom_adj(len); }
};

static_assert(sizeof(message) == sizeof(rte_mbuf), "");

class message_allocator {
  static constexpr uint16_t kRequiredHeadRoom = 128;
  static constexpr std::size_t kMempoolCacheSize = 256;
  static constexpr std::size_t kMemBufPrivSize = 0;
  static constexpr std::size_t kMemBufDataRoomSize = 2048;

public:
  message_allocator(const char *name, std::size_t elems)
      : pool(rte_pktmbuf_pool::rte_pktmbuf_pool_create(name, kMemBufDataRoomSize, elems, 0)) {
    assert(pool && "allocation failed");        
    payload_size = RTE_MBUF_DEFAULT_DATAROOM;
    assert(payload_size > 0);
    FASTT_LOG_DEBUG("Payload Size: %lu\n", payload_size);
  }

  message *alloc_message(uint16_t data_size) {
      
    assert(data_size < payload_size);
    if (data_size >= payload_size - kRequiredHeadRoom)
      return nullptr;
    rte_mbuf* mbuf;
    pool->alloc_bulk(&mbuf, 1);
    return prepare(mbuf, data_size);
  }

  static void deallocate(message *msg) { rte_pktmbuf_free(msg); }

  ~message_allocator() { rte_pktmbuf_pool::rte_pktmbuf_pool_delete(pool); }

private:
  message *prepare(rte_mbuf *mbuf, uint16_t data_size) {
    auto *msg = static_cast<message *>(mbuf);
    msg->data_len = data_size;
    msg->pkt_len = data_size;
    return msg;
  }
  std::size_t payload_size;
  rte_mempool *pool;
};
