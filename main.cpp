/*
 * main.cpp
 * ─────────
 * CPU-Pinned Algorithm Complexity Profiler — C++17
 *
 * Usage:
 *   ./algoscope                   full run  (pin + benchmark + save JSON)
 *   ./algoscope --core 1          use physical core 1
 *   ./algoscope --trials 5        5 timing trials per (algo, n)
 *   ./algoscope --input reverse   benchmark on reverse-sorted input
 *   ./algoscope --quiet           suppress per-size output
 *   ./algoscope --out results.json  custom output path
 */

#include "cpu_affinity.hpp"
#include "benchmarker.hpp"
#include "algorithms.hpp"

#include <iostream>
#include <string>
#include <cstring>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

// ── CLI parsing ──────────────────────────────────────────────────────────────

struct CLIArgs {
    int         core            = 0;
    int         trials          = 7;
    std::string input_kind      = "random";
    std::string dataset         = "large";
    bool        verbose         = true;
    std::string outfile         = "benchmark_results.json";
    bool        observe         = false;   // live single-core demo mode
    int         observe_seconds = 30;      // duration of demo
    std::string observe_algo    = "matrix"; // matrix | bubble
    bool        pause_on_exit   = true;    // wait for Enter before exiting
};

static void applyDatasetProfile(BenchmarkConfig& cfg, const std::string& dataset) {
    cfg.dataset_profile = dataset;
    if (dataset == "small") {
        cfg.normal_sizes = {100, 250, 500, 1000, 2000, 4000};
        cfg.cubic_sizes  = {20, 30, 40, 50, 60, 70};
    } else if (dataset == "standard") {
        cfg.normal_sizes = {200, 500, 1000, 2000, 4000, 8000, 12000};
        cfg.cubic_sizes  = {20, 30, 40, 50, 60, 70, 80};
    } else if (dataset == "large") {
        cfg.normal_sizes = {500, 1000, 2500, 5000, 10000, 15000, 20000, 30000};
        cfg.cubic_sizes  = {30, 40, 50, 60, 70, 80, 90, 100};
    } else if (dataset == "xlarge") {
        cfg.normal_sizes = {1000, 2500, 5000, 10000, 20000, 30000, 45000, 60000};
        cfg.cubic_sizes  = {40, 50, 60, 70, 80, 90, 100, 110};
    } else {
        cfg.dataset_profile = "large";
        cfg.normal_sizes = {500, 1000, 2500, 5000, 10000, 15000, 20000, 30000};
        cfg.cubic_sizes  = {30, 40, 50, 60, 70, 80, 90, 100};
    }
}

static CLIArgs parseArgs(int argc, char* argv[]) {
    CLIArgs args;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--core")     && i+1 < argc) args.core            = std::stoi(argv[++i]);
        if (!strcmp(argv[i], "--trials")   && i+1 < argc) args.trials          = std::stoi(argv[++i]);
        if (!strcmp(argv[i], "--input")    && i+1 < argc) args.input_kind      = argv[++i];
        if (!strcmp(argv[i], "--dataset")  && i+1 < argc) args.dataset         = argv[++i];
        if (!strcmp(argv[i], "--out")      && i+1 < argc) args.outfile         = argv[++i];
        if (!strcmp(argv[i], "--seconds")  && i+1 < argc) args.observe_seconds = std::stoi(argv[++i]);
        if (!strcmp(argv[i], "--algo")     && i+1 < argc) args.observe_algo    = argv[++i];
        if (!strcmp(argv[i], "--observe"))                args.observe         = true;
        if (!strcmp(argv[i], "--quiet"))                  args.verbose         = false;
        if (!strcmp(argv[i], "--no-pause"))               args.pause_on_exit   = false;
        if (!strcmp(argv[i], "--help")) {
            std::cout
                << "Usage: ./algoscope [options]\n"
                << "  --core   N      Physical core to pin to (default: 0)\n"
                << "  --trials N      Timing trials per run    (default: 7)\n"
                << "  --input  KIND   random | sorted | reverse (default: random)\n"
                << "  --dataset SIZE  small | standard | large | xlarge (default: large)\n"
                << "  --out    FILE   JSON output path\n"
                << "  --quiet         Suppress per-size output\n"
                << "\n"
                << "  --observe       LIVE demo: run a heavy algorithm in a loop so you\n"
                << "                  can watch the pinned core in Task Manager.\n"
                << "  --seconds N     Duration of --observe mode (default: 30)\n"
                << "  --algo NAME     Algorithm for --observe: matrix | bubble (default: matrix)\n"
                << "  --no-pause      Don't wait for Enter before exiting (used by the menu)\n";
            std::exit(0);
        }
    }
    return args;
}

