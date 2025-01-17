/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRTSEG_HH
#define VIRTSEG_HH

#include <osv/types.h>
#include "osv/mmu.hh"
#include <osv/mmu-defs.hh>
#include <string>

namespace mmu {

// Beginning of first segment
constexpr uintptr_t lower_vma_limit = 0x0;
// First byte after last segment
constexpr uintptr_t upper_vma_limit = main_mem_area_base;
// Upper limit of cores OSv can be initialized with
constexpr unsigned max_cores = 64;
// Cores cannot share a segment. There must therefore be at least as many segments as there are cores
constexpr u64 segment_size = (upper_vma_limit - lower_vma_limit) / max_cores;

struct vma_compare {
    bool operator ()(const vma& a, const vma& b) const {
        return a.addr() < b.addr();
    }
};

typedef boost::intrusive::set<vma,
                              bi::compare<vma_compare>,
                              bi::member_hook<vma,
                                              bi::set_member_hook<>,
                                              &vma::_vma_list_hook>,
                              bi::optimize_size<true>
                              > vma_list_base;

struct vma_list_type : vma_list_base {
    vma_list_type();
};

class vma_store
{
  public:
      // Threadsafe insertion of a vma into the store
      void vma_insert(vma* v);
      void vma_insert(linear_vma* v);

      // Threadsafe deletion of a vma into the store
      void vma_erase(vma& v);

      // Reserve size bytes of virtual memory
      uintptr_t reserve(vma* v, uintptr_t start, u64 size);

      vma_list_type::iterator
      find_intersecting_vma(uintptr_t addr);

      std::pair<vma_list_type::iterator, vma_list_type::iterator>
      find_intersecting_vmas(const addr_range& range);

      vma_list_type::iterator const end();

      u64 all_vmas_size();

      std::string procfs_maps();

      std::string sysfs_linear_maps();
  private:
};
}

#endif
