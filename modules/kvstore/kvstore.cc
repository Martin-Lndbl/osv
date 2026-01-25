
#include <arpa/inet.h>
#include <bypass/fastt/connection.hh>
#include <bypass/fastt/iface.hh>
#include <bypass/fastt/message.hh>
#include <bypass/fastt/server.hh>
#include <bypass/fastt/util.hh>
#include <bypass/mem.hh>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <osv/lockless-queue.hh>
#include <tbb/concurrent_hash_map.h>
#include <thread>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include "kvstore_def.hh"
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
    response.id = request.id;
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
  tbb::concurrent_hash_map<int64_t, int64_t> hmap;
  using const_accessor = decltype(hmap)::const_accessor;
  using accessor = decltype(hmap)::accessor;
  kvstore_config config;

  void handle_get(kv_request request, kv_completion &response) {
    const_accessor lookup;
    if (hmap.find(lookup, request.key)) {
      response.val = lookup->second;
      lookup.release();
      response.reponse = response_t::SUCCESS;
    } else {
      response.reponse = response_t::FAILURE;
    }
  }

  void handle_put(kv_request request, kv_completion &response) {
    {
      accessor inserter;
      hmap.insert(inserter, request.key);
      inserter->second = request.val;
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

  worker_interface kv_if(store, allocator.get());
  auto network_thread = [&allocator](server_iface &server,
                                     worker_interface &worker) {
    poll_state<32> ps;
    uint16_t del_cnt = 0;
    while (true) {
      auto events = server.poll(ps);
      for (uint16_t i = 0; i < events; ++i) {
        message *msg;
        auto *con = ps.events[i];
        if (con->receive_message(&msg, 1)) {
          con->acknowledge_all();
          worker.rx_ring.push(
              {*static_cast<kv_packet<kv_request> *>(msg->data()), con});
          rte_pktmbuf_free(msg);
        }
      }
      while (!worker.tx_ring.empty() && del_cnt < 32) {
        auto resp = worker.tx_ring.front();
        auto *msg = allocator->alloc_message(sizeof(kv_packet<kv_completion>));
        memcpy(msg->data(), &resp, sizeof(resp));
        resp.second->send_message(msg, msg->len());
        ++del_cnt;
      }
      server.accept();
      server.flush();
    }
  };
  auto worker = std::thread([&]() {
    while (true)
      kv_if.process_packets();
  });
  network_thread(server, kv_if);
  worker.join();
  return 0;
}
