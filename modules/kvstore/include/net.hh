#pragma once

#include <bypass/bit.hh>
#include <bypass/defs.hh>
#include <bypass/dev.hh>
#include <bypass/mem.hh>
#include <bypass/net.hh>
#include <bypass/time.hh>
#include <bypass/util.hh>

#include <cstdint>
#include <iostream>
#include <memory>

using pool_ptr = std::shared_ptr<rte_pktmbuf_pool>;
static constexpr std::size_t MBUF_DEFAULT_SIZE = 1400;
static constexpr auto deleter = [](auto *p) {
  rte_pktmbuf_pool::rte_pktmbuf_pool_delete(p);
};

struct net_if {
  static rte_eth_conf conf;
  rte_eth_dev *dev;
  uint16_t port;
  net_if(uint16_t port) : port(port) {}

  void setup_pool(uint16_t rx_desc_lim, uint16_t tx_desc_lim, uint16_t bs,
                  pool_ptr &recv_pool, pool_ptr &send_pool) {
    recv_pool = {rte_pktmbuf_pool::rte_pktmbuf_pool_create(
                     nullptr, MBUF_DEFAULT_SIZE, rx_desc_lim + bs, 0),
                 deleter};
    send_pool = {rte_pktmbuf_pool::rte_pktmbuf_pool_create(
                     nullptr, MBUF_DEFAULT_SIZE, tx_desc_lim + bs, 0),
                 deleter};
  }

  int configure_port(uint16_t bs, std::vector<pool_ptr> &recv_pools,
                     std::vector<pool_ptr> &send_pools, uint16_t nq) {
    uint16_t nb_desc = 1024, tx_desc, rx_desc;
    rte_eth_dev_info info{};
    struct rte_eth_rxconf rxconf{};
    struct rte_eth_txconf txconf{};
    dev = eth_os::get_eth_for_port(0);
    if (!dev) {
      std::cout << "no dev" << std::endl;
      return ENODEV;
    }
    dev->get_dev_info(&info);
    rx_desc = std::min<uint16_t>(nb_desc, info.rx_desc_lim.nb_max);
    tx_desc = std::min<uint16_t>(nb_desc, info.tx_desc_lim.nb_max);
    for (uint16_t i = 0; i < nq; ++i)
      setup_pool(rx_desc, tx_desc, bs, recv_pools[i], send_pools[i]);
    dev->dev_configure(nq, nq, &conf);
    rxconf.offloads |= RTE_ETH_RX_OFFLOAD_CHECKSUM;
    txconf.offloads |=
        RTE_ETH_TX_OFFLOAD_UDP_CKSUM | RTE_ETH_TX_OFFLOAD_IPV4_CKSUM;
    for (uint16_t i = 0; i < nq; ++i) {
      if (dev->rx_queue_setup(i, rx_desc, 0, &rxconf, recv_pools[i].get())) {
        std::cout << "rx queue setup failed" << std::endl;
        return 1;
      }

      if (dev->tx_queue_setup(i, tx_desc, 0, &txconf)) {
        std::cout << "tx queue setup failed" << std::endl;
        return 1;
      }
    }
    if (dev->start()) {
      std::cout << "Starting dev failed" << std::endl;
      return 1;
    }
    return 0;
  }
};
