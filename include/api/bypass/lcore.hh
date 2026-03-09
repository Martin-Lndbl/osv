#pragma once

#include <cstdint>
#include <osv/sched.hh>
#include <vector>

#define SKIP_MAIN false
#define CALL_MAIN true

struct lcore_container{
    std::vector<sched::thread*> threads;
    uint16_t ncpus;
    static lcore_container lcores;
    static void init(uint16_t cnt){
        lcores.ncpus = cnt;
        lcores.threads.resize(cnt);
        sched::current()->pin(sched::cpus[0]);
        lcores.threads.front() = sched::current();
    }
};

inline uint16_t lcore_id(){
    return sched::current_cpu->id;
}

inline uint16_t lcore_count(){
    return lcore_container::lcores.ncpus;
}

inline uint16_t lcore_index(uint16_t id){
    (void)id;
    return lcore_id();
}


inline void mp_remote_launch(int(*lcore_fn)(void*), void* arg, bool call_main){
    auto ncpu = sched::max_cpus;
    for(auto i = 1u; i < ncpu; ++i)
        lcore_container::lcores.threads[i] = sched::thread::make([lcore_fn, arg]{
                    lcore_fn(arg);
                    }, sched::thread::attr().pin(sched::cpus[i]));
    if(call_main)
        lcore_fn(arg);
 }


inline void mp_wait_all(){
    for(auto i = 1u; i < sched::max_cpus; ++i)
        lcore_container::lcores.threads[i]->join();
}

#define rte_lcore_id lcore_id
#define rte_lcore_count lcore_count
#define rte_lcore_index lcore_index
#define rte_eal_mp_remote_launch mp_remote_launch
#define rte_eal_mp_wait_lcore mp_wait_all
#define RTE_LCORE_FOREACH(id) for(id = 0; id < lcore_container::lcores.ncpus; ++id)
#define rte_lcore_to_socket_id

