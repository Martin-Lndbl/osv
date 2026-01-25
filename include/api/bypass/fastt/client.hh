#pragma once

#include "connection.hh"
#include "log.hh"
#include "message.hh"
#include "util.hh"
#include <bypass/dev.hh>
#include <cstdint>
#include <memory>
#include <bypass/net.hh>

class client_iface {
  static constexpr uint16_t kdefaultBurstSize = 32;

public:
  client_iface(rte_eth_dev* eth_dev, uint16_t txq, uint16_t rxq,
               std::shared_ptr<message_allocator> pool,
               const con_config &scon_config)
      : scon_config(scon_config),
        manager(eth_dev, txq, rxq, scon_config.ip, pool) {}

  template <bool flush = false>
  bool send_message(connection *con, message *message, uint16_t len) {
    FASTT_LOG_DEBUG("Sending new message with len %u\n", len);
    bool sent = con->send_message(message, len);
    if constexpr (flush)
      manager.flush();
    return sent;
  }

  template <bool flush = true> bool probe_connection_setup_done(connection *con) {
    recv_message(con);
    if constexpr (flush)
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
