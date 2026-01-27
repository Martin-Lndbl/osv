#include "bypass/fastt/kv.hh"
#include "bypass/fastt/connection.hh"
#include "bypass/fastt/transaction.hh"

std::unique_ptr<transaction_proxy> kv_proxy::start_transaction(connection *con,
                                                           message *msg,
                                                           transaction_queue &q) {
  auto* slot = con->start_transaction();  
  if(!slot)
      return nullptr;
  auto* th = q.enqueue(slot);
  auto * pkt = rte_pktmbuf_mtod(msg, kv_packet_base*);
  pkt->pt = packet_t::SINGLE;
  return std::unique_ptr<transaction_proxy>(new transaction_proxy(q, con, th));
}

void kv_proxy::finish_transaction(transaction_proxy* proxy){
    proxy->con->finish_transaction(proxy->t->slot);
}
