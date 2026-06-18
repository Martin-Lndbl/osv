#pragma once
#include <cstdint>
#include <new>
#include "slab_allocator.h"
namespace kv{
static constexpr uint16_t payload_offset = 0;  
enum class packet_t : uint8_t {
  SINGLE = 0,
  BATCH = 1,
};

enum class request_t : uint8_t {
  GET = 0,
  PUT = 1,
  DELETE = 2,
  SCAN = 3,
};

enum class response_t : uint8_t {
  SUCCESS,
  FAILURE,
};

struct [[gnu::packed]] kv_packet_base {
  packet_t pt;
  uint64_t id;
};

struct [[gnu::packed]] kv_request {
  request_t op;
  int64_t key;
  int64_t val;
};

struct [[gnu::packed]] kv_scan {
  request_t op;
  int64_t low, high;
};

struct [[gnu::packed]] kv_completion {
  response_t reponse;
  int64_t key;
  uint64_t data_len;
  char data[];
};

template <typename T> struct [[gnu::packed]] kv_packet : public kv_packet_base {
  T payload;
};

template <typename T> struct [[gnu::packed]] kv_batch : public kv_packet_base {
  uint32_t elems;
  T elements[];
};

inline void create_kv_request(uint8_t *data, uint64_t id, int64_t key) {
  auto *kv_req = new (data) kv_packet<kv_request>;
  kv_req->pt = packet_t::SINGLE;
  kv_req->id = id;
  kv_req->payload.op = request_t::GET;
  kv_req->payload.key = key;
}

inline void create_kv_scan(uint8_t *data, uint64_t id, int64_t low,
                           int64_t high) {
  auto *kv_scn = new (data) kv_packet<kv_scan>;
  kv_scn->pt = packet_t::SINGLE;
  kv_scn->id = id;
  kv_scn->payload.op = request_t::SCAN;
  kv_scn->payload.low = low;
  kv_scn->payload.high = high;
}
}; // namespace kv

struct batch{
    batch(mbuf_ptr& buf): buf(std::move(buf)), off(0){}
    batch(): buf(mbuf_take_owner_ship(nullptr)){}

    template<typename T>
    T* next(uint32_t len){
        if(len + off > buf->sb->kMaxDataLen)
            return nullptr;
        return reinterpret_cast<T*>(buf->data<T>(off));
    }

    void finalize(uint32_t len){
        off += len;
    }

    mbuf_ptr&& release() &&{
        assert(off < buf->sb->kMaxDataLen);
        buf->data_len = off;
        return std::move(buf);
    }

    mbuf_ptr buf;
    uint32_t off;
};
