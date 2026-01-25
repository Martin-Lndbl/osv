#include <bypass/fastt/connection.hh>
#include <bypass/fastt/log.hh>
#include <bypass/fastt/message.hh>

#include <cstdint>

void connection::process_pkt(rte_mbuf *pkt) {
  auto *msg = static_cast<message*>(pkt);  
  if (!transport_impl->process_pkt(msg))
    return;
} 

uint16_t connection::receive_message(message** msgs, uint16_t cnt){
    return transport_impl->receive_messages(msgs, cnt);
}

void connection::accept(){
    transport_impl->accept_connection();
}

bool connection::send_message(message *pkt, uint16_t len) {
  pkt->set_size(len);
  FASTT_LOG_DEBUG("Sent pkt of len %u\n", len);
  return transport_impl->send_pkt(pkt);
}

void connection::acknowledge_all() { transport_impl->send_acks(); }

void connection::open_connection(){
    transport_impl->open_connection();
}

bool connection::has_ready_message() const {
  return transport_impl->poll();
}
