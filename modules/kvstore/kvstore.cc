
#include <bypass/mem.hh>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <oneapi/tbb/concurrent_hash_map.h>

#include <yaml-cpp/yaml.h>

#include "kvstore_def.hh"

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

static void process_request(kvstore &store, kv_packet<kv_request> *request,
                            kv_packet<kv_completion> *response) {
  response->pt = packet_t::SINGLE;
  response->payload = store.serve_request(request->payload);
}

static void process_request(kvstore &store, kv_batch<kv_request> *request,
                            kv_batch<kv_completion> *response) {
  response->pt = packet_t::BATCH;
  response->elems = request->elems;
  for (auto i = 0u; i < response->elems; ++i)
    response->elements[i] = store.serve_request(request->elements[i]);
}

struct worker_interface {
  worker_interface(std::shared_ptr<kvstore> store, std::size_t burst)
      : requests(burst), responses(burst), store(std::move(store)) {}
  void process_packets(rte_mempool *pool) {
    auto space = std::min(responses.size(), requests.size());
    if (pool->alloc_bulk(responses.data(), requests.size()))
      return;

    for (auto i = 0u; i < space; ++i) {
      auto *base =
          reinterpret_cast<kv_packet_base *>(requests[i]->buf + payload_offset);
      auto *repsonse_base = reinterpret_cast<kv_packet_base *>(
          responses[i]->buf + payload_offset);
      switch (base->pt) {
      case packet_t::SINGLE:
        process_request(*store, static_cast<kv_packet<kv_request> *>(base),
                        static_cast<kv_packet<kv_completion> *>(repsonse_base));
        break;
      case packet_t::BATCH:
        process_request(*store, static_cast<kv_batch<kv_request> *>(base),
                        static_cast<kv_batch<kv_completion> *>(repsonse_base));
        break;
      }
    }
  }
  std::vector<rte_mbuf *> requests, responses;
  std::shared_ptr<kvstore> store;
};

int main() {
  static constexpr uint16_t burst = 32;
  std::string addr;
  uint16_t pmin, pmax;
  uint32_t initial_size;
  std::cin >> addr >> pmin >> pmax >> initial_size;
  std::shared_ptr<kvstore> store = std::make_shared<kvstore>(initial_size);
  std::size_t tidx = 0;
  worker_interface kv_if(store, burst);
  auto worker = [kv_if = std::move(kv_if)] () mutable {


  };
  worker();
  return 0;
}
