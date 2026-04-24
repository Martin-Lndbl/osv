#include "bench.h"
#include "connection.h"
#include "dpdk/allocator.h"
#include "iface.h"
#include "kv_protocol.h"
#include "server.h"
#include "sgl.h"
#include "slab_allocator.h"
#include "task/async.h"
#include "task/task.h"
#include <arpa/inet.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <getopt.h>
#include <memory>
#include <minidpdk/dev.hh>
#include <minidpdk/lcore.hh>
#include <minidpdk/mem.hh>
#include <minidpdk/net.hh>
#include <minidpdk/slab.hh>
#include <minidpdk/time.hh>
#include <signal.h>
#include <utility>

struct netconfig {
  uint32_t sip;
};

struct lcore_server_adapter {
  std::unique_ptr<server_iface> iface;
  std::shared_ptr<dpdk_allocator> allocator;
};

static bench::storage store;
static unsigned len = 8;
static unsigned store_size = bench::kStoreSize;

template <typename T>
static T *alloc_or_get(sgl &rsgl, slab_allocator &alloc, uint32_t len,
                       batch &btch) {
  auto *completion = btch.next<T>(len);
  if (!completion) {
    rsgl.add_segment_safe(std::move(btch).release());
    auto seg = alloc.alloc_default_safe(0);
    btch = batch(seg);
    completion = btch.next<T>(len);
  }
  return completion;
}
#ifdef NBATCH
static void serve(sgl &resp_sgl, slab_allocator &alloc,
                  kv::kv_packet<kv::kv_request> *packet) {
  kv::kv_packet<kv::kv_completion> *completion;
  auto key = packet->payload.key;
  auto it = store.find(key);
  if (it == store.end()) {
    auto seg = alloc.alloc_default_safe(sizeof(*completion));
    completion = seg->data<kv::kv_packet<kv::kv_completion>>();
    completion->payload.reponse = kv::response_t::FAILURE;
    completion->payload.data_len = 0;
    resp_sgl.add_segment_safe(std::move(seg));
  } else {
    auto seg =
        alloc.alloc_default_safe(sizeof(*completion) + it->second.size());
    completion = seg->data<kv::kv_packet<kv::kv_completion>>();
    completion->payload.reponse = kv::response_t::SUCCESS;
    std::memcpy(completion->payload.data, it->second.data(), it->second.size());
    completion->payload.data_len = it->second.size();
    resp_sgl.add_segment_safe(std::move(seg));
  }
  completion->id = packet->id;
  completion->pt = packet->pt;
  completion->payload.key = packet->payload.key;
}
#else

static void serve(batch &btch, sgl &resp_sgl, slab_allocator &alloc,
                  kv::kv_packet<kv::kv_request> *packet) {
  kv::kv_packet<kv::kv_completion> *completion;
  auto key = packet->payload.key;
  auto it = store.find(key);
  if (it == store.end()) {
    completion = alloc_or_get<kv::kv_packet<kv::kv_completion>>(
        resp_sgl, alloc, sizeof(*completion), btch);
    completion->payload.reponse = kv::response_t::FAILURE;
    completion->payload.data_len = 0;
  } else {
    completion = alloc_or_get<kv::kv_packet<kv::kv_completion>>(
        resp_sgl, alloc, sizeof(*completion) + it->second.size(), btch);
    completion->payload.reponse = kv::response_t::SUCCESS;
    std::memcpy(completion->payload.data, it->second.data(), it->second.size());
    completion->payload.data_len = it->second.size();
  }
  completion->id = packet->id;
  completion->pt = packet->pt;
  completion->payload.key = packet->payload.key;
  btch.finalize(completion->payload.data_len +
                sizeof(kv::kv_packet<kv::kv_completion>));
}
#endif

static netconfig parse_cmdline(int argc, char *argv[]) {
  int opt, option_index;
  netconfig conf{};
  static const struct option long_options[] = {
      {"sip", required_argument, 0, 0},
      {"len", required_argument, 0, 0},
      {"size", required_argument, 0, 0},
      {0, 0, 0, 0}};
  while ((opt = getopt_long(argc, argv, "", long_options, &option_index)) !=
         -1) {
    switch (option_index) {
    case 0:
      conf.sip = inet_addr(optarg);
      break;
    case 1:
      len = atoi(optarg);
      break;
    case 2:
      store_size = std::stol(optarg);
      break;
    }
  }
  return conf;
}

static std::atomic<int> terminate = 0;

static void handler(int sig) {
  (void)sig;
  terminate = 1;
}

