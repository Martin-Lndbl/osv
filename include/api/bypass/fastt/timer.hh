#pragma once
#include "osv/clock.hh"
#include "osv/timer-set.hh"
#include <boost/intrusive/list_hook.hpp>
#include <chrono>

enum class timertype { SINGLE, PERIODICAL };

template <typename T, typename Timepoint, typename Cb> struct timer {
  using impl_t = T;
  using tick_t = std::chrono::microseconds;
  using timer_cb_t = Cb;
  using timeout = Timepoint;

  Timepoint timeout_tp;
  tick_t ticks;

  int reset(tick_t to, timer_cb_t cb, void *timer_arg) {
    timeout_tp = to;
    return static_cast<T *>(this)->reset(to, cb, timer_arg);
  }

  int stop() { return static_cast<T *>(this)->stop(); }

  Timepoint get_timeout() const { return timeout_tp; }
};

struct osv_timer : timer<osv_timer, osv::clock::uptime::time_point,
                         void (*)(osv_timer *, void *)> {
  bi::list_member_hook<> link;
  timer_cb_t cb;
  void *arg;
  timertype type;
  timer_set<osv_timer, &osv_timer::link, osv::clock::uptime> *set;

  osv_timer(timer_set<osv_timer, &osv_timer::link, osv::clock::uptime> *set)
      : set(set) {}

  void reset(std::chrono::microseconds to, timertype type_arg, timer_cb_t tcb,
             void *timer_arg) {
    timeout_tp = to + osv::clock::uptime::now();
    ticks = to;
    type = type_arg;
    cb = tcb;
    arg = timer_arg;
    set->insert(*this);
  }

  void stop() { 
      if(link.is_linked())
      set->remove(*this); 
  }
};

template <typename> struct timer_manager;

template <> struct timer_manager<osv_timer> {
  using timer_t = osv_timer;
  timer_set<timer_t, &timer_t::link, osv::clock::uptime> set;

  int manage() {
    set.expire(osv::clock::uptime::now());
    timer_t *t;
    while ((t = set.pop_expired())) {
      t->cb(t, t->arg);
      if (t->type == timertype::PERIODICAL)
        t->reset(t->ticks, t->type, t->cb, t->arg);
    }
    return 0;
  }
};
