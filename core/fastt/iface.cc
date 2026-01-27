#include <bypass/fastt/iface.hh>
#include <bypass/fastt/debug.hh>
#include <bypass/fastt/message.hh>
#include <bypass/bit.hh>
#include <bypass/defs.hh>
#include <bypass/dev.hh>
#include <bypass/mem.hh>
#include <bypass/rss.hh>
#include <cstdint>
#include <memory>
#include <tuple>

static uint8_t RSS_DEFAULT_KEY[] = {
    0xbe, 0xac, 0x01, 0xfa, 0x6a, 0x42, 0xb7, 0x3b, 0x80, 0x30,
    0xf2, 0x0c, 0x77, 0xcb, 0x2d, 0xa3, 0xae, 0x7b, 0x30, 0xb4,
    0xd0, 0xca, 0x2b, 0xcb, 0x43, 0xa3, 0x8f, 0xb0, 0x41, 0x67,
    0x25, 0x3d, 0x25, 0x5b, 0x0e, 0xc2, 0x6d, 0x5a, 0x56, 0xda};

static constexpr unsigned RSS_KEY_LEN = 40;


static auto deleter = [](rte_mempool *pool) {
  if (pool)
    rte_pktmbuf_pool::rte_pktmbuf_pool_delete(pool);
};

static inline int setup_reta(rte_eth_dev *dev, uint32_t nrx, uint32_t reta_size){
    auto groups = reta_size / RTE_ETH_RETA_GROUP_SIZE;
    std::vector<rte_eth_rss_reta_entry64> reta(groups);

    for(auto i = 0u; i < reta_size; ++i)
        reta[i / RTE_ETH_RETA_GROUP_SIZE].mask = UINT64_MAX;

    for(auto i = 0u; i < reta_size; ++i){
        uint32_t reta_id = i / RTE_ETH_RETA_GROUP_SIZE;
        uint32_t reta_pos = i % RTE_ETH_RETA_GROUP_SIZE;
        uint32_t rss_qid = i % nrx;
        reta[reta_id].reta[reta_pos] = static_cast<uint16_t>(rss_qid);
    }

    int ret = dev->rss_reta_update(reta.data(), reta_size);
    if(ret)
        return -1;
    return 0;
}

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
    rssconf.rss_key = RSS_DEFAULT_KEY;
    rssconf.rss_key_len = RSS_KEY_LEN;
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
  setup_reta(ifc->eth_dev, nrx, dev_info.reta_size);
  if (retval < 0)
    return nullptr;
  return ifc;
}

iface::netdev_iface iface::get_slice(uint16_t idx) {
  assert(idx < tx_queues);
  return std::make_tuple(port, idx, idx, pools[idx]);
}
