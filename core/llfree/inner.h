#pragma once

#include "local.h"
#include "lower.h"
#include "platform.h"
#include "tree.h"
#include "utils.h"
#include <osv/llfree.h>

/// The llfree metadata
typedef struct __attribute__((aligned(LLFREE_CACHE_SIZE))) llfree {
  /// Lower allocator
  lower_t lower;
  /// Cpu-local data
  local_t *local;
  /// Length of local
  size_t cores;
  /// Array of tree entries
  _Atomic(tree_t) *trees;
  size_t trees_len;
} llfree_t;
