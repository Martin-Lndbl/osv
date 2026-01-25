#ifndef HEADERS_H
#define HEADERS_H

#include <api/bypass/mem.hh>
#include <bypass/net.hh>
#include <endian.h>

struct app_config{
    rte_ether_addr src;
    rte_ether_addr dst;
    uint32_t sip;
    uint32_t dip;
    uint32_t l4port;
    uint32_t data_len;
};


static void create_packet(const app_config& config, rte_mbuf *pkt){
    uint16_t len = config.data_len;
    rte_ether_hdr *eth = rte_pktmbuf_mtod(pkt, rte_ether_hdr*);
    rte_ipv4_hdr *ipv4 = reinterpret_cast<rte_ipv4_hdr*>(eth + 1);
    rte_udp_hdr *udp = reinterpret_cast<rte_udp_hdr*>(ipv4 + 1);

    len += sizeof(*udp);
    udp->src_port = htobe16(config.l4port);
    udp->dst_port = htobe16(config.l4port);
    udp->dgram_len = htobe16(len);
    pkt->l4_len = sizeof(*udp);

    len += sizeof(*ipv4);
    ipv4->version_ihl = VERSION_IHL;
    ipv4->time_to_live = TTL;
    ipv4->next_proto_id = RTE_IPPROTO_UDP;
    ipv4->fragment_offset = 0;
    ipv4->packet_id = 0;
    ipv4->total_length = htobe16(len);
    ipv4->type_of_service = 0;
    ipv4->dst_addr = config.dip;
    ipv4->src_addr = config.sip;
    pkt->l3_len = sizeof(*ipv4);

    eth->src_addr = config.src;
    eth->dst_addr = config.dst;
    eth->ether_type = htobe16(RTE_ETHER_TYPE_IPV4);
    pkt->l2_len = sizeof(*eth);

    pkt->pkt_len = sizeof(*eth) + len;
    pkt->data_len = sizeof(*eth) + len;
    pkt->nb_segs = 1;
    udp->dgram_cksum = 0;
    ipv4->hdr_checksum = 0;
    pkt->ol_flags = RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_UDP_CKSUM | RTE_MBUF_F_TX_IPV4;

}

static bool verify_packet(const rte_mbuf* pkt){
    auto *eth = rte_pktmbuf_mtod(pkt, rte_ether_hdr*);
    if(eth->ether_type != htobe16(RTE_ETHER_TYPE_IPV4))
        return false;
    bool l3_valid = (pkt->ol_flags & RTE_MBUF_F_RX_IP_CKSUM_GOOD) || ((pkt->ol_flags & RTE_MBUF_F_RX_IP_CKSUM_MASK) == RTE_MBUF_F_RX_IP_CKSUM_UNKNOWN);
    bool l4_valid = (pkt->ol_flags & RTE_MBUF_F_RX_L4_CKSUM_GOOD) || ((pkt->ol_flags & RTE_MBUF_F_RX_L4_CKSUM_MASK) == RTE_MBUF_F_RX_L4_CKSUM_UNKNOWN);
    return l3_valid && l4_valid;
}
#endif // !HEADERS_H
