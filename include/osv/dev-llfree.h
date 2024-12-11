#pragma once

#include "llfree.h"
#include "osv/types.h"

#define OSV_RESERVE_MEMORY 0ull * 1024 * 1024 * 1024

bool llfree_setup();
uint64_t llfree_alloc(llflags_t flags, size_t core);
bool llfree_free(uint64_t frame, size_t core);

extern u64 start_physical_region;
extern void *start_virtual_region;

extern u64 size_memory_region;
extern u64 curr_memory_region;
