#ifndef KV_STORE_DEF
#define KV_STORE_DEF

#include <cstdint>

static constexpr uint16_t payload_offset = 0;
enum class packet_t: uint8_t{
    SINGLE = 0, BATCH = 1,
};

enum class request_t: uint8_t{
    GET = 0, PUT = 1, DELETE = 2,
};

enum class response_t: uint8_t{
    SUCCESS, FAILURE,
};

struct kv_operation_base{
    uint64_t id;
};

struct kv_packet_base{
    packet_t pt;
};

struct [[gnu::packed]] kv_request : public kv_operation_base{
    request_t op;
    int64_t key;
    int64_t val;
}; 

struct [[gnu::packed]] kv_completion : public kv_operation_base{
    response_t reponse;
    int64_t val;
};

template<typename T>
struct [[gnu::packed]] kv_packet : public kv_packet_base{
    T payload;
};

template<typename T>
struct [[gnu::packed]] kv_batch : public kv_packet_base{
    uint32_t elems;
    T elements[];
};

#endif // !KV_STORE_DEF
