#include <bypass/fastt/iface.hh>
#include <bypass/fastt/log.hh>
#include <bypass/fastt/message.hh>
#include <bypass/bit.hh>
#include <bypass/defs.hh>
#include <bypass/dev.hh>
#include <bypass/mem.hh>
#include <bypass/rss.hh>
#include <cstdint>
#include <memory>
#include <tuple>

static auto deleter = [](rte_mempool *pool) {
  if (pool)
    rte_pktmbuf_pool::rte_pktmbuf_pool_delete(pool);
};

std::unique_ptr<iface> iface::configure_port(uint16_t port_id, uint16_t ntx,
                                           uint16_t nrx) {
  uint16_t nb_rxd, nb_txd;
  int retval;
  std::unique_ptr<iface> ifc{new iface{}};
  ifc->port = port_id;
  struct rte_eth_dev_info dev_info;
  struct rte_eth_rxconf rxconf{};
  struct rte_eth_txconf txconf{};
  ifc->eth_dev = eth_os::get_eth_for_port(port_id);
  if (!ifc->eth_dev)
    return nullptr;
  rte_eth_conf port_conf{};
  auto* eth_dev = ifc->eth_dev;
  retval = eth_dev->get_dev_info(&dev_info);
  if (retval != 0)
    return nullptr;
  nb_rxd = dev_info.rx_desc_lim.nb_max;
  nb_txd = dev_info.tx_desc_lim.nb_max;

  if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
    port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;
  if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_IPV4_CKSUM)
    port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_IPV4_CKSUM;
  if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_UDP_CKSUM)
    port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_UDP_CKSUM;

  if (dev_info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_UDP_CKSUM)
    port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_UDP_CKSUM;
  if (dev_info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_IPV4_CKSUM)
    port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_IPV4_CKSUM;
  if (dev_info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_RSS_HASH) {
    auto &rssconf = port_conf.rx_adv_conf.rss_conf;
    port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_RSS_HASH;
    port_conf.rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
    rssconf.algorithm = RTE_ETH_HASH_FUNCTION_DEFAULT;
    rssconf.rss_key = nullptr;
    rssconf.rss_hf =
        RTE_ETH_RSS_NONFRAG_IPV4_UDP & dev_info.flow_type_rss_offloads;
  }
  eth_dev->dev_configure(nrx, ntx, &port_conf);

  if (retval)
    return nullptr;
  txconf = dev_info.default_txconf;
  txconf.offloads = port_conf.txmode.offloads;
  rxconf = dev_info.default_rxconf;
  rxconf.offloads = port_conf.rxmode.offloads;
  uint16_t lcore_id = 0;
  uint16_t setup_tx = 0;
  uint16_t setup_rx = 0;
  for (auto i = 0u; i < 10; ++i) {
    ifc->pools.emplace_back(rte_pktmbuf_pool::rte_pktmbuf_pool_create(
                               std::to_string(lcore_id).data(), 2 * nb_rxd,
                               RTE_MBUF_DEFAULT_BUF_SIZE, 0),
                           deleter);
    if (eth_dev->rx_queue_setup(setup_rx++, nb_rxd, 0, &rxconf,
                                ifc->pools.back().get()))
      return nullptr;
    if (eth_dev->tx_queue_setup(setup_tx++, nb_txd, 0, &txconf))
      return nullptr;
  }
  ifc->tx_queues = setup_tx;
  ifc->rx_queues = setup_rx;
  retval = eth_dev->start();
  if (retval < 0)
    return nullptr;
  return ifc;
}

iface::netdev_iface iface::get_slice(uint16_t idx) {
  assert(idx < tx_queues);
  return std::make_tuple(port, idx, idx, pools[idx]);
}
