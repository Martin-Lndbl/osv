#pragma once

#include "connection.hh"
#include "debug.hh"
#include "message.hh"
#include "util.hh"
#include <cstdint>
#include <memory>

class transaction_queue;

class client_iface {
  static constexpr uint16_t kdefaultBurstSize = 32;

public:
  client_iface(rte_eth_dev *eth_dev, uint16_t txq, uint16_t rxq,
               std::shared_ptr<message_allocator> pool,
               const con_config &scon_config)
      : scon_config(scon_config),
        manager(true, eth_dev, txq, rxq, scon_config.ip, pool) {}

  template <bool flush = true> bool probe_connection_setup_done(connection *con) {
    manager.fetch_from_device();  
    manager.flush();
    return con->active();
  }

  message *recv_message(connection *con);
  connection *open_connection(const con_config &target, rte_ether_addr &dmac);

  void flush() { manager.flush(); }

private:
  con_config scon_config;
  connection_manager manager;
};
