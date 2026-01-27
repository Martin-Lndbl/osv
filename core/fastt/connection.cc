#include <bypass/fastt/connection.hh>
#include <bypass/fastt/debug.hh>
#include <bypass/fastt/message.hh>

#include <cstdint>

connection::connection(message_allocator *allocator, packet_if *pkt_if,
             const con_config &target, uint16_t sport,
             connection_manager *manager, bool is_client)
      : allocator(allocator), transport_impl(new transport(
                                  allocator, pkt_if, sport, target)),
        manager(manager) {
    slots.reserve(kMaxTransactionPerConnection);
    for (uint16_t i = 0; i < kMaxTransactionPerConnection; ++i){
      slots.emplace_back(i, transport_impl.get(), is_client, manager->con_timer_manager);
      if(is_client)
          free_slots.push_back(i);
    }
  }

void connection::process_pkt(rte_mbuf *pkt) {
  auto *msg = static_cast<message*>(pkt);  
  if (!transport_impl->process_pkt(msg))
    return;
} 

void connection::acknowledge_all(){
    transport_impl->acknowledge();
}

void connection::accept(){
    transport_impl->accept_connection();
}

void connection::open_connection(){
    transport_impl->open_connection();
}
