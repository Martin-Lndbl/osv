#pragma once

#include "llfree.h"
#include "osv/types.h"

#define OSV_RESERVE_MEMORY 32ull * 1024 * 1024 * 1024

extern u64 start_physical_region;
extern void *start_virtual_region;

extern u64 size_memory_region;
extern u64 curr_memory_region;
