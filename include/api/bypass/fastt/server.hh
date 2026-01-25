#pragma once

#include "connection.hh"
#include "message.hh"
#include "util.hh"

#include <bypass/dev.hh>
#include <cstdint>
#include <memory>

class server_iface {
public:
  server_iface(rte_eth_dev *dev, uint16_t txq, uint16_t rxq,
               const con_config &scon_config,
               std::shared_ptr<message_allocator> pool)
      : scon_config(scon_config),
        manager(dev, txq, rxq, scon_config.ip, pool) {}

  bool send_message(connection *con, message *messages, uint16_t len);
  void flush();

  template <int N> uint16_t poll(poll_state<N> &events) {
    return manager.poll(events);
  }

  connection *accept();

private:
  con_config scon_config;
  connection_manager manager;
};
