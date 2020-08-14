#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <mutex.h>
#include <perfcounter.h>
#include <seqread.h>
#include <stdint.h>
#include <stdio.h>

#define PRINT_DEBUG(fmt, ...) printf("\033[0;34mDEBUG:\033[0m   " fmt "\n", ##__VA_ARGS__)
#define ROUND_UP_TO_MULTIPLE_OF_8(x) ((((x) + 7) / 8) * 8)
#define ROUND_UP_TO_MULTIPLE_OF_32(x) ((((x)-1) / 32 + 1) * 32)

// Note: this is overriden by compiler flags.
#ifndef NR_TASKLETS
#define NR_TASKLETS 16
#endif

__host __mram_ptr void *p_used_mram_end = DPU_MRAM_HEAP_POINTER; // Points to the end of used MRAM addresses.

// CSR data.
__host __mram_ptr uint32_t *node_ptrs; // DPU's share of node_ptrs.
__host __mram_ptr uint32_t *edges;     // DPU's share of edges.

// Chunks data.
__host uint32_t len_nf;     // Length of next_frontier.
__host uint32_t len_cf;     // Length of curr_frontier.
__host uint32_t len_nf_tsk; // Length of next_frontier per tasklet.
__host uint32_t len_cf_tsk; // Length of curr_frontier per tasklet.

// BFS data.
__host uint32_t level;                     // Current level of the BFS.
__host __mram_ptr uint32_t *visited;       // Nodes that are already visited.
__host __mram_ptr uint32_t *curr_frontier; // Nodes that are in the current frontier.
__host __mram_ptr uint32_t *next_frontier; // Nodes that are in the next frontier.
__host __mram_ptr uint32_t *node_levels;   // OUTPUT of the BFS.

BARRIER_INIT(nf_barrier, NR_TASKLETS);
MUTEX_INIT(nf_mutex);

int main() {

  const uint32_t idx_nf = me() * len_nf_tsk;
  const uint32_t lim_nf = idx_nf + len_nf_tsk;
  const uint32_t idx_cf = me() * len_cf_tsk;
  const uint32_t lim_cf = idx_cf + len_cf_tsk;

  // Loop over next_frontier.
  for (uint32_t c = idx_nf; c < lim_nf; ++c) {
    visited[c] |= next_frontier[c]; // Update visited nodes.
    next_frontier[c] = 0;           // Clear nf.
  }

  barrier_wait(&nf_barrier);

  // Loop over curr_frontier.
  for (uint32_t c = idx_cf; c < lim_cf; ++c) {

    uint32_t f = curr_frontier[c];

    // For each set node in the curr_frontier.
    for (uint32_t b = 0; b < 32; ++b)
      if (f & 1 << b % 32) {
        uint32_t node = c * 32 + b;
        node_levels[node] = level; // Update node level.

        // Get node_ptrs of this node.
        uint32_t from = node_ptrs[node];
        uint32_t to = node_ptrs[node + 1];

        // For each not visited neighbor of this node, add it to next_frontier.
        for (uint32_t n = from; n < to; ++n) {
          uint32_t neighbor = edges[n];
          uint32_t offset = 1 << neighbor % 32;

          if (!(visited[neighbor / 32] & offset)) {
            mutex_lock(nf_mutex);
            next_frontier[neighbor / 32] |= offset;
            mutex_unlock(nf_mutex);
          }
        }
      }
  }
}
