#include <bypass/mem.hh>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <new>
#include <bsd/porting/netport.h>
#include <cerrno>
#include <osv/trace.hh>

void rte_pktmbuf_free(rte_mbuf* mbuf){
    mbuf->pool->free_bulk(&mbuf, 1);
}
void rte_mbuf_raw_free(rte_mbuf* mbuf){
    mbuf->pool->free_bulk(&mbuf, 1);
}

void rte_pktmbuf_free_bulk(rte_mbuf** pkts, uint16_t size){
    if(!size)
        return;
    pkts[0]->pool->free_bulk(pkts, size);
}


void rte_pktmbuf_read(rte_mbuf *m, uint32_t off,
	uint32_t len, uint8_t *buf)
{
    uint32_t buf_off = 0;
    for(auto* seg = m; len > 0 && seg;  seg = seg->next){
        auto copy_len = seg->buf_len - off;
        if(copy_len > seg->buf_len)
            copy_len = seg->buf_len;
        memcpy(reinterpret_cast<char*>(buf) + buf_off, rte_pktmbuf_mtod(seg, char*) + off, copy_len);
        off = 0;
		    buf_off += copy_len;
        len -= copy_len;
    }
}


static int mb_ctor_buf(void * mem, int, void *arg, int){
    rte_mbuf* mbuf = static_cast<rte_mbuf*>(mem);
    rte_pktmbuf_pool* pool = static_cast<rte_pktmbuf_pool*>(arg);
    assert(mbuf->refcnt == 0);
    mbuf->pool = pool;
    mbuf->next = nullptr;
    mbuf->refcnt = 1;
    return 0;
}

rte_pktmbuf_pool::~rte_pktmbuf_pool() = default;


rte_pktmbuf_pool* rte_pktmbuf_pool::rte_pktmbuf_pool_create(const char *name, uint32_t size, uint32_t elems, uint32_t flags = 0){
    rte_pktmbuf_pool* pool = static_cast<rte_pktmbuf_pool*>(malloc(sizeof(rte_pktmbuf_pool)));
    new (pool) rte_pktmbuf_pool(name, size, elems, flags);
    return pool;
}


void rte_pktmbuf_pool::rte_pktmbuf_pool_delete(rte_pktmbuf_pool *pb_pool){
    pb_pool->~rte_pktmbuf_pool();
    free(pb_pool);
}


TRACEPOINT(trace_rte_pktmbuf_pool_alloc_bulk, "pkts=%x, nb=%y", rte_mbuf**, uint16_t);
TRACEPOINT(trace_rte_pktmbuf_pool_alloc_bulk_ret, "");
int rte_pktmbuf_pool::alloc_bulk(struct rte_mbuf** pkts, uint16_t nb){
    trace_rte_pktmbuf_pool_alloc_bulk(pkts, nb);
    uint16_t i;
    if(!pool_impl.can_alloc(nb))
        return -ENOMEM;
    pool_impl.get(pkts, nb);
    for(i = 0; i < nb; ++i)
        mb_ctor_buf(pkts[i], 0, this, 0);
    
    trace_rte_pktmbuf_pool_alloc_bulk_ret();
    return 0;

}

/* add freeing chains of buffer */
void rte_pktmbuf_pool::free_bulk(struct rte_mbuf** pkts, uint16_t nb){
    for(uint16_t i = 0; i < nb; ++i)
        if(--pkts[i]->refcnt)
            pool_impl.put(pkts[i]);
}
