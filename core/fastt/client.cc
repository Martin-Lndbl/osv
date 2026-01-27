#include "bypass/fastt/client.hh"
#include "bypass/fastt/connection.hh"
#include "bypass/fastt/message.hh"
#include "bypass/fastt/util.hh"

connection *client_iface::open_connection(const con_config &target,
                                          rte_ether_addr &dmac) {
  manager.add_mac(target.ip, dmac);
  return manager.open_connection(scon_config, target);
}
