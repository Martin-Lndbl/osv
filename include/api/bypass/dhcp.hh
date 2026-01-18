#ifndef BYPASS_DHCP_HH
#define BYPASS_DHCP_HH

#include <asm-generic/errno.h>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/optional/optional.hpp>
#include <bypass/dev.hh>
#include <bypass/mem.hh>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <osv/dhcp.hh>
#include <sys/param.h>
#include <tuple>

#include <bypass/net.hh>

template<int log_level = 4, typename S, typename ...Args>
void dhcp_log(S&& str, Args&&... args){
    if constexpr(log_level < 4){
        fprintf(stderr, str, args...);
    }
}
class dhcp_buf {
public:
  typedef std::tuple<boost::asio::ip::address_v4, boost::asio::ip::address_v4,
                     boost::asio::ip::address_v4>
      route_type;
  enum packet_type {
    DHCP_REQUEST = 1,
    DHCP_REPLY = 2,
  };

  enum dhcp_request_packet_type {
    DHCP_REQUEST_INIT_REBOOT = 1,
    DHCP_REQUEST_SELECTING = 2,
    DHCP_REQUEST_RENEWING = 3,
    DHCP_REQUEST_REBINDING = 4,
  };

  dhcp_buf(rte_mbuf *pkt) : _m(pkt) {}

  rte_mbuf **get() { return &_m; }
  void set(rte_mbuf *m) { _m = m; }

  void compose_discover(rte_ether_addr &addr);

  void compose_request(rte_ether_addr &src, u32 xid,
                       boost::asio::ip::address_v4 yip,
                       boost::asio::ip::address_v4 sip,
                       dhcp_request_packet_type request_packet_type,
                       std::string hostname);
  /* Decode packet */
  bool is_valid_dhcp();
  void decode_ip_len() {
    auto *ip = pip();
    _ip_len = ip->ip_hl << 2;
  }
  bool decode();

  u32 get_xid() { return pdhcp()->xid; }

  dhcp::dhcp_message_type get_message_type() { return _message_type; }
  boost::asio::ip::address_v4 get_router_ip() { return _router_ip; }
  boost::asio::ip::address_v4 get_dhcp_server_ip() { return _dhcp_server_ip; }
  boost::asio::ip::address_v4 get_your_ip() { return _your_ip; }
  std::vector<boost::asio::ip::address> get_dns_ips() { return _dns_ips; }
  boost::asio::ip::address_v4 get_subnet_mask() { return _subnet_mask; }
  boost::asio::ip::address_v4 get_broadcast_ip() { return _broadcast_ip; }
  u16 get_interface_mtu() { return _mtu; }
  int get_lease_time_sec() { return _lease_time_sec; }
  int get_renewal_time_sec() { return _renewal_time_sec; }
  int get_rebind_time_sec() { return _rebind_time_sec; }
  std::vector<route_type> &get_routes() { return _routes; }
  std::string get_hostname() { return _hostname; }
  const rte_ether_addr &get_server_mac() const { return server_mac; }

private:
  // Pointers for building DHCP packet
  ip *pip() { return reinterpret_cast<ip *>(rte_pktmbuf_mtod(_m, char*) + sizeof(rte_eth_header)); }
  udphdr *pudp() { return reinterpret_cast<udphdr *>(pip() + 1); }
  dhcp::dhcp_packet *pdhcp() {
    return reinterpret_cast<dhcp::dhcp_packet *>(pudp() + 1);
  }
  u8 *poptions() { return reinterpret_cast<u8 *>(pdhcp() + 1); }

  // Writes a new option to pos, returns new pos
  u8 *add_option(u8 *pos, u8 type, u8 len, u8 *buf) {
    pos[0] = type;
    pos[1] = len;
    memcpy(&pos[2], buf, len);

    return pos + 2 + len;
  }
  u8 *add_option(u8 *pos, u8 type, u8 len, u8 data) {
    pos[0] = type;
    pos[1] = len;
    memset(&pos[2], data, len);

    return pos + 2 + len;
  }

  void build_eth_ip_headers(rte_ether_addr &src, rte_ether_addr &dst) {
    auto *eth = rte_pktmbuf_mtod(_m, rte_eth_header*);
    eth->src = src;
    eth->dst = dst;
    eth->ether_type = htons(0x0800);
    _m->pkt_len += sizeof(rte_eth_header);
    _m->data_len += sizeof(rte_eth_header);
    _m->l2_len = sizeof(rte_eth_header);
    _m->nb_segs = 1;
  }

  // Packet assembly
  void build_udp_ip_headers(size_t dhcp_len, in_addr_t src_addr,
                            in_addr_t dest_addr) {
    ip *ip = pip();
    udphdr *udp = pudp();
    _m->data_len = _m->pkt_len = dhcp_len + dhcp::udp_len + dhcp::min_ip_len;
    _m->l3_len = sizeof(*ip);
    _m->l4_len = sizeof(udphdr);

    memset(ip, 0, sizeof(*ip));
    ip->ip_v = IPVERSION;
    ip->ip_hl = dhcp::min_ip_len >> 2;
    ip->ip_len = htons(dhcp::min_ip_len + dhcp::udp_len + dhcp_len);
    ip->ip_id = 0;
    ip->ip_ttl = 128;
    ip->ip_p = IPPROTO_UDP;
    ip->ip_sum = 0;
    ip->ip_src.s_addr = src_addr;
    ip->ip_dst.s_addr = dest_addr;
    ip->ip_sum = 0;

    // UDP
    memset(udp, 0, sizeof(*udp));
    udp->uh_sport = htons(dhcp::dhcp_client_port);
    udp->uh_dport = htons(dhcp::dhcp_server_port);
    udp->uh_ulen = htons(dhcp::udp_len + dhcp_len);
    // FIXME: add a "proper" UDP checksum,
    // in the meanwhile, 0 will work as the RFC allows it.
    udp->uh_sum = 0;
    _m->ol_flags = RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_IPV4;
  }

