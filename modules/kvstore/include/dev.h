#pragma once

#include "arch/ena.h"
#include "arch/nic.h"
#include "util.h"
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <minidpdk/mem.hh>
#include <minidpdk/dev.hh>

class qpair {
public:
    static constexpr uint16_t kDefaultInputBurstSize = 64;
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
  static void unsent_cb(rte_mbuf** pkts, uint16_t unsent, void* userdata){
      static constexpr uint16_t kRetryTOus = 10;
      auto* qp = static_cast<qpair*>(userdata);
      auto now = rte_get_timer_cycles();
      auto end = now + get_ticks_us() * kRetryTOus;
      auto sent = 0u;
      do{
          sent += rte_eth_tx_burst(qp->port, qp->txq, pkts + sent, unsent - sent);
      }while(sent < unsent && rte_get_timer_cycles() < end);
      if(unsent - sent)
          rte_pktmbuf_free_bulk(pkts + sent, unsent - sent);
  }

  uint16_t port;
  uint16_t txq;
  uint16_t rxq;
  rte_eth_dev_tx_buffer *tx_buffer;

public:
  std::unique_ptr<nic> nic_arch;
};
