
#include <arpa/inet.h>
#include <bypass/fastt/connection.hh>
#include <bypass/fastt/iface.hh>
#include <bypass/fastt/kv.hh>
#include <bypass/fastt/message.hh>
#include <bypass/fastt/server.hh>
#include <bypass/fastt/transport/slot.hh>
#include <bypass/fastt/util.hh>
#include <bypass/mem.hh>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <osv/lockless-queue.hh>
#include <unistd.h>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

#include "lockfree/ring.hh"

#define PUN(target, elem)                                                      \
  do {                                                                         \
    std::memcpy(target, &elem, sizeof(elem));                                  \
  } while (0)

struct port_range {
  std::pair<uint16_t, uint16_t> port_range;
  bool operator&(uint16_t port) {
    return port >= port_range.first && port <= port_range.second;
  }
};

struct kvstore_config {
  uint32_t sip;
  port_range prange;
};
template <typename T> struct span_view {
  template <typename C>
  span_view(C &container) : ptr_(container.data()), size_(container.size()) {}
  T *ptr_;
  std::size_t size_;

  std::size_t size() const { return size_; }
  T *data() { return ptr_; }
  T &operator[](std::size_t i) { return ptr_[i]; }
};

class kvstore {
public:
  kvstore(std::size_t size) : hmap(size) {}
  kv_completion serve_request(kv_request request) {
    kv_completion response;
    switch (request.op) {
    case request_t::GET:
      handle_get(request, response);
      break;
    case request_t::PUT:
      handle_put(request, response);
    case request_t::DELETE:
      handle_put(request, response);
      break;
    }
    return response;
  }

private:
  std::unordered_map<int64_t, int64_t> hmap;
  kvstore_config config;

  void handle_get(kv_request request, kv_completion &response) {
      auto it = hmap.find(request.key);
    if (hmap.find(request.key) != hmap.end()) {
      response.val = it->second;
      response.reponse = response_t::SUCCESS;
    } else {
      response.reponse = response_t::FAILURE;
    }
  }

  void handle_put(kv_request request, kv_completion &response) {
    {
      hmap[request.key] = request.val;  
    }

    response.reponse = response_t::SUCCESS;
    response.val = 0;
  }

  void handle_delete(kv_request request, kv_completion &response) {
    if (hmap.erase(request.key))
      response.reponse = response_t::SUCCESS;
    else
      response.reponse = response_t::FAILURE;
  }
};

static void process_request(kvstore &store,
                            const kv_packet<kv_request> *request,
                            kv_packet<kv_completion> *response) {
  response->pt = packet_t::SINGLE;
  response->payload = store.serve_request(request->payload);
}

struct worker_interface {
  template <typename T> using queue_t = ring_spsc<T, unsigned, 64u>;
  worker_interface(std::shared_ptr<kvstore> store, message_allocator *allocator)
      : allocator(allocator), store(std::move(store)) {}

  void process_packets() {
    while (rx_ring.empty())
      pause();
    auto &req = rx_ring.front();
    kv_packet<kv_completion> resp;
    switch (req.first.pt) {
    case packet_t::SINGLE:
      process_request(*store, &req.first, &resp);
      break;
    default:
      return;
    }
    tx_ring.push({resp, req.second});
  }
  message_allocator *allocator;
  std::shared_ptr<kvstore> store;
  queue_t<std::pair<kv_packet<kv_request>, connection *>> rx_ring;
  queue_t<std::pair<kv_packet<kv_completion>, connection *>> tx_ring;
};

int main() {
  std::string addr;
  std::cin >> addr;
  uint32_t sip = inet_addr(addr.c_str());
  auto ifc = iface::configure_port(0, 1, 1);
  std::shared_ptr<kvstore> store = std::make_shared<kvstore>(1024);
  auto dev = ifc->get_slice(0);
  std::shared_ptr<message_allocator> allocator =
      std::make_shared<message_allocator>("pool", 8095);
  server_iface server(ifc->eth_dev, 1, 1, con_config{sip, 1000}, allocator);

  server.poll([&](transaction_slot &slot) {
    auto *resp = allocator->alloc_message(sizeof(kv_packet<kv_completion>));
    auto *req = slot.rx_if.read();
    process_request(*store, rte_pktmbuf_mtod(req, kv_packet<kv_request> *),
                    rte_pktmbuf_mtod(resp, kv_packet<kv_completion> *));
    slot.tx_if.send(resp, true);
  });
  return 0;
}
