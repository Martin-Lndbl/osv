/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "osv/prio.hh"
#include "osv/rwlock.h"
#include <numeric>
#include <osv/vma_store.hh>
#include <algorithm>
#include <set>

namespace mmu{

struct vma_range_compare {
    bool operator()(const vma_range& a, const vma_range& b) const {
        return a.start() < b.start();
    }
};

struct linear_vma_compare {
    bool operator()(const linear_vma* a, const linear_vma* b) const {
        return a->_virt_addr < b->_virt_addr;
    }
};

//Set of all vma ranges - both linear and non-linear ones
__attribute__((init_priority((int)init_prio::vma_range_set)))
std::set<vma_range, vma_range_compare> vma_range_set;
rwlock_t vma_range_set_mutex;

// Set of all linear_mapped vmas
__attribute__((init_priority((int)init_prio::linear_vma_set)))
std::set<linear_vma*, linear_vma_compare> linear_vma_set;
rwlock_t linear_vma_set_mutex;

// Set of all vmas
__attribute__((init_priority((int)init_prio::vma_list)))
vma_list_type vma_list;

// protects vma list and page table modifications.
// anything that may add, remove, split vma, zaps pte or changes pte permission
// should hold the lock for write
rwlock_t vma_list_mutex;

vma_list_type::vma_list_type(){
   auto lower_edge = new anon_vma(addr_range(lower_vma_limit, lower_vma_limit), 0, 0);
    insert(*lower_edge);
    auto upper_edge = new anon_vma(addr_range(upper_vma_limit, upper_vma_limit), 0, 0);
    insert(*upper_edge);

    WITH_LOCK(vma_range_set_mutex.for_write()) {
        vma_range_set.insert(vma_range(lower_edge));
        vma_range_set.insert(vma_range(upper_edge));
    }

}

// So that we don't need to create a vma (with size, permission and alot of
// other irrelevant data) just to find an address in the vma list, we have
// the following addr_compare, which compares exactly like vma_compare does,
// except that it takes a bare uintptr_t instead of a vma.
struct addr_compare {
    bool operator()(const vma& x, uintptr_t y) const { return x.start() < y; }
    bool operator()(uintptr_t x, const vma& y) const { return x < y.start(); }
};


struct vma_range_addr_compare {
    bool operator()(const vma_range& x, uintptr_t y) const { return x.start() < y; }
    bool operator()(uintptr_t x, const vma_range& y) const { return x < y.start(); }
};

u64 vma_store::all_vmas_size() {
    SCOPE_LOCK(vma_list_mutex.for_read());
    return std::accumulate(vma_list.begin(), vma_list.end(), size_t(0), [](size_t s, vma& v) { return s + v.size(); });
}

vma_list_type::iterator const vma_store::end(){
  return vma_list.end();
}

// Find the single (if any) vma which contains the given address.
// The complexity is logarithmic in the number of vmas in vma_list.
vma_list_type::iterator vma_store::find_intersecting_vma(uintptr_t addr)
{
    SCOPE_LOCK(vma_list_mutex.for_read());
    auto vma = vma_list.lower_bound(addr, addr_compare());
    if (vma->start() == addr) {
        return vma;
    }
    // Otherwise, vma->start() > addr, so we need to check the previous vma
    --vma;
    if (addr >= vma->start() && addr < vma->end()) {
        return vma;
    } else {
        return vma_list.end();
    }
}

// Find the list of vmas which intersect a given address range. Because the
// vmas are sorted in vma_list, the result is a consecutive slice of vma_list,
// [first, second), between the first returned iterator (inclusive), and the
// second returned iterator (not inclusive).
// The complexity is logarithmic in the number of vmas in vma_list.
std::pair<vma_list_type::iterator, vma_list_type::iterator>
vma_store::find_intersecting_vmas(const addr_range& r)
{
    SCOPE_LOCK(vma_list_mutex.for_read());
    if (r.end() <= r.start()) { // empty range, so nothing matches
        return {vma_list.end(), vma_list.end()};
    }
    auto start = vma_list.lower_bound(r.start(), addr_compare());
    if (start->start() > r.start()) {
        // The previous vma might also intersect with our range if it ends
        // after our range's start.
        auto prev = std::prev(start);
        if (prev->end() > r.start()) {
            start = prev;
        }
    }
    // If the start vma is actually beyond the end of the search range,
    // there is no intersection.
    if (start->start() >= r.end()) {
        return {vma_list.end(), vma_list.end()};
    }
    // end is the first vma starting >= r.end(), so any previous vma (after
    // start) surely started < r.end() so is part of the intersection.
    auto end = vma_list.lower_bound(r.end(), addr_compare());
    return {start, end};
}

uintptr_t vma_store::reserve(vma* v, uintptr_t start, u64 size){
    bool small = size < huge_page_size;
    uintptr_t good_enough = 0;

    SCOPE_LOCK(vma_range_set_mutex.for_write());
    //Find first vma range which starts before the start parameter or is the 1st one
    auto p = std::lower_bound(vma_range_set.begin(), vma_range_set.end(), start, vma_range_addr_compare());
    if (p != vma_range_set.begin()) {
        --p;
    }
    auto n = std::next(p);
    while (n->start() <= upper_vma_limit) { //we only go up to the upper mmap vma limit
        //See if desired hole fits between p and n vmas
        if (start >= p->end() && start + size <= n->start()) {
            v->set(start, start+size);
            vma_range_set.insert(v);
            return start;
        }
        //See if shifting start to the end of p makes desired hole fit between p and n
        if (p->end() >= start && n->start() - p->end() >= size) {
            good_enough = p->end();
            if (small) {
                v->set(good_enough, good_enough+size);
                vma_range_set.insert(v);
                return good_enough;
            }
            //See if huge hole fits between p and n
            if (n->start() - align_up(good_enough, huge_page_size) >= size) {
                uintptr_t res = align_up(good_enough, huge_page_size);
                v->set(res, res+size);
                vma_range_set.insert(v);
                return res;
            }
        }
        //If nothing worked move next in the list
        p = n;
        ++n;
    }
    if (good_enough) {
        v->set(good_enough, good_enough+size);
        vma_range_set.insert(v);
        return good_enough;
    }
    throw make_error(ENOMEM);
}

void vma_store::vma_insert(vma *v){
    WITH_LOCK(vma_list_mutex.for_write()){
        vma_list.insert(*v);
    }
    bool insert_in_range = true;
    // Check if already in vma_range_set
    WITH_LOCK(vma_range_set_mutex.for_read()){
      insert_in_range = vma_range_set.find(v) == vma_range_set.end();
    }
    if(insert_in_range){
        WITH_LOCK(vma_range_set_mutex.for_write()){
            vma_range_set.insert(v);
        }
    }
}

void vma_store::vma_insert(linear_vma *v){
    WITH_LOCK(linear_vma_set_mutex.for_write()){
        linear_vma_set.insert(v);
    }
    bool insert_in_range = true;
    // Check if already in vma_range_set
    WITH_LOCK(vma_range_set_mutex.for_read()){
      insert_in_range = vma_range_set.find(v) == vma_range_set.end();
    }
    if(insert_in_range){
        WITH_LOCK(vma_range_set_mutex.for_write()){
            vma_range_set.insert(v);
        }
    }
}

void vma_store::vma_erase(vma& v){
    WITH_LOCK(vma_list_mutex.for_write()){
        vma_list.erase(v);
    }
    WITH_LOCK(vma_range_set_mutex.for_write()){
        vma_range_set.erase(vma_range(&v));
    }
    delete &v;
}


std::string vma_store::sysfs_linear_maps() {
    std::string output;
    WITH_LOCK(linear_vma_set_mutex.for_read()) {
        for(auto *vma : linear_vma_set) {
            char mattr = vma->_mem_attr == mmu::mattr::normal ? 'n' : 'd';
            output += osv::sprintf("%18p %18p %12x rwxp %c %s\n",
                vma->_virt_addr, (void*)vma->_phys_addr, vma->_size, mattr, vma->_name.c_str());
        }
    }
    return output;
}

std::string vma_store::procfs_maps()
{
    std::string output;
    WITH_LOCK(vma_list_mutex.for_read()) {
        for (auto& vma : vma_list) {
            char read    = vma.perm() & perm_read  ? 'r' : '-';
            char write   = vma.perm() & perm_write ? 'w' : '-';
            char execute = vma.perm() & perm_exec  ? 'x' : '-';
            char priv    = 'p';
            output += osv::sprintf("%lx-%lx %c%c%c%c ", vma.start(), vma.end(), read, write, execute, priv);
            if (vma.flags() & mmap_file) {
                const file_vma &f_vma = static_cast<file_vma&>(vma);
                unsigned dev_id_major = major(f_vma.file_dev_id());
                unsigned dev_id_minor = minor(f_vma.file_dev_id());
                output += osv::sprintf("%08x %02x:%02x %ld %s\n", f_vma.offset(), dev_id_major, dev_id_minor, f_vma.file_inode(), f_vma.file()->f_dentry->d_path);
            } else {
                output += osv::sprintf("00000000 00:00 0\n");
            }
        }
    }
    return output;
}

}
