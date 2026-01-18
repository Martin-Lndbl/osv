#pragma once 

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <api/bypass/mem.hh>
#include <bypass/net.hh>
#include <endian.h>

static constexpr uint16_t ETHER_ADDR_LEN = 6;
static constexpr uint16_t IPV4 = 0x0800;
static constexpr uint8_t VERSION = 4;
static constexpr uint8_t VERSION_IHL = ((VERSION << 4) | 0x5);
static constexpr uint8_t TTL = 64;

static __inline void* PTR_ADD(const void* ptr, size_t x) { return ((void*)((uintptr_t)(ptr) + (x))); }

template<typename T>
static __inline T ALIGN_FLOOR(T val, uint32_t align){
    return static_cast<T>((val) & (~static_cast<T>(align - 1)));
}


struct [[gnu::packed]] ipv4_header{
    uint8_t  version_ihl;       
     uint8_t  type_of_service;   
     uint16_t total_length;    
     uint16_t packet_id;       
     uint16_t fragment_offset; 
     uint8_t  time_to_live;      
     uint8_t  next_proto_id;     
     uint16_t hdr_checksum;    
     uint32_t src_addr;        
     uint32_t dst_addr;
};

struct [[gnu::packed]] udp_header{
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t dgram_len;
    uint16_t dgram_cksum;

};

struct app_config{
    rte_ether_addr src;
    rte_ether_addr dst;
    uint32_t sip;
    uint32_t dip;
    uint32_t l4port;
    uint32_t data_len;
};

/* from dpdk */

 static inline uint32_t
 _raw_cksum(const void *buf, size_t len, uint32_t sum)
 {
     const void *end;
 
     for (end = PTR_ADD(buf, ALIGN_FLOOR(len, sizeof(uint16_t)));
          buf != end; buf = PTR_ADD(buf, sizeof(uint16_t))) {
         uint16_t v;
 
         memcpy(&v, buf, sizeof(uint16_t));
         sum += v;
     }
 
     /* if length is odd, keeping it byte order independent */
     if (len % 2) {
         uint16_t left = 0;
 
         memcpy(&left, end, 1);
         sum += left;
     }
 
     return sum;
 }

static inline uint16_t
 _raw_cksum_reduce(uint32_t sum)
 {
     sum = ((sum & 0xffff0000) >> 16) + (sum & 0xffff);
     sum = ((sum & 0xffff0000) >> 16) + (sum & 0xffff);
     return (uint16_t)sum;
 }

inline uint16_t phdr_cksum(ipv4_header* ipv4, udp_header* udp){
    struct ipv4_psd_header {
         uint32_t src_addr; /* IP address of source host. */
         uint32_t dst_addr; /* IP address of destination host. */
         uint8_t  zero;     /* zero. */
         uint8_t  proto;    /* L4 protocol type. */
         uint16_t len;      /* L4 length. */
     } psd_hdr;
  
     psd_hdr.src_addr = ipv4->src_addr;
     psd_hdr.dst_addr = ipv4->dst_addr;
     psd_hdr.zero = 0;
     psd_hdr.proto = ipv4->next_proto_id;
     psd_hdr.len = udp->dgram_len;
     auto sum = _raw_cksum(&psd_hdr, sizeof(psd_hdr), 0);
     return _raw_cksum_reduce(sum);
}

static void create_packet(const app_config& config, rte_mbuf *pkt){
    uint16_t len = config.data_len;
    memset(pkt->buf, 0, pkt->buf_len);
    rte_eth_header *eth = reinterpret_cast<rte_eth_header*>(pkt->buf);
    ipv4_header *ipv4 = reinterpret_cast<ipv4_header*>(eth + 1);
    udp_header *udp = reinterpret_cast<udp_header*>(ipv4 + 1);

    len += sizeof(*udp);
    udp->src_port = htobe16(config.l4port);
    udp->dst_port = htobe16(config.l4port);
    udp->dgram_len = htobe16(len);
    pkt->l4_len = sizeof(*udp);

    len += sizeof(*ipv4);
    ipv4->version_ihl = VERSION_IHL;
    ipv4->time_to_live = TTL;
    ipv4->next_proto_id = IPPROTO_UDP;
    ipv4->fragment_offset = 0;
    ipv4->packet_id = 0;
    ipv4->total_length = htobe16(len);
    ipv4->type_of_service = 0;
    ipv4->dst_addr = config.dip;
    ipv4->src_addr = config.sip;
    pkt->l3_len = sizeof(*ipv4);

    eth->src = config.src;
    eth->dst = config.dst;
    eth->ether_type = htobe16(IPV4);
    pkt->l2_len = sizeof(*eth);

    pkt->pkt_len = sizeof(*eth) + len;
    pkt->data_len = sizeof(*eth) + len;
    pkt->nb_segs = 1;
    udp->dgram_cksum = 0;
    ipv4->hdr_checksum = 0;
    pkt->ol_flags = RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_UDP_CKSUM | RTE_MBUF_F_TX_IPV4;

}

static bool verify_packet(const rte_mbuf* pkt){
    auto *eth = reinterpret_cast<const rte_eth_header*>(pkt->buf);
    if(eth->ether_type != htobe16(IPV4))
        return false;
    bool l3_valid = (pkt->ol_flags & RTE_MBUF_F_RX_IP_CKSUM_GOOD) || ((pkt->ol_flags & RTE_MBUF_F_RX_IP_CKSUM_MASK) == RTE_MBUF_F_RX_IP_CKSUM_UNKNOWN);
    bool l4_valid = (pkt->ol_flags & RTE_MBUF_F_RX_L4_CKSUM_GOOD) || ((pkt->ol_flags & RTE_MBUF_F_RX_L4_CKSUM_MASK) == RTE_MBUF_F_RX_L4_CKSUM_UNKNOWN);
    return l3_valid && l4_valid;
}