// ── Banner ───────────────────────────────────────────────────────────────────

static void printBanner(const CLIArgs& args) {
    std::cout <<
        "\n╔══════════════════════════════════════════════════════════╗\n"
        "║    AlgoScope — CPU-Pinned Complexity Profiler  [C++17]  ║\n"
        "╚══════════════════════════════════════════════════════════╝\n\n";
    std::cout << "  Core      : " << args.core       << " (physical)\n";
    if (args.observe) {
        std::cout << "  Mode      : LIVE single-core demo\n";
        std::cout << "  Algorithm : " << args.observe_algo << "\n";
        std::cout << "  Duration  : " << args.observe_seconds << " s\n\n";
    } else {
        std::cout << "  Trials    : " << args.trials     << " per (algo, n)\n";
        std::cout << "  Input     : " << args.input_kind << "\n";
        std::cout << "  Dataset   : " << args.dataset    << "\n";
        std::cout << "  Output    : " << args.outfile    << "\n\n";
    }
}

// ── Live single-core demo ────────────────────────────────────────────────────
//
// Runs a CPU-heavy algorithm in a tight loop for a fixed wall-clock duration
// AFTER the affinity pin is in place. This gives the presenter time to open
// Task Manager and visually verify that ONLY the pinned core spikes to 100%
// while every other core stays idle. The PID is printed so the same process
// can be identified in Task Manager > Details.

// Wait for the user to press Enter before exiting, so the console window
// stays open when the exe is launched by double-click or from a context
// that would otherwise close it the moment main() returns. Skipped when
// --no-pause is passed (the PowerShell menu does its own pause).
static void waitForEnter(const CLIArgs& args) {
    if (!args.pause_on_exit) return;
    std::cout << "\nPress Enter to exit ...";
    std::cout.flush();
    std::cin.clear();
    std::cin.get();
}

static int currentProcessId() {
#ifdef _WIN32
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}

