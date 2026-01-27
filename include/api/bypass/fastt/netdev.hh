#pragma once

#include "debug.hh"
#include "message.hh"
#include <bypass/dev.hh>
#include <cstdint>
#include <bypass/time.hh>

#include <array>

class netdev {
  static constexpr uint16_t kDefaultInputBurstSize = 32;

public:
  netdev(rte_eth_dev* dev, uint16_t txq, uint16_t rxq)
      : to_us(rte_get_timer_hz() / 1e6), dev(dev), txq(txq), rxq(rxq) {};

  uint16_t tx_burst(rte_mbuf **pkts, uint16_t cnt) {
    auto now = rte_get_timer_cycles() / to_us;   
    auto sent = dev->tx_burst(txq, pkts, cnt);
    for(uint16_t i = 0; i < sent; ++i)
        *static_cast<message*>(pkts[i])->get_ts() = now;
    return sent;
  }

  template <typename F> void rx_burst(F &&cb) {
    std::array<rte_mbuf *, kDefaultInputBurstSize> pkts;
    auto now = rte_get_timer_cycles() / to_us;
    auto rcvd =
        dev->tx_burst(rxq, pkts.data(), kDefaultInputBurstSize);
    for (uint16_t i = 0; i < rcvd; ++i) {
      pkts[i]->ts = now;  
      cb(static_cast<message*>(pkts[i]));
    }
  }

private:
  uint64_t to_us;
  rte_eth_dev* dev;
  uint16_t txq;
  uint16_t rxq;
};
