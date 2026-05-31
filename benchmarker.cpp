/*
 * benchmarker.cpp
 */

#include "benchmarker.hpp"
#include "algorithms.hpp"

#include <chrono>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using Clock    = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double, std::milli>;

// ── Single timing run ────────────────────────────────────────────────────────

TimingResult timeAlgorithm(const AlgoFn& fn,
                            const std::vector<int>& data,
                            int trials) {
    std::vector<double> times;
    times.reserve(trials);

    for (int t = 0; t < trials; ++t) {
        std::vector<int> copy = data;   // fresh copy every trial

        auto t0 = Clock::now();
        fn(copy);
        auto t1 = Clock::now();

        times.push_back(Duration(t1 - t0).count());
    }

    std::sort(times.begin(), times.end());

    double median = (trials % 2 == 0)
                  ? (times[trials/2-1] + times[trials/2]) / 2.0
                  : times[trials/2];

    double mean = std::accumulate(times.begin(), times.end(), 0.0) / trials;

    double var = 0;
    for (double v : times) var += (v - mean) * (v - mean);
    double stdev = trials > 1 ? std::sqrt(var / (trials - 1)) : 0.0;

    return { median, mean, times.front(), times.back(), stdev };
}

// ── Main benchmark runner ────────────────────────────────────────────────────

std::vector<AlgoBenchmark> runBenchmarks(const BenchmarkConfig& cfg) {
    const auto& algos = getAlgorithms();
    std::vector<AlgoBenchmark> results;
    results.reserve(algos.size());

    int total = (int)algos.size();

    for (int ai = 0; ai < total; ++ai) {
        const auto& meta = algos[ai];
        bool isCubic = (meta.complexity == "O(n^3)");
        const auto& sizes = isCubic ? cfg.cubic_sizes : cfg.normal_sizes;

        AlgoBenchmark bench;
        bench.name       = meta.name;
        bench.complexity = meta.complexity;
        bench.color      = meta.color;
        bench.category   = meta.category;

        if (cfg.verbose)
            std::cout << "\n[" << (ai+1) << "/" << total << "]  "
                      << meta.name << "  (" << meta.complexity << ")\n";

        for (int n : sizes) {
            // Matrix multiply interprets n as matrix dimension, so allocate n*n elements.
            int input_n = isCubic ? (n * n) : n;
            auto data = generateInput(input_n, cfg.input_kind);
            auto res  = timeAlgorithm(meta.fn, data, cfg.trials);

            bench.sizes.push_back(n);
            bench.times_ms.push_back(res.median_ms);

            if (cfg.verbose)
                std::cout << "  n=" << std::setw(5) << n
                          << "  →  " << std::fixed << std::setprecision(4)
                          << std::setw(10) << res.median_ms << " ms"
                          << "  (σ=" << std::setprecision(4) << res.stdev_ms << " ms)\n";
        }
        results.push_back(std::move(bench));
    }
    return results;
}

// ── JSON output ──────────────────────────────────────────────────────────────

// Tiny manual JSON builder — no external dependencies
static std::string jsonDouble(double v) {
    std::ostringstream os;
    os << std::setprecision(8) << v;
    return os.str();
}

static std::string jsonIntArray(const std::vector<int>& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        s += std::to_string(v[i]);
        if (i + 1 < v.size()) s += ",";
    }
    return s + "]";
}

static std::string jsonDoubleArray(const std::vector<double>& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        s += jsonDouble(v[i]);
        if (i + 1 < v.size()) s += ",";
    }
    return s + "]";
}

static std::string jsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c == '\\' || c == '"') o += '\\';
        if (c == '\n' || c == '\r') { o += ' '; continue; }
        o += c;
    }
    return o;
}

void saveResultsJson(const std::vector<AlgoBenchmark>& results,
                     const BenchmarkConfig& cfg,
                     const std::string& filepath,
                     const ProfilerRunMeta* run_meta) {
    std::ofstream f(filepath);
    if (!f.is_open()) {
        std::cerr << "ERROR: cannot open " << filepath << "\n";
        return;
    }

    // Get timestamp
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

    f << "{\n";
    f << "  \"meta\": {\n";
    f << "    \"trials\": "      << cfg.trials                      << ",\n";
    f << "    \"input_kind\": \"" << cfg.input_kind                  << "\",\n";
    f << "    \"dataset_profile\": \"" << cfg.dataset_profile         << "\",\n";
    f << "    \"timestamp\": \""  << ts                              << "\",\n";
    f << "    \"language\": \"C++17\",\n";
    f << "    \"compiler\": \"" << jsonEscape(__VERSION__)            << "\",\n";
    f << "    \"measurement_method\": \"median wall-clock ms per (algorithm, n); std::chrono::high_resolution_clock; fresh input copy each trial\",\n";
    f << "    \"theory_note\": \"Big-O labels classify algorithms; every plotted point is a measured time — not a formula.\",\n";
    if (run_meta) {
        f << "    \"physical_core\": " << run_meta->physical_core << ",\n";
        f << "    \"detected_physical_cores\": " << run_meta->detected_physical_cores << ",\n";
        f << "    \"detected_logical_cores\": " << run_meta->detected_logical_cores << ",\n";
        f << "    \"affinity_pinned\": " << (run_meta->affinity_success ? "true" : "false") << ",\n";
        f << "    \"pinned_logical_cpus\": " << jsonIntArray(run_meta->pinned_logical_cpus) << ",\n";
        f << "    \"affinity_api\": \"" << jsonEscape(run_meta->affinity_api) << "\",\n";
        f << "    \"affinity_evidence\": \"" << jsonEscape(run_meta->affinity_evidence) << "\",\n";
        f << "    \"total_points\": " << run_meta->total_points << ",\n";
        f << "    \"run_duration_sec\": " << jsonDouble(run_meta->run_duration_sec) << ",\n";
        if (!run_meta->affinity_error.empty())
            f << "    \"affinity_error\": \"" << jsonEscape(run_meta->affinity_error) << "\",\n";
    } else {
        f << "    \"physical_core\": 0,\n";
        f << "    \"detected_physical_cores\": 0,\n";
        f << "    \"detected_logical_cores\": 0,\n";
        f << "    \"affinity_pinned\": false,\n";
        f << "    \"pinned_logical_cpus\": [],\n";
        f << "    \"affinity_api\": \"\",\n";
        f << "    \"affinity_evidence\": \"\",\n";
        f << "    \"total_points\": 0,\n";
        f << "    \"run_duration_sec\": 0,\n";
    }
    f << "    \"affinity_note\": \"Single-core pinning reduces scheduler migration noise; verify pinned_logical_cpus and affinity_evidence for proof.\"\n";
    f << "  },\n";
    f << "  \"algorithms\": {\n";

    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        f << "    \"" << r.name << "\": {\n";
        f << "      \"complexity\": \"" << r.complexity  << "\",\n";
        f << "      \"color\": \""      << r.color       << "\",\n";
        f << "      \"category\": \""   << r.category    << "\",\n";
        f << "      \"sizes\": "        << jsonIntArray(r.sizes)      << ",\n";
        f << "      \"times_ms\": "     << jsonDoubleArray(r.times_ms) << "\n";
        f << "    }";
        if (i + 1 < results.size()) f << ",";
        f << "\n";
    }

    f << "  }\n}\n";
    f.close();
    std::cout << "\n✅  Results saved → " << filepath << "\n";
}
