#include "bypass/fastt/client.hh"
#include "bypass/fastt/connection.hh"
#include "bypass/fastt/queue.hh"
#include "transport/slot.hh"
#include <cstddef>
#include <cstdint>

struct transaction_handle {
  transaction_slot *slot;
  transaction_handle(transaction_slot *slot = nullptr): slot(slot){}
};

class transaction_queue {
public:
  transaction_queue(std::size_t size) : queue(size) {}

  transaction_handle *enqueue(transaction_slot *slot) {
    if (queue.full())
      return nullptr;
    return queue.enqueue(transaction_handle(slot));
  }

  transaction_handle &front() { return *queue.front(); }

  void pop_front() {
    queue.pop_front();
    ++least_in_queue;
  }

  transaction_handle &operator[](std::size_t i) {
    return queue[i - least_in_queue];
  }

private:
  uint64_t least_in_queue = 0;
  queue_base<transaction_handle> queue;
};

struct transaction_proxy {
  transaction_queue &q;
  connection *con;
  transaction_handle *t;
  bool done = false;

  transaction_proxy(transaction_queue &q, connection *con,
                    transaction_handle *t)
      : q(q), con(con), t(t) {}

  decltype(t->slot->rx_if) &rx_if() { return t->slot->rx_if; }

  decltype(t->slot->tx_if) &tx_if() { return t->slot->tx_if; }

  void wait() {
    if (!t->slot->has_outstanding_messages()) {
      q.pop_front();
      done = true;
      return;
    }
    while (!t->slot->rx_if.has_incoming_messages())
      con->get_manager()->poll_single_connection(con);
  }

  bool finish() {
    if (!t->slot->has_outstanding_messages()) {
      q.pop_front();
      done = true;
    }
    return done;
  }

  bool completed() const { return done; }
};
