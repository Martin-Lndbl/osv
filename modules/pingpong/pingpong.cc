#include <algorithm>
#include <arpa/inet.h>
#include <bypass/mem.hh>
#include <bypass/net.hh>
#include <bypass/time.hh>
#include <bypass/util.hh>
#include <cassert>
#include <cerrno>
#include <cstdint>

#include <algorithm>
#include <api/bypass/dev.hh>
#include <api/bypass/mem.hh>
#include <bypass/defs.hh>
#include <cstring>
#include <ctime>
#include <endian.h>
#include <features.h>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <ostream>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
#include <signal.h>
#include "net.hh"
#include <osv/sched.hh>

static constexpr uint32_t pbuf_sz = 1400;

#define SWAP(val1, val2)                                                       \
  do {                                                                         \
    auto temp = val1;                                                          \
    val1 = val2;                                                               \
    val2 = temp;                                                               \
  } while (0);

using pool_ptr =
    std::unique_ptr<rte_pktmbuf_pool,
                    decltype(std::addressof(
                        rte_pktmbuf_pool::rte_pktmbuf_pool_delete))>;

struct payload {
  uint64_t ticks;
};

static volatile int terminate = 0;
static void handler(int sig) {
  (void)sig;
  terminate = 1;
}

template <typename T> static __inline T pun(rte_mbuf *pbuf) {
  char *data = rte_pktmbuf_mtod(pbuf, char*) + sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr) +
               sizeof(rte_ether_hdr);
  T ret_data;
  std::memcpy(&ret_data, data, sizeof(T));
  return ret_data;
}

template <typename T> static __inline void move_data(rte_mbuf *pbuf, T &data) {
  char *data_ptr = rte_pktmbuf_mtod(pbuf, char*) + sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr) +
                   sizeof(rte_ether_hdr);
  memcpy(data_ptr, &data, sizeof(T));
}

template<typename T> static __inline void prefetch(rte_mbuf *pbuf, T& data){
    rte_prefetch0_write(rte_pktmbuf_mtod(pbuf, char*) + sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr) + sizeof(rte_ether_hdr));
}

struct port_config {
  pool_ptr pool;
  pool_ptr send_pool;
  app_config app;
  rte_eth_dev *dev;
  uint64_t rt;
  uint16_t burst_size;
  uint64_t ticks = 0, pkts = 0, faulty = 0;
  port_config()
      : pool(rte_pktmbuf_pool::rte_pktmbuf_pool_create("pool", pbuf_sz, 4095, 0),
             &rte_pktmbuf_pool::rte_pktmbuf_pool_delete),
        send_pool(rte_pktmbuf_pool::rte_pktmbuf_pool_create("spool", pbuf_sz, 2047, 0),
                  &rte_pktmbuf_pool::rte_pktmbuf_pool_delete),
        rt(300), burst_size(1) {}
};

static rte_eth_conf conf = {
    .rxmode = {.offloads = RTE_ETH_RX_OFFLOAD_CHECKSUM},
    .txmode = {.offloads = RTE_ETH_TX_OFFLOAD_UDP_CKSUM |
                           RTE_ETH_TX_OFFLOAD_IPV4_CKSUM}};

static int configure_port(port_config &pconf) {
  uint16_t nb_desc = 1024, tx_desc, rx_desc;
  rte_eth_dev_info info{};
  struct rte_eth_rxconf rxconf{};
  struct rte_eth_txconf txconf{};
  pconf.dev = eth_os::get_eth_for_port(0);
  if (!pconf.dev) {
    std::cout << "no dev" << std::endl;
    return ENODEV;
  }
  pconf.dev->get_dev_info(&info);
  rx_desc = std::min<uint16_t>(nb_desc, info.rx_desc_lim.nb_max);
  tx_desc = std::min<uint16_t>(nb_desc, info.tx_desc_lim.nb_max);
  pconf.dev->dev_configure(1, 1, &conf);
  rxconf.offloads |= RTE_ETH_RX_OFFLOAD_CHECKSUM;
  txconf.offloads |=
      RTE_ETH_TX_OFFLOAD_UDP_CKSUM | RTE_ETH_TX_OFFLOAD_IPV4_CKSUM;
  if (pconf.dev->rx_queue_setup(0, rx_desc, 0, &rxconf, pconf.pool.get())) {
    std::cout << "rx queue setup failed" << std::endl;
    return 1;
  }

  if (pconf.dev->tx_queue_setup(0, tx_desc, 0, &txconf)) {
    std::cout << "tx queue setup failed" << std::endl;
    return 1;
  }
  memcpy(pconf.app.src.addr.data(), pconf.dev->data.mac_addr.addr.data(),
         sizeof(pconf.app.src));
  if (pconf.dev->start()) {
    std::cout << "Starting dev failed" << std::endl;
    return 1;
  }
  return 0;
}

static void init_packets(const std::vector<rte_mbuf *> &pkts) {
  payload payload{static_cast<uint64_t>(rte_get_timer_cycles())};
  for (auto *pkt : pkts){
    move_data(pkt, payload);
  }
}

static void close_port(port_config &pconf) { pconf.dev->stop(); }

static uint16_t receive_packets_ping(port_config &pconf,
                                     std::vector<rte_mbuf *> &pkts,
                                     uint16_t nb_rx) {
  uint16_t total = 0;
  auto ticks = rte_get_timer_cycles();
  for (uint16_t i = 0; i < nb_rx; ++i) {
    if (!verify_packet(pkts[i])) {
      pconf.faulty++;
      continue;
    }

    auto pticks = pun<payload>(pkts[i]);
    auto diff = ticks - pticks.ticks;
    pconf.ticks += diff;
    ++pconf.pkts;
    ++total;
  }
  pconf.pool->free_bulk(pkts.data(), nb_rx);
  return total;
}

