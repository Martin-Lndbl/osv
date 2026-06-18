#pragma once

#include "arch/ena.h"
#include "arch/nic.h"
#include "util.h"
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <minidpdk/mem.hh>
#include <minidpdk/dev.hh>

class qpair {
public:
    static constexpr uint16_t kDefaultInputBurstSize = 256;
  qpair(uint16_t port, uint16_t txq, uint16_t rxq)
      : port(port), txq(txq), rxq(rxq),
        tx_buffer(static_cast<rte_eth_dev_tx_buffer *>(
            malloc(rte_eth_dev_tx_buffer::memsize(kDefaultInputBurstSize)))),
        nic_arch(std::make_unique<ena::ena>()) {
    assert(tx_buffer);
    rte_eth_tx_buffer_init(tx_buffer, kDefaultInputBurstSize);
  };

  ~qpair() {
      rte_pktmbuf_free_bulk(tx_buffer->pkts, tx_buffer->length);
      rte_free(tx_buffer); 
  }

  void enqueue_pkt(rte_mbuf *pkt) {
    rte_eth_tx_buffer(port, txq, tx_buffer, pkt);
  }

  template <unsigned N> void rx_burst(packet_vector<rte_mbuf*, N> &vec) {
    auto rcvd = rte_eth_rx_burst(port, rxq, vec.pkts.data(), vec.pkts.size());
    vec.i = rcvd;
  }

  void flush() { rte_eth_tx_buffer_flush(port, txq, tx_buffer); }

  uint16_t get_rx_qid() const{
      return rxq;
  }

private:
  uint16_t port;
  uint16_t txq;
  uint16_t rxq;
  rte_eth_dev_tx_buffer *tx_buffer;

public:
  std::unique_ptr<nic> nic_arch;
};
