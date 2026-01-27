#pragma once

#include "connection.hh"
#include "message.hh"
#include "util.hh"

#include <bypass/dev.hh>
#include <cstdint>
#include <memory>

class server_iface {
public:
  server_iface(rte_eth_dev* dev, uint16_t txq, uint16_t rxq,
               const con_config &scon_config,
               std::shared_ptr<message_allocator> pool)
      : scon_config(scon_config),
        manager(false, dev, txq, rxq, scon_config.ip, pool) {}

  void complete() { manager.flush(); };

  template<typename F>
   void poll(F&& f){
       manager.poll(f);
   }   
private:
  con_config scon_config;
  connection_manager manager;
};
