#pragma once
#include <minidpdk/slab.hh>
#include <memory>
#include <minidpdk/mem.hh>

struct dpdk_allocator {
  using backend_data = minidpdk::rte_mbuf_ext_shared_info;  
  static std::shared_ptr<dpdk_allocator> create(const char *name, unsigned n, void* priv = nullptr, minidpdk::init_fn_t init_fn = nullptr) {
    auto *pool = rte_pktmbuf_pool_create(
        name, n, 0, 0, minidpdk::slab_allocator::kMaxDataLen, SOCKET_ID_ANY);
    pool->priv = priv;
    pool->init_fn = init_fn;
    assert(pool);
    return std::make_shared<dpdk_allocator>(pool);
  }
  dpdk_allocator(rte_mempool *pool) : pool(pool) {}
  ~dpdk_allocator(){
      rte_mempool_free(pool);
  }
  rte_mempool *get() { return pool; }
  rte_mempool *pool;
};