static int receive_packets_pong(port_config& pconf, rte_mbuf *pkt) {
  if (!verify_packet(pkt))
    return -1;
  rte_ether_hdr *eth = rte_pktmbuf_mtod(pkt, rte_ether_hdr*);
  rte_ipv4_hdr *ipv4 = reinterpret_cast<rte_ipv4_hdr *>(eth + 1);
  rte_udp_hdr *udp = reinterpret_cast<rte_udp_hdr *>(ipv4 + 1);
  eth->dst_addr = eth->src_addr;
  eth->src_addr = pconf.app.src;
  SWAP(ipv4->dst_addr, ipv4->src_addr);
  SWAP(udp->dst_port, udp->src_port);
  udp->dgram_cksum = 0;
  ipv4->hdr_checksum = 0;
  ipv4->time_to_live = TTL;
  pkt->l2_len = sizeof(*eth);
  pkt->l3_len = sizeof(*ipv4);
  pkt->l4_len = sizeof(*udp);
  pkt->ol_flags =
      RTE_MBUF_F_TX_UDP_CKSUM | RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_IP_CKSUM;
  return 0;
}

static void do_ping(port_config &pconf) {
  uint16_t nb_rx = 0, burst_size = 1, total = 0;
  uint16_t nb_tx = burst_size;

  std::vector<rte_mbuf *> pkts(burst_size, nullptr);
  std::vector<rte_mbuf *> rpkts(burst_size, nullptr);
  // const auto max_cycles_per_it = rte_get_timer_hz();
  auto cycles = rte_get_timer_cycles();
  auto end = cycles + pconf.rt * rte_get_timer_hz();
  pconf.send_pool->init([&](rte_mbuf *pkt) { create_packet(pconf.app, pkt); });
  for (; cycles < end; cycles = rte_get_timer_cycles()) {
      if (pconf.send_pool->alloc_bulk(pkts.data(), nb_tx)){
          std::cerr << "not enough buffers" << std::endl;  
          continue;
    }
    init_packets(pkts);
    nb_tx = pconf.dev->tx_burst(0, pkts.data(), burst_size);
    // auto deadline = cycles + max_cycles_per_it;
    total = 0;
    do {
      nb_rx = pconf.dev->rx_burst(0, rpkts.data(), burst_size);
      if (nb_rx)
        total += receive_packets_ping(pconf, rpkts, nb_rx);
    } while (total < nb_tx && rte_get_timer_cycles() < end);
  }

  std::cout << "Latency:"
            << (static_cast<double>(pconf.ticks) / (rte_get_timer_hz() / 1e6)) /
                   (static_cast<double>(pconf.pkts))
            << std::endl;
  std::cout << "PPS:" << static_cast<double>(pconf.pkts) / pconf.rt
            << std::endl;
}

static void do_pong(port_config &pconf) {
  rte_eth_stats stats;
  pconf.dev->get_stats(&stats);
  std::cerr << stats.imissed << ", " << stats.ierrors << ", " << stats.ipackets
            << ", " << stats.ibytes << std::endl;
  uint16_t nb_rx = 0, burst_size = pconf.burst_size;
  uint16_t nb_tx = burst_size;
  uint16_t nb_rm = 0;
  std::vector<rte_mbuf *> pkts(burst_size, nullptr);
  std::vector<rte_mbuf *> rpkts(burst_size, nullptr);
  for (; !terminate;) {
    nb_rx = pconf.dev->rx_burst(0, rpkts.data(), burst_size - nb_rm);
    for (uint16_t i = 0; i < nb_rx; ++i) {
      pkts[nb_rm] = rpkts[i];
      if (!receive_packets_pong(pconf, pkts[nb_rm]))
        ++nb_rm;
    }
    nb_tx = pconf.dev->tx_burst(0, pkts.data(), nb_rm);
    for (uint16_t j = 0, i = nb_tx; i < nb_rm; ++i, ++j)
      pkts[j] = pkts[i];
    nb_rm = nb_rm - nb_tx;
  }
  pconf.dev->get_stats(&stats);
  std::cerr << stats.imissed << ", " << stats.ierrors << ", " << stats.ipackets
            << ", " << stats.ibytes << std::endl;
}

enum mode { PING, PONG };
static constexpr uint16_t tu_size = 60 - sizeof(rte_ether_hdr);

int main(int argc, char *argv[]) {
  struct sigaction sa{};
  sa.sa_handler = handler;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  std::string mode, sip, dip, dmac;
  port_config pconf;
  sched::thread::current()->pin(sched::cpus.front());
  enum mode opmode = PONG;
  std::cout << "mode" << std::endl;
  std::cin >> mode;
  if (mode == "ping") {
    std::cin >> sip >> dip >> pconf.app.l4port >> dmac;
    pconf.app.sip = inet_addr(sip.c_str());
    pconf.app.dip = inet_addr(dip.c_str());
    pconf.app.dst.parse_string(dmac.c_str());
    pconf.app.data_len = tu_size - sizeof(rte_ipv4_hdr) - sizeof(rte_udp_hdr);
    opmode = PING;
  }
  sched::update_disable_reschedule(true);
  std::cout << "burst_size" << std::endl;
  std::cin >> pconf.burst_size;
  std::cout << "runtime" << std::endl;
  std::cin >> pconf.rt;
  if (configure_port(pconf))
    return -1;
  switch (opmode) {
  case PING:
    do_ping(pconf);
    break;
  case PONG:
    do_pong(pconf);
    break;
  }
  sched::update_disable_reschedule(false);

  std::cerr << pconf.pool->get_stat() << std::endl;
  close_port(pconf);
}
