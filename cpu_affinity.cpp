/*
 * cpu_affinity.cpp
 */

#include "cpu_affinity.hpp"

#ifdef _WIN32
#include <windows.h>
#include <vector>
#include <sstream>
#else
#include <sched.h>        // sched_setaffinity, cpu_set_t
#include <unistd.h>       // sysconf
#include <cerrno>
#include <fstream>
#include <set>
#endif

// ── Helpers ──────────────────────────────────────────────────────────────────

#ifdef _WIN32
static std::string mask_to_hex(DWORD_PTR mask) {
    std::ostringstream os;
    os << "0x" << std::hex << std::uppercase << static_cast<unsigned long long>(mask);
    return os.str();
}

static std::vector<int> mask_to_logical_ids(DWORD_PTR mask) {
    std::vector<int> out;
    const int bits = static_cast<int>(sizeof(DWORD_PTR) * 8);
    for (int i = 0; i < bits; ++i) {
        if (mask & (static_cast<DWORD_PTR>(1) << i)) out.push_back(i);
    }
    return out;
}

static std::vector<DWORD_PTR> enumerate_physical_core_masks() {
    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len);
    if (len == 0) return {};

    std::vector<unsigned char> buf(len);
    auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(buf.data());
    if (!GetLogicalProcessorInformation(info, &len)) return {};

    std::vector<DWORD_PTR> masks;
    const std::size_t count = len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    masks.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (info[i].Relationship == RelationProcessorCore) {
            masks.push_back(info[i].ProcessorMask);
        }
    }
    return masks;
}

static int count_logical_cores() {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return static_cast<int>(info.dwNumberOfProcessors);
}

static int count_physical_cores() {
    auto masks = enumerate_physical_core_masks();
    if (!masks.empty()) return static_cast<int>(masks.size());
    return count_logical_cores();
}
#else
/* Parse /proc/cpuinfo to count unique physical cores.
   Each "core id" line gives the physical-core index on one socket.
   We collect unique (physical id, core id) pairs. */
static int count_physical_cores() {
    std::ifstream f("/proc/cpuinfo");
    if (!f.is_open()) return sysconf(_SC_NPROCESSORS_ONLN); // fallback

    std::set<std::pair<int,int>> seen;
    int phys_id = 0, core_id = 0;
    bool has_phys = false, has_core = false;
    std::string line;

    while (std::getline(f, line)) {
        if (line.empty()) {
            if (has_phys && has_core)
                seen.insert({phys_id, core_id});
            has_phys = has_core = false;
            phys_id = core_id = 0;
            continue;
        }
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        // trim
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!val.empty() && val.front() == ' ') val.erase(val.begin());

        if (key == "physical id") { phys_id  = std::stoi(val); has_phys = true; }
        if (key == "core id")     { core_id  = std::stoi(val); has_core = true; }
    }
    if (has_phys && has_core) seen.insert({phys_id, core_id});
    return seen.empty() ? (int)sysconf(_SC_NPROCESSORS_ONLN) : (int)seen.size();
}
#endif

// ── Public API ───────────────────────────────────────────────────────────────

CoreInfo get_core_info() {
    CoreInfo info;
#ifdef _WIN32
    info.logical_cores  = count_logical_cores();
#else
    info.logical_cores  = (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
    info.physical_cores = count_physical_cores();
    // guard: physical should never exceed logical
    if (info.physical_cores > info.logical_cores)
        info.physical_cores = info.logical_cores;
    info.hyperthreading = (info.logical_cores > info.physical_cores);
    info.ht_ratio       = info.physical_cores > 0
                          ? info.logical_cores / info.physical_cores
                          : 1;
    return info;
}

AffinityResult pin_to_physical_core(int physical_core_id) {
    AffinityResult res;
    res.requested_physical_core = physical_core_id;
    res.info = get_core_info();

    if (physical_core_id >= res.info.physical_cores) {
        res.success = false;
        res.error   = "Requested core " + std::to_string(physical_core_id)
                    + " but only " + std::to_string(res.info.physical_cores)
                    + " physical cores available (0-"
                    + std::to_string(res.info.physical_cores - 1) + ").";
        return res;
    }

#ifdef _WIN32
    auto core_masks = enumerate_physical_core_masks();
    DWORD_PTR mask = 0;
    if (!core_masks.empty()) {
        mask = core_masks[physical_core_id];
    } else {
        // Fallback: assume 1 logical CPU per physical core.
        mask = static_cast<DWORD_PTR>(1) << physical_core_id;
    }

    HANDLE proc = GetCurrentProcess();
    DWORD_PTR processMask = 0;
    DWORD_PTR systemMask = 0;
    GetProcessAffinityMask(proc, &processMask, &systemMask);

    if (SetProcessAffinityMask(proc, mask)) {
        res.success = true;
        res.pinned_logical_cores = mask_to_logical_ids(mask);
        if (res.pinned_logical_cores.empty()) {
            res.pinned_logical_cores.push_back(physical_core_id);
        }
        DWORD_PTR afterMask = 0;
        DWORD_PTR afterSystemMask = 0;
        GetProcessAffinityMask(proc, &afterMask, &afterSystemMask);
        res.api_used = "SetProcessAffinityMask (Windows)";
        res.evidence = "before=" + mask_to_hex(processMask)
                     + ", requested=" + mask_to_hex(mask)
                     + ", after=" + mask_to_hex(afterMask);
    } else {
        res.success = false;
        res.error = "SetProcessAffinityMask failed (GetLastError="
                  + std::to_string(GetLastError()) + ")";
        res.api_used = "SetProcessAffinityMask (Windows)";
        res.evidence = "before=" + mask_to_hex(processMask)
                     + ", requested=" + mask_to_hex(mask);
    }
#else
    // Logical sibling IDs for this physical core.
    // On HT systems physical core i → logical {i, i + physical_cores}.
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    for (int sibling = 0; sibling < res.info.ht_ratio; ++sibling) {
        int logical_id = physical_core_id + sibling * res.info.physical_cores;
        if (logical_id < res.info.logical_cores) {
            CPU_SET(logical_id, &cpuset);
            res.pinned_logical_cores.push_back(logical_id);
        }
    }

    // Pin the calling thread (pid 0 = current thread)
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0) {
        res.success = true;
        res.api_used = "sched_setaffinity (Linux)";
        res.evidence = "Pinned current thread to logical CPUs for requested physical core.";
    } else {
        res.success = false;
        res.error   = "sched_setaffinity failed (errno=" + std::to_string(errno) + ")";
        res.api_used = "sched_setaffinity (Linux)";
    }
#endif
    return res;
}

void release_affinity() {
#ifdef _WIN32
    int logical = count_logical_cores();
    DWORD_PTR all_mask = 0;
    int max_bits = static_cast<int>(sizeof(DWORD_PTR) * 8);
    for (int i = 0; i < logical && i < max_bits; ++i) {
        all_mask |= (static_cast<DWORD_PTR>(1) << i);
    }
    SetProcessAffinityMask(GetCurrentProcess(), all_mask);
#else
    int logical = (int)sysconf(_SC_NPROCESSORS_ONLN);
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i < logical; ++i)
        CPU_SET(i, &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);
#endif
}
