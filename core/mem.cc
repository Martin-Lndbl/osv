#include <bypass/mem.hh>
#include <bypass/slab.hh>
#include <cassert>
#include <cstdint>
#include <malloc.h>
#include <bsd/porting/netport.h>
#include <osv/trace.hh>

void rte_pktmbuf_free(rte_mbuf* mbuf){
    sant::mbuf_free(mbuf);
}
void rte_mbuf_raw_free(rte_mbuf* mbuf){
    sant::mbuf_free(mbuf);
}


int rte_pktmbuf_alloc_bulk(rte_mempool* pool, rte_mbuf** pkts, uint16_t size){
    for(auto i = 0u; i < size; ++i)
        // adjust add region manually
        pkts[i] = pool->alloc_default(0);
    return 0;
}

void rte_pktmbuf_free_bulk(rte_mbuf** pkts, uint16_t size){
    for(auto i = 0u; i < size; ++i)
        sant::mbuf_free(pkts[i]);
}

const void* rte_pktmbuf_read(rte_mbuf *m, uint32_t off,
	uint32_t len, uint8_t *buf)
{
    return m->read(off, len, buf);
}

rte_mempool *rte_pktmbuf_pool_create(const char *name, unsigned n,
                                     unsigned cache_size, uint16_t priv_size,
                                     uint16_t data_room_size, int socket_id){
    assert(data_room_size <= sant::slab_allocator::kMaxDataLen);
    (void)cache_size;
    (void)priv_size;
    (void)socket_id;
    (void)data_room_size;
    auto *slab = malloc(sizeof(sant::slab_allocator));
    return new(slab) sant::slab_allocator();
}
void rte_mempool_free(rte_mempool *pool){
    pool->~slab_allocator();
    free(pool);
}