static void runObserveMode(const CLIArgs& args, const AffinityResult& aff) {
    std::cout << "\n▶  LIVE SINGLE-CORE DEMO\n";
    std::cout << "   PID            : " << currentProcessId() << "\n";
    std::cout << "   Pinned core    : physical " << args.core;
    if (!aff.pinned_logical_cores.empty()) {
        std::cout << "  → logical CPUs [";
        for (std::size_t i = 0; i < aff.pinned_logical_cores.size(); ++i) {
            std::cout << aff.pinned_logical_cores[i];
            if (i + 1 < aff.pinned_logical_cores.size()) std::cout << ", ";
        }
        std::cout << "]";
    }
    std::cout << "\n";
    std::cout << "   Algorithm      : " << args.observe_algo << "\n";
    std::cout << "   Duration       : " << args.observe_seconds << " seconds\n\n";

    std::cout << "   HOW TO SEE IT LIVE (Windows Task Manager):\n";
    std::cout << "   1) Press Ctrl + Shift + Esc to open Task Manager.\n";
    std::cout << "   2) Go to the 'Performance' tab → click 'CPU'.\n";
    std::cout << "   3) Right-click the big CPU graph →\n";
    std::cout << "      'Change graph to' → 'Logical processors'.\n";
    std::cout << "   4) Watch ONLY the pinned logical CPU(s) above hit ~100%;\n";
    std::cout << "      every other core graph should stay near idle.\n";
    std::cout << "   5) For extra proof: open the 'Details' tab, find PID "
              << currentProcessId() << " (algoscope.exe),\n"
              << "      right-click → 'Set affinity'. The dialog will show\n"
              << "      that only the pinned CPU(s) are checked.\n\n";

    std::cout << "   Starting heavy loop now ... press Ctrl+C to stop early.\n\n";

    // Pick a payload sized so each iteration takes a non-trivial chunk of
    // CPU time, but still completes often enough that the countdown ticks
    // smoothly. Matrix multiply at n=300 is ~27M multiplications per call.
    std::vector<int> payload;
    AlgoFn fn;

    if (args.observe_algo == "bubble") {
        payload = generateInput(8000, "reverse"); // worst case for bubble
        fn = bubbleSort;
    } else {
        // Default: matrix multiply. Vector length must be a perfect square so
        // matrixMultiply uses an n×n matrix. 300*300 = 90000 ints.
        payload = generateInput(300 * 300, "random");
        fn = matrixMultiply;
    }

    auto start = std::chrono::steady_clock::now();
    auto deadline = start + std::chrono::seconds(args.observe_seconds);
    long long iterations = 0;
    int last_remaining = -1;

    while (std::chrono::steady_clock::now() < deadline) {
        std::vector<int> copy = payload;
        fn(copy);
        ++iterations;

        auto now = std::chrono::steady_clock::now();
        int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(deadline - now).count());
        if (remaining != last_remaining && remaining >= 0) {
            std::cout << "\r   ⏳  " << remaining
                      << " s remaining   (iterations completed: "
                      << iterations << ")        " << std::flush;
            last_remaining = remaining;
        }
    }

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();

    std::cout << "\n\n   ✅  Demo finished.\n";
    std::cout << "   Iterations     : " << iterations << "\n";
    std::cout << "   Wall time      : " << elapsed << " s\n";
    std::cout << "   Avg per iter   : "
              << (iterations > 0 ? (elapsed * 1000.0 / iterations) : 0.0)
              << " ms\n\n";
    std::cout << "   While this was running, Task Manager should have shown\n"
              << "   ONLY core " << args.core
              << " (and its HT sibling, if hyper-threading is on) busy.\n";
    std::cout << "   That is the visual proof that the algorithm executed on\n"
              << "   exactly one physical CPU core.\n";
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    CLIArgs args = parseArgs(argc, argv);
    printBanner(args);

    // ── Step 1: CPU Affinity ─────────────────────────────────────────────────
    std::cout << "▶  Step 1/2 — CPU Core Setup\n";

    CoreInfo info = get_core_info();
    std::cout << "   Physical cores  : " << info.physical_cores << "\n";
    std::cout << "   Logical cores   : " << info.logical_cores  << "\n";
    std::cout << "   Hyperthreading  : " << (info.hyperthreading ? "yes" : "no") << "\n";

    AffinityResult aff = pin_to_physical_core(args.core);
    if (aff.success) {
        std::cout << "   ✅  Pinned to physical core " << args.core << " → logical [";
        for (std::size_t i = 0; i < aff.pinned_logical_cores.size(); ++i) {
            std::cout << aff.pinned_logical_cores[i];
            if (i + 1 < aff.pinned_logical_cores.size()) std::cout << ", ";
        }
        std::cout << "]\n";
    } else {
        std::cout << "   ⚠️   Affinity pin failed: " << aff.error << "\n";
        std::cout << "   Benchmarks will still run but may have timing noise.\n";
    }

    // ── Observe (live demo) mode short-circuits the benchmark step ───────────
    if (args.observe) {
        runObserveMode(args, aff);
        release_affinity();
        std::cout << "\n✅  Live demo done. CPU affinity released.\n\n";
        waitForEnter(args);
        return 0;
    }

    // ── Step 2: Benchmark ────────────────────────────────────────────────────
    std::cout << "\n▶  Step 2/2 — Benchmarking\n";

    BenchmarkConfig cfg;
    cfg.trials     = args.trials;
    cfg.input_kind = args.input_kind;
    cfg.verbose    = args.verbose;
    applyDatasetProfile(cfg, args.dataset);

    auto t0 = std::chrono::steady_clock::now();
    auto results = runBenchmarks(cfg);
    auto t1 = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = t1 - t0;

    ProfilerRunMeta run_meta;
    run_meta.physical_core    = args.core;
    run_meta.detected_physical_cores = info.physical_cores;
    run_meta.detected_logical_cores = info.logical_cores;
    run_meta.affinity_success = aff.success;
    run_meta.pinned_logical_cpus = aff.pinned_logical_cores;
    if (!aff.success) run_meta.affinity_error = aff.error;
    run_meta.affinity_api = aff.api_used;
    run_meta.affinity_evidence = aff.evidence;
    run_meta.dataset_profile = cfg.dataset_profile;
    for (const auto& r : results) run_meta.total_points += static_cast<int>(r.sizes.size());
    run_meta.run_duration_sec = elapsed.count();

    saveResultsJson(results, cfg, args.outfile, &run_meta);

    // ── Release affinity ─────────────────────────────────────────────────────
    release_affinity();

    std::cout << "\n✅  Done!  CPU affinity released.\n";
    std::cout << "   Open dashboard.html in your browser to explore results.\n";
    waitForEnter(args);
    return 0;
}