  // mbuf related
  rte_mbuf *_m;
  rte_ether_addr server_mac;
  u16 _ip_len;

  // Decoded variables
  dhcp::dhcp_message_type _message_type;
  boost::asio::ip::address_v4 _router_ip;
  boost::asio::ip::address_v4 _dhcp_server_ip;
  // store DNS IPs as a vector of ip::address to ease working with IPV6
  // compatible libc
  std::vector<boost::asio::ip::address> _dns_ips;
  boost::asio::ip::address_v4 _subnet_mask;
  boost::asio::ip::address_v4 _broadcast_ip;
  boost::asio::ip::address_v4 _your_ip;
  u32 _lease_time_sec;
  u32 _renewal_time_sec;
  u32 _rebind_time_sec;
  u16 _mtu;
  std::vector<route_type> _routes;
  std::string _hostname;
};

struct dhcp_handler {
  enum State { DHCP_INIT, DHCP_DISCOVER, DHCP_REQUEST, DHCP_ACKNOWLEDGE };
  dhcp_handler(rte_eth_dev &dev, rte_pktmbuf_pool *pool, uint16_t rqid = 0,
               uint16_t tqid = 0)
      : state(DHCP_INIT), rqid(rqid), tqid(tqid), dev(dev), pool(pool) {
    server_ip = client_ip = boost::asio::ip::make_address_v4("0.0.0.0");
  }

  boost::optional<std::string> run_dhcp(){
      discover();
      if(get_response())
          return {};
      if(get_response())
          return {};
      return boost::optional<std::string>(client_ip.to_string());
  }

  void discover() {
    uint16_t sent = 0;
    rte_mbuf *pkt;
    pool->alloc_bulk(&pkt, 1);
    state = DHCP_DISCOVER;
    dhcp_buf dm(pkt);
    dm.compose_discover(dev.data.mac_addr);
    xid = dm.get_xid();
    do {
      sent = dev.tx_burst(tqid, dm.get(), 1);
    } while (!sent);
  }

  int get_response() {
    int ret;  
    uint16_t nb_rx = 0;
    rte_mbuf *pkt;
    do {
      nb_rx = dev.rx_burst(rqid, &pkt, 1);
      if (!nb_rx)
        continue;
      ret = process_packet(pkt);
    } while (ret == -EAGAIN);
    return ret;
  }

  int process_packet(rte_mbuf *pkt) {
    auto *eth = rte_pktmbuf_mtod(pkt, rte_eth_header*);
    if(eth->ether_type != htons(0x0800)){
        dhcp_log("Got wrong ether type: %x", ntohs(eth->ether_type));
        return -EAGAIN;
    }
    dhcp_buf dm(pkt);
    if (!dm.decode()){
      dhcp_log("Packet decoding failed\n"); 
      return -EINVAL;
    }

    if (state == DHCP_DISCOVER) {
      state_discover(dm);
    } else if (state == DHCP_REQUEST) {
      state_request(dm);
    }
    pkt->pool->free_bulk(&pkt, 1);
    return 0;
  }

  void state_discover(dhcp_buf &resp) {
    uint16_t sent = 0;
    rte_mbuf *pkt;
    std::string hostname("");
    if (resp.get_hostname().length() > 0)
      hostname = resp.get_hostname();
    pool->alloc_bulk(&pkt, 1);
    dhcp_buf dm(pkt);
    dm.compose_request(dev.data.mac_addr, xid, resp.get_your_ip(),
                       resp.get_dhcp_server_ip(),
                       dhcp_buf::DHCP_REQUEST_SELECTING, resp.get_hostname());
    do {
      sent = dev.tx_burst(tqid, dm.get(), 1);
    } while (!sent);
    state = DHCP_REQUEST;
  }

  void state_request(dhcp_buf &resp) {
    if (resp.get_message_type() == dhcp::DHCP_MT_NAK) {
      state = DHCP_INIT;
      server_ip = client_ip = boost::asio::ip::make_address_v4("0.0.0.0");
      discover();
    } else if (resp.get_message_type() == dhcp::DHCP_MT_ACK) {
      server_ip = resp.get_dhcp_server_ip();
      client_ip = resp.get_your_ip();
      state = DHCP_ACKNOWLEDGE;
    }
  }

  State state;
  uint64_t xid;
  uint16_t rqid, tqid;
  boost::asio::ip::address_v4 server_ip, client_ip;
  rte_eth_dev &dev;
  rte_pktmbuf_pool *pool;
};

#endif // !BYPASS_DHCP_HH
