#pragma once

#include <bypass/mem.hh>
#include <cstdint>

namespace transport {
static constexpr uint16_t bs = 32;
template <typename T> struct tx_transport_buffer {
  std::vector<T> buf;
  std::size_t ptr;

  bool full() const { return ptr == buf.size(); }

  bool add(const T &elem) {
    if (full())
      return false;
    buf[ptr++] = elem;
    return true;
  }

  void tx_cb(uint16_t sent) {
    for (int i = sent, j = 0; i < ptr; ++i, ++j)
      buf[j] = buf[i];
    ptr = ptr - sent;
  }
  tx_transport_buffer(std::size_t size) : ptr(0), buf(size) {}
};

template <typename T> struct rx_transport_buffer {
  std::size_t elems, ptr;
  std::vector<T> buf;

  bool empty() const { return elems == ptr; }
  T next() { return buf[ptr++]; }

  void rx_cb(uint16_t rcvd) {
    elems = rcvd;
    ptr = 0;
  }

  rx_transport_buffer(std::size_t size) : elems(0), ptr(0), buf(size) {}
};

template <typename D> class transport {
public:
  transport() : tx_buf(bs), rx_buf(bs) {}
  virtual ~transport() = 0;

  std::pair<rte_mbuf*, char*> recv() {
    if (rx_buf.empty())
      static_cast<D &>(*this).rx_burst();
    if (rx_buf.empty())
      return {nullptr, nullptr};
    auto *pkt = rx_buf.next();
    return D::data(pkt);
  }

  bool send(rte_mbuf *buf, bool flush = false) {
    bool inserted = tx_buf.add(buf);
    if (tx_buf.full() || flush)
      static_cast<D &>(*this).tx_burst();
    return inserted;
  }

protected:
  tx_transport_buffer<rte_mbuf *> tx_buf;
  rx_transport_buffer<rte_mbuf *> rx_buf;
};
}; // namespace transport
