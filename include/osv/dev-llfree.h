#pragma once

#include "llfree.h"
#include "osv/types.h"

#define OSV_RESERVE_MEMORY 2ull * 1024 * 1024 * 1024
#define DOCUMENT_RESERVATION

#define VERBOSE

extern u64 start_physical_region;
extern void *start_virtual_region;

extern u64 size_memory_region;
extern u64 curr_memory_region;
