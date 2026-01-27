#pragma once

#include "debug.hh"
#include "message.hh"
#include "packet_scheduler.hh"
#include "util.hh"
#include <bypass/dev.hh>
#include <bypass/net.hh>
#include <cstdint>
#include <endian.h>

class packet_if {
  static constexpr uint16_t kdefaultTTL = 64;
  static constexpr uint16_t kdefaultARPTableSize = 1024;

public:
  packet_if(packet_scheduler *scheduler, uint32_t sip, rte_eth_dev *dev)
      : arp_table(kdefaultARPTableSize), scheduler(scheduler), sip(sip) {
    smac = dev->get<rte_eth_dev_data>()->mac_addr;
  }

  rte_udp_hdr *udp_header(message *msg, uint16_t sport, uint16_t dport) {
    auto *udp = msg->move_headroom<rte_udp_hdr>();
    udp->src_port = htobe16(sport);
    udp->dst_port = htobe16(dport);
    udp->dgram_cksum = 0;
    udp->dgram_len = htobe16(msg->pkt_len);
    msg->l4_len = sizeof(rte_udp_hdr);
    return udp;
  }

  void ip_header(message *msg, rte_udp_hdr *udp_header, uint32_t source,
                 uint32_t target) {
    auto *ipv4 = msg->move_headroom<rte_ipv4_hdr>();
    ipv4->src_addr = source;
    ipv4->dst_addr = target;
    ipv4->fragment_offset = 0;
    ipv4->next_proto_id = RTE_IPPROTO_UDP;
    ipv4->time_to_live = kdefaultTTL;
    ipv4->total_length = htobe16(msg->pkt_len);
    ipv4->hdr_checksum = 0;
    ipv4->version_ihl = VERSION_IHL;
    ipv4->type_of_service = 0;
    ipv4->packet_id = 0;
    msg->l3_len = sizeof(rte_ipv4_hdr);

    msg->ol_flags = 0;
    msg->ol_flags |=
        RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_UDP_CKSUM | RTE_MBUF_F_TX_IPV4;
    udp_header->dgram_cksum = phdr_cksum(ipv4, udp_header);
  }

  void eth_header(message *msg, const rte_ether_addr &smac,
                  const rte_ether_addr &dmac) {
    auto *eth = msg->move_headroom<rte_ether_hdr>();
    rte_ether_addr_copy(&dmac, &eth->dst_addr);
    rte_ether_addr_copy(&smac, &eth->src_addr);
    eth->ether_type = htobe16(RTE_ETHER_TYPE_IPV4);
    msg->l2_len = sizeof(rte_ether_hdr);
  }

  void consume_pkt(message *msg, uint16_t sport,
                   const con_config &tcon_config) {
    auto *udp = udp_header(msg, sport, tcon_config.port);
    ip_header(msg, udp, sip, tcon_config.ip);
    auto *addr = arp_table.lookup(tcon_config.ip);
    assert(addr);
    eth_header(msg, smac, *addr);
    FASTT_DUMP_PKT(msg, msg->len());
    scheduler->add_pkt(static_cast<rte_mbuf *>(msg));
  }

  void consume_for_retransmission(message *msg) { scheduler->add_pkt(msg); }

  void add_mapping(uint32_t ip, rte_ether_addr &addr) {
    arp_table.emplace(ip, addr);
  }

  void broken_packet(rte_mbuf *pkt) {
    FASTT_LOG_DEBUG("Got broken packet\n");
    FASTT_DUMP_PKT(static_cast<message *>(pkt), pkt->data_len);
    rte_pktmbuf_free(pkt);
  }

  bool check_ip_cksum(rte_mbuf *mbuf) {
    return !(mbuf->ol_flags & RTE_MBUF_F_RX_IP_CKSUM_BAD);
  }

  bool check_udp_cksum(rte_mbuf *mbuf) {
    return !(mbuf->ol_flags & RTE_MBUF_F_RX_L4_CKSUM_BAD);
  }

  bool check_ether(rte_mbuf *mbuf) {
    auto *eth = rte_pktmbuf_mtod(mbuf, rte_ether_hdr *);
    return eth->ether_type == htobe16(RTE_ETHER_TYPE_IPV4);
  }

  void strip_ether_ip(rte_mbuf *mbuf, flow_tuple &ft) {
    auto *eth = rte_pktmbuf_mtod(mbuf, rte_ether_hdr *);
    auto *ip =
        rte_pktmbuf_mtod_offset(mbuf, rte_ipv4_hdr *, sizeof(rte_ether_hdr));
    add_mapping(ip->src_addr, eth->src_addr);
    ft.sip = ip->src_addr;
    ft.dip = ip->dst_addr;
    mbuf->headroom_adj(sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr));
  }

  void strip_udp(rte_mbuf *mbuf, flow_tuple &ft) {
    auto *udp = rte_pktmbuf_mtod(mbuf, rte_udp_hdr *);
    ft.sport = udp->src_port;
    ft.dport = udp->dst_port;
    mbuf->headroom_adj(sizeof(rte_udp_hdr));
  }

  message *consume_pkt(rte_mbuf *mbuf, flow_tuple &ft) {
    if (!check_ether(mbuf)) {
      broken_packet(mbuf);
      return nullptr;
    }
    if (check_ip_cksum(mbuf))
      strip_ether_ip(mbuf, ft);
    else {
      broken_packet(mbuf);
      return nullptr;
    }
    if (check_udp_cksum(mbuf))
      strip_udp(mbuf, ft);
    else {
      broken_packet(mbuf);
      return nullptr;
    }
    return static_cast<message *>(mbuf);
  }

private:
  fixed_size_hash_table<uint32_t, rte_ether_addr> arp_table;
  rte_ether_addr smac;
  packet_scheduler *scheduler;
  uint32_t sip;
};
