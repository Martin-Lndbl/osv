#pragma once

#include "bypass/fastt/message.hh"
#include "bypass/fastt/timer.hh"
#include "bypass/fastt/transport/transport.hh"
#include "bypass/fastt/util.hh"
#include "bypass/fastt/timer.hh"
#include <chrono>
#include <cstdint>
#include <deque>

enum class slot_state {
  COMPLETED,
  RUNNING,
};

struct transaction_slot {
  static constexpr uint32_t kOutStandingMsg = 64;
  std::deque<message *> incoming;
  list_hook link;
  transport *transport_impl;
  uint64_t incoming_pkts = 0;
  osv_timer slot_timer;
  uint16_t tid = 0;
  slot_state state = slot_state::COMPLETED;
  bool is_client = false;
  bool has_outstanding_msgs = false;

  transaction_slot(uint16_t tid, transport *transport_impl, bool is_client, timer_manager<osv_timer>& tmanager)
      : transport_impl(transport_impl), slot_timer(&tmanager.set),
        tid(tid), is_client(is_client) {
  }

  static void timer_cb(osv_timer *timer, void *arg) {
    (void)timer;
    auto *slot = static_cast<transaction_slot *>(arg);
    slot->transport_impl->acknowledge();
    if (slot->incoming_pkts == 0)
      slot->transport_impl->probe_timeout(slot->tid);
    slot->rearm();
  }

  bool completed() { return state == slot_state::COMPLETED; }

  bool has_outstanding_messages() const {
    return has_outstanding_msgs || incoming.size() > 0;
  }

  void handle_incoming_server(message *msg, bool fini) {
    incoming.push_back(msg);
    ++incoming_pkts;
    has_outstanding_msgs = !fini;
  }

  void handle_incoming_client(message *msg, bool fini) {
    incoming.push_back(msg);
    ++incoming_pkts;
    if (fini) {
      transport_impl->acknowledge();
      stop_timer();
      state = slot_state::COMPLETED;
      has_outstanding_msgs = false;
    }
  }

  void rearm() {
    incoming_pkts = 0; 
    /* set timeout to 2ms/1ms */
    auto timeout =std::chrono::microseconds(1000 * (is_client ? 2 : 1));
    slot_timer.reset(timeout, timertype::SINGLE, timer_cb, this);

  }

  void stop_timer() {
    incoming_pkts = 0;
    slot_timer.stop();
  }

  void acknowledge() { transport_impl->acknowledge(); }

  void finish() {
    state = slot_state::COMPLETED;
    stop_timer();
    link.unlink();
    assert(!has_outstanding_msgs);
  }

  void update_execution() {
    assert(state == slot_state::COMPLETED);
    state = slot_state::RUNNING;
    has_outstanding_msgs = true;
    rearm();
  }

  bool update_execution_state(intrusive_list_t<transaction_slot> &head) {
    if (state == slot_state::COMPLETED) {
      assert(!link.is_linked());
      head.push_front(*this);
      state = slot_state::RUNNING;
      has_outstanding_msgs = true;
      rearm(); /*rearm timer*/
      return true;
    }
    return false;
  }

  struct {
    message *read() {
      if (slot->incoming.empty())
        return nullptr;
      auto *msg = slot->incoming.front();
      slot->incoming.pop_front();
      return msg;
    }

    bool has_incoming_messages() { return slot->incoming.size() > 0; }

    transaction_slot *slot;
  } rx_if{this};

  struct {
    bool send(message *msg, bool last = false) {
      return slot->transport_impl->send_pkt(msg, slot->tid, last);
    }
    transaction_slot *slot;
  } tx_if{this};
};
