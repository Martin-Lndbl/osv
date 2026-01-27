#pragma once

#include <bypass/dev.hh>
#include <bypass/mem.hh>
#include <cstdint>
#include <memory>
#include <vector>

namespace fastt {
int init();
};

struct iface {
  using netdev_iface =
      std::tuple<uint16_t, uint16_t, uint16_t, std::shared_ptr<rte_pktmbuf_pool>>;
  static std::unique_ptr<iface> configure_port(uint16_t port, uint16_t ntx,
                                             uint16_t nrx);
  void stop(){ eth_dev->stop(); }
  std::vector<std::shared_ptr<rte_mempool>> pools;
  uint16_t tx_queues, rx_queues;
  uint16_t port;
  rte_eth_dev* eth_dev;
  netdev_iface get_slice(uint16_t idx);
};
