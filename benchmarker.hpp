#pragma once
/*
 * benchmarker.hpp
 * ───────────────
 * Times algorithms across a range of input sizes on the pinned core.
 * Uses std::chrono::high_resolution_clock — nanosecond resolution.
 *
 * Design:
 *   - Each (algo, n) pair is timed TRIALS times.
 *   - The MEDIAN is kept → robust against cache-warm-up spikes.
 *   - Results are emitted as JSON for the HTML dashboard.
 */

#include "algorithms.hpp"
#include <string>
#include <vector>

struct TimingResult {
    double median_ms;
    double mean_ms;
    double min_ms;
    double max_ms;
    double stdev_ms;
};

struct AlgoBenchmark {
    std::string          name;
    std::string          complexity;
    std::string          color;
    std::string          category;
    std::vector<int>     sizes;
    std::vector<double>  times_ms;
};

struct BenchmarkConfig {
    std::vector<int> normal_sizes  = {50,100,250,500,750,1000,1500,2000,3000,5000};
    std::vector<int> cubic_sizes   = {10,20,30,40,50,60,70,80,90,100};
    int              trials        = 7;
    std::string      input_kind    = "random";
    std::string      dataset_profile = "standard";
    bool             verbose       = true;
};

TimingResult timeAlgorithm(const AlgoFn& fn, const std::vector<int>& data,
                           int trials);

std::vector<AlgoBenchmark> runBenchmarks(const BenchmarkConfig& cfg);

/** Extra fields written into JSON meta (affinity, measurement honesty). */
struct ProfilerRunMeta {
    int              physical_core       = 0;
    int              detected_physical_cores = 0;
    int              detected_logical_cores  = 0;
    bool             affinity_success    = false;
    std::vector<int> pinned_logical_cpus;
    std::string      affinity_error;
    std::string      affinity_api;
    std::string      affinity_evidence;
    std::string      dataset_profile;
    int              total_points = 0;
    double           run_duration_sec = 0.0;
};

void saveResultsJson(const std::vector<AlgoBenchmark>& results,
                     const BenchmarkConfig& cfg,
                     const std::string& filepath,
                     const ProfilerRunMeta* run_meta = nullptr);
