#pragma once
/*
 * cpu_affinity.hpp
 * ─────────────────
 * Pin the current process/thread to a single physical CPU core.
 *
 * Why bother?
 *   The OS scheduler migrates threads between cores freely.
 *   Each migration flushes the L1/L2 cache → unpredictable timing noise.
 *   Pinning to one physical core gives stable, reproducible benchmarks.
 *
 * Platform: Linux (uses sched_setaffinity via <sched.h>)
 * Windows equivalent: SetThreadAffinityMask(GetCurrentThread(), mask)
 */

#include <string>
#include <vector>

struct CoreInfo {
    int  physical_cores;   // distinct physical silicon cores
    int  logical_cores;    // OS-visible threads (incl. HT siblings)
    bool hyperthreading;   // true if logical > physical
    int  ht_ratio;         // logical / physical
};

struct AffinityResult {
    int              requested_physical_core;
    std::vector<int> pinned_logical_cores;   // sibling logical IDs pinned
    bool             success;
    std::string      error;
    std::string      api_used;               // OS API used for affinity
    std::string      evidence;               // human-readable proof details
    CoreInfo         info;
};

CoreInfo       get_core_info();
AffinityResult pin_to_physical_core(int physical_core_id = 0);
void           release_affinity();   // restore: run on all cores