#ifdef NBATCH
int lcore_server_fun(void *arg) {
    
  __rte_setup_memory(0);
  auto myid = rte_lcore_index(rte_lcore_id());
  auto &adapters = *static_cast<std::vector<lcore_server_adapter> *>(arg);
  auto *server = adapters[myid].iface.get();
  server->register_service(
      2, [](server_iface &iface, connection &con) -> concurrency::task {
        sgl ssgl;
        auto &slab = *iface.get_alloc();
        while (true) {
          sgl rsgl{};
          auto sz = co_await recv(iface.get_scheduler(), con, rsgl);
          if (sz == 0) 
            co_return;
          
          assert(ssgl.empty());
          for (auto &seg : rsgl) {
            assert(seg.data_len == sizeof(kv::kv_packet<kv::kv_request>));
            serve(ssgl, slab, seg.data<kv::kv_packet<kv::kv_request>>());
          }
          ssize_t to_send = ssgl.size;
          auto sent =
              co_await send(iface.get_scheduler(), con, std::move(ssgl));
          if (sent == 0)
            co_return;
          assert(sent == to_send);
        }
      });

  while (!terminate)
    server->run();
  server->complete();

  return 0;
}
#else
int lcore_server_fun(void *arg) {
  auto myid = rte_lcore_index(rte_lcore_id());
  auto &adapters = *static_cast<std::vector<lcore_server_adapter> *>(arg);
  auto *server = adapters[myid].iface.get();
  server->register_service(
      2, [](server_iface &iface, connection &con) -> concurrency::task {
        sgl ssgl;
        auto &slab = *iface.get_alloc();
        while (true) {
          sgl rsgl{};
          auto sz = co_await recv(iface.get_scheduler(), con, rsgl);
          if (sz == 0)
            co_return;
          auto seg = slab.alloc_default_safe(0);
          batch btch(seg);
          assert(ssgl.empty());
          for (auto &seg : rsgl) {
            parse_mbuf<kv::kv_packet<kv::kv_request>>(
                seg, [&](kv::kv_packet<kv::kv_request> *req) {
                  serve(btch, ssgl, slab, req);
                  return sizeof(kv::kv_packet<kv::kv_request>);
                });
          }
          ssgl.add_segment_safe(std::move(btch).release());
          ssize_t to_send = ssgl.size;
          auto sent =
              co_await send(iface.get_scheduler(), con, std::move(ssgl));
          if (sent == 0)
            co_return;
          assert(sent == to_send);
        }
      });

  while (!terminate)
    server->run();
  server->complete();

  return 0;
}
#endif

static void free_cb(void*, void* mb){
    mbuf_free(static_cast<mbuf*>(mb));
}

static void init(minidpdk::mbuf **pkts, uint16_t n, void* priv){
    auto *sb = static_cast<slab_allocator*>(priv);
    for(unsigned i = 0; i < n; ++i){
        auto *app_mbuf = sb->alloc_default(0);
        auto *ext = get_new_backend_data<minidpdk::rte_mbuf_ext_shared_info>(app_mbuf);
        ext->refcnt = 1;
        ext->fcb_opaque = app_mbuf;
        ext->free_cb = free_cb;
        rte_pktmbuf_attach_extbuf(pkts[i], app_mbuf->data<uint8_t*>(), sb->get_iova(app_mbuf, 0), sb->kMaxDataRoom, ext); 
    }
}

int run(netconfig &conf) {
  bench::prepare(store, len);

  if (fastt::init())
    return -1;

  auto nthreads = rte_lcore_count();
  unsigned i = 0;
  uint16_t lcore_id;
  std::vector<std::shared_ptr<dpdk_allocator>> allocators;
  std::vector<std::unique_ptr<slab_allocator>> sbs;
  std::vector<uint16_t> lcore_ids;
  allocators.reserve(nthreads);
  lcore_ids.reserve(nthreads);
  sbs.reserve(nthreads);
  RTE_LCORE_FOREACH(lcore_id) {
    sbs.emplace_back(std::make_unique<slab_allocator>());
    allocators.emplace_back(
        dpdk_allocator::create(("mpool" + std::to_string(i)).c_str(), 4095, sbs.back().get(), init));
    lcore_ids.push_back(lcore_id);
    ++i;
  }
  auto ifc =
      iface::configure_port(0, nthreads, nthreads, allocators, lcore_ids);
  if (!ifc)
    return -1;

  std::vector<lcore_server_adapter> adapters(nthreads);
  i = 0;
  RTE_LCORE_FOREACH(lcore_id) {
    auto &adapter = adapters[i];
    auto [port, txq, rxq] = ifc->get_slice(i);
    adapter.allocator = std::move(allocators[i]);
    adapter.iface = std::make_unique<server_iface>(
        port, txq, rxq, conf.sip, adapter.allocator, sbs[i], rte_lcore_count());
    ++i;
  }

  rte_eal_mp_remote_launch(lcore_server_fun, &adapters, CALL_MAIN);
  rte_eal_mp_wait_lcore();
  ifc->stop();
  return 0;
}

int main(int argc, char *argv[]) {
  lcore_container::init(1);
  struct sigaction sa = {};
  sa.sa_handler = handler;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  auto conf = parse_cmdline(argc, argv);
  run(conf);
  return 0;
}
