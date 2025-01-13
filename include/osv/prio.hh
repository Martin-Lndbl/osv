/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef PRIO_HH_
#define  PRIO_HH_

namespace init_prio {
enum {
    dtb = 101,
    console,
    sort,
    cpus,
    page_allocator,
    pt_root,
    vma_range_set,
    linear_vma_set,
    mempool,
    routecache,
    pagecache,
    threadlist,
    pthread,
    notifiers,
    psci,
    acpi,
    vma_list,
    virt_segment_array,
    reclaimer,
    sched,
    clock,
    hpet,
    tracepoint_base,
    malloc_pools,
    idt,
};
}

#endif
