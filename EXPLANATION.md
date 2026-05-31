# AlgoScope — Project Explanation

**Course project — algorithm complexity analysis on a single, dedicated physical CPU core.**
Language: C++17. Platform shown here: Windows 11. Dashboard: HTML + Chart.js.

---

## 1. The goal of this project

When a student measures how fast an algorithm runs, the result is normally
**noisy**. The operating system constantly moves the running program from one
CPU core to another. Every time it moves:

* the **L1 / L2 cache** on the old core is thrown away,
* the program restarts cold on the new core,
* the measured time jumps around for no algorithmic reason.

This project pins the algorithm to **exactly one physical CPU core** before
benchmarking it. That gives a stable, fair, reproducible measurement of how
each algorithm's running time grows with input size — which is what
"algorithm analysis" actually means in practice.

So the project does three things:

1. **Locks the program onto one physical core** of the CPU.
2. **Runs 10 classic algorithms** at many different input sizes and times them.
3. **Visualises the result** in a dashboard so you can compare measured
   running times against the Big-O complexity classes you learn in class.

---

## 2. What is "pinning to a physical core" (CPU affinity)?

Modern CPUs have several **physical cores**. With hyper-threading, each
physical core exposes **2 logical cores** to the operating system. The
scheduler is free to move a thread to any logical core at any time.

**CPU affinity** is an OS-level setting that says: *"this process is only
allowed to run on these specific cores."* Once set, the scheduler will not
migrate the process anywhere else.

| OS      | API used                                | File / line in this project |
|---------|-----------------------------------------|-----------------------------|
| Windows | `SetProcessAffinityMask` (Win32 API)    | [cpu_affinity.cpp:154](cpu_affinity.cpp#L154) |
| Linux   | `sched_setaffinity` (POSIX)             | [cpu_affinity.cpp:190](cpu_affinity.cpp#L190) |

On a 6-core / 12-thread CPU, "physical core 0" corresponds to logical CPUs
**0 and 1** (the two hyper-threads of the same silicon core). The program
detects this automatically and pins both.

The detection of physical-vs-logical core counts is done with
`GetLogicalProcessorInformation` on Windows ([cpu_affinity.cpp:38](cpu_affinity.cpp#L38))
and by parsing `/proc/cpuinfo` on Linux ([cpu_affinity.cpp:72](cpu_affinity.cpp#L72)).

---

## 3. How the project is organised

```
algo_Analysis_using_cpp/
├── main.cpp              # CLI, pins the core, runs the benchmark or demo
├── cpu_affinity.hpp/.cpp # OS-level "pin to physical core" logic
├── algorithms.hpp/.cpp   # 10 algorithm implementations + registry
├── benchmarker.hpp/.cpp  # Times each (algo, n), writes JSON
├── dashboard.html        # Loads JSON, draws charts, shows proof panel
├── dashboard_bridge.py   # Optional: control the C++ exe from the browser
├── run_algoscope.ps1     # Interactive menu for Windows
├── benchmark_results.json# Output written by the C++ exe
└── algoscope.exe         # Compiled binary
```

### What each algorithm represents

| Algorithm         | Complexity   | Why we include it                                    |
|-------------------|--------------|------------------------------------------------------|
| Constant Access   | O(1)         | Baseline — should be flat regardless of n            |
| Binary Search     | O(log n)     | Logarithmic growth — barely rises as n grows         |
| Linear Search     | O(n)         | Touches every element once                           |
| Prefix Sum        | O(n)         | Another O(n) — cache-friendly array sweep            |
| Merge Sort        | O(n log n)   | Divide-and-conquer sort                              |
| Heap Sort         | O(n log n)   | In-place comparison sort using a binary heap         |
| Insertion Sort    | O(n²)        | Quadratic, fast on small or nearly-sorted input      |
| Selection Sort    | O(n²)        | Quadratic, always n² regardless of input             |
| Bubble Sort       | O(n²)        | Quadratic, classic textbook example                  |
| Matrix Multiply   | O(n³)        | Cubic — triple nested loop                           |

All ten are in [algorithms.cpp](algorithms.cpp) and registered in
[algorithms.cpp:159](algorithms.cpp#L159).

---

## 4. How a measurement is taken

For each algorithm `A` and each input size `n`:

1. Generate an input array of `n` elements ([algorithms.cpp:179](algorithms.cpp#L179)).
2. Make a fresh copy of that array (so an in-place sort can't cheat on
   the next trial).
3. Read the wall clock with `std::chrono::high_resolution_clock`.
4. Run the algorithm.
5. Read the clock again. Time = clock₂ − clock₁.
6. Repeat steps 2–5 `--trials` times. Keep the **median**, not the mean.
   The median is robust against the one-off spikes that happen when the
   OS does something unrelated.

This is implemented in [benchmarker.cpp:22](benchmarker.cpp#L22).

The result is `{algorithm, size, median_ms}` rows that get written to
`benchmark_results.json` and plotted in `dashboard.html`.

> **Important:** the Big-O label next to each algorithm is the *theoretical*
> complexity. Every dot on the chart is a *measured* time. The point of the
> project is to compare them — the measured curves should hug the shape of
> their theoretical class.

---

## 5. Technical approach & design decisions

This section lists the non-obvious engineering choices behind the project and
the reason each one was made. These are the questions a teacher is likely
to ask about.

### 5.1 Modular architecture — one job per file

The program is split into four independent C++ modules, each with a header
that declares the public interface and a `.cpp` that implements it:

| Module                              | Responsibility                                   |
|-------------------------------------|--------------------------------------------------|
| [cpu_affinity.hpp/.cpp](cpu_affinity.cpp) | OS-specific pinning + core detection             |
| [algorithms.hpp/.cpp](algorithms.cpp)     | 10 algorithm implementations + input generation  |
| [benchmarker.hpp/.cpp](benchmarker.cpp)   | Timing loop + JSON output                        |
| [main.cpp](main.cpp)                | CLI parsing + observe mode + orchestration       |

This means the timing code never has to know which OS it is on (Windows or
Linux). It just calls `pin_to_physical_core(0)` and gets back a uniform
`AffinityResult` struct. All `#ifdef _WIN32` / `#ifndef _WIN32` branches
are isolated inside one file ([cpu_affinity.cpp](cpu_affinity.cpp)).

### 5.2 **Process** affinity, not thread affinity

The Windows path uses `SetProcessAffinityMask` (not `SetThreadAffinityMask`)
because:

* the program is single-threaded — there is exactly one thread to pin,
* process-level pinning *also* constrains any worker thread the C++
  runtime might spawn (e.g. for `std::async`), which removes a whole class
  of subtle migration bugs,
* the Linux equivalent `sched_setaffinity(0, …)` already pins the calling
  thread by default and (on a single-threaded process) is equivalent.

### 5.3 Pinning the **physical** core, not just one logical CPU

On a hyper-threaded chip, "physical core 0" exposes two logical CPUs —
typically `0` and the `physical_cores`-th index (e.g. logical 0 and 6 on a
6/12 chip). Both share the same L1 and L2 cache. We pin to **both** logical
siblings because:

* if we pinned to only one, and that logical CPU was already running
  another HT thread from the kernel, our process would be starved,
* pinning to both lets the OS run the algorithm on its preferred sibling
  while still guaranteeing it stays on the same physical silicon (same
  cache, same branch predictor),
* it matches what professional benchmark harnesses do.

The sibling mask is built from `GetLogicalProcessorInformation` on Windows
([cpu_affinity.cpp:37](cpu_affinity.cpp#L37)), which returns the actual
processor mask for each physical core — so the project does *not* assume
any specific HT layout and works correctly on AMD chips, big.LITTLE Intel,
etc.

### 5.4 Why **median**, not mean

In [benchmarker.cpp:40](benchmarker.cpp#L40) the timing of each `(algo, n)`
pair takes the **median** of `--trials` runs. The first trial usually
includes a cold-cache penalty (~10–100× slower) and the OS may also fire a
timer interrupt mid-run. The median throws away both the high outlier
*and* any anomalously fast run, leaving the "typical" wall time. Mean
and stdev are still computed and reported alongside, but they are for
diagnostic use only — the chart is plotted from the median.

### 5.5 Fresh input copy on every trial

In-place sorts (insertion, bubble, selection) leave the array sorted
after the first trial. If we re-used the same buffer, trial 2 would run on
already-sorted input and finish in O(n) instead of O(n²). To prevent that,
every trial calls `std::vector<int> copy = data;` ([benchmarker.cpp:29](benchmarker.cpp#L29))
which gives a freshly randomised buffer to each run.

The copy itself is O(n) but is **outside** the timed region — only the
algorithm call is between the two `Clock::now()` reads.

### 5.6 Fixed RNG seed for reproducibility

`generateInput` ([algorithms.cpp:179](algorithms.cpp#L179)) uses
`std::mt19937 rng(42)` — a Mersenne-Twister seeded with `42`. Two runs of
the benchmark on the same machine will therefore produce **bit-identical**
input arrays, so any timing difference between runs is real CPU noise,
not noise from different random data.

### 5.7 Defeating dead-code elimination

Two of the algorithms (`constantAccess`, `linearSearch`) don't write back
to the data buffer — they just read and compute a max. Modern compilers
will happily delete those loops at `-O2` because the result is unused.
To stop that, the result is written to a `volatile int`
([algorithms.cpp:20](algorithms.cpp#L20),
[algorithms.cpp:49](algorithms.cpp#L49)):

```cpp
volatile int x = data.empty() ? 0 : data[0];
```

`volatile` forces the compiler to materialise the read; the loop survives
the optimiser. `matrixMultiply` solves the same problem differently — it
writes the result back into `data` ([algorithms.cpp:151](algorithms.cpp#L151)).

### 5.8 Clock choice

The timing is taken with `std::chrono::high_resolution_clock`. On Windows
with MinGW this maps to `QueryPerformanceCounter`, which has
sub-microsecond resolution and is **not** affected by NTP corrections
mid-run. The duration is materialised as `double` milliseconds via
`std::chrono::duration<double, std::milli>` ([benchmarker.cpp:17–18](benchmarker.cpp#L17)).
We deliberately do **not** use `steady_clock` for the timing itself — only
for the wall-clock deadline of the observe mode — because on some
platforms `steady_clock` has coarser resolution than `high_resolution_clock`.

### 5.9 Compile flags

```text
g++ -std=c++17 -O2 -Wall -Wextra -I.
```

* `-std=c++17` — required for structured bindings and `if constexpr` style
  habits even if not strictly used here.
* `-O2` — production-grade optimisation. We *want* the compiler to
  optimise; the goal is to measure how fast the algorithm *actually* runs,
  not how slow an un-optimised debug build is.
* `-Wall -Wextra` — surfaces signed/unsigned mismatches and unused-result
  warnings that have caused real bugs in the past.

### 5.10 Zero-dependency JSON output

The benchmark JSON is written by hand in [benchmarker.cpp:99–203](benchmarker.cpp#L99),
not by linking a JSON library. This keeps the build pure-toolchain (just
`g++`, no `vcpkg`/`conan`/header-only dependencies) and lets the project
build with a single command on any machine that already has MinGW.

The dashboard then reads the file with the browser's built-in
`JSON.parse`, so the chain has zero external libraries on either side.

### 5.11 Adaptive dataset profiles

`matrixMultiply` is O(n³), so a "size" of 100 already means **1 million**
inner-loop iterations on a 100×100 matrix. Mixing it on the same x-axis as
an O(n) algorithm at `n = 30000` would make either of them invisible on
the chart. The benchmarker therefore reads two separate size arrays from
`BenchmarkConfig` ([benchmarker.hpp:36–37](benchmarker.hpp#L36)):

* `normal_sizes` — used for everything except cubic.
* `cubic_sizes` — used for matrix multiply only.

The `--dataset {small | standard | large | xlarge}` flag selects between
four preset pairs in `applyDatasetProfile` in [main.cpp](main.cpp).

### 5.12 Observe mode — duration-bounded loop

The live demo (`--observe`) uses a steady-clock **deadline**, not a fixed
iteration count, so the demo always takes exactly the requested wall-clock
time regardless of how fast the machine is. The loop body is:

```cpp
auto deadline = std::chrono::steady_clock::now()
              + std::chrono::seconds(args.observe_seconds);
while (std::chrono::steady_clock::now() < deadline) {
    std::vector<int> copy = payload;
    fn(copy);
    ++iterations;
}
```

This keeps the CPU at 100% for the full duration so Task Manager has
something stable to display, but never holds the user hostage for an
unbounded amount of time.

---

## 6. Implementation walkthrough

This section walks through the actual code in execution order, so you can
trace what happens from the moment you type the command to the moment
results are written.

### 6.1 Startup: argument parsing

`parseArgs` in [main.cpp](main.cpp) walks `argv[]` linearly and fills a
`CLIArgs` struct. Unknown flags are silently ignored — this is intentional
so that future PowerShell wrappers can add flags without breaking older
exes. Defaults are set as struct field initialisers so the `--help` text
and the actual defaults can never disagree.

### 6.2 Detecting physical and logical cores

```cpp
CoreInfo info = get_core_info();
```

On Windows, `count_logical_cores()` calls `GetSystemInfo` and reads
`dwNumberOfProcessors`. `count_physical_cores()` calls
`GetLogicalProcessorInformation` — a Win32 API that returns an array of
`SYSTEM_LOGICAL_PROCESSOR_INFORMATION` records. We filter for records
where `Relationship == RelationProcessorCore`; the count of such records
is the physical-core count, and each one's `ProcessorMask` is the bitmask
of logical CPUs that map to that physical core
([cpu_affinity.cpp:37–55](cpu_affinity.cpp#L37)).

On Linux the same information is parsed out of `/proc/cpuinfo`. We
collect the unique pairs of `(physical id, core id)` ([cpu_affinity.cpp:72–102](cpu_affinity.cpp#L72)) —
the set's size is the true physical-core count even on multi-socket
systems.

### 6.3 Pinning to the requested physical core

`pin_to_physical_core(N)` is the public API. It:

1. Validates that `N` is within `[0, physical_cores)`. If not it returns
   an `AffinityResult` with `success = false` and an explanatory error.
2. **Windows:** picks the Nth processor mask from the array, captures the
   current mask with `GetProcessAffinityMask`, calls
   `SetProcessAffinityMask(currentProcess, mask)`, then captures the new
   mask. The before/requested/after values are concatenated into the
   `evidence` string for the JSON dump:
   ```text
   "affinity_evidence": "before=0xFFF, requested=0x3, after=0x3"
   ```
   That string is what tells you, on inspection of the JSON, that the OS
   actually accepted the pin.
3. **Linux:** zeroes a `cpu_set_t`, sets one bit per HT sibling of the
   requested physical core, and calls `sched_setaffinity(0, sizeof(set), &set)`.
   The `0` means "the current thread".
4. Records the list of pinned logical CPUs in `res.pinned_logical_cores`.
   This list is what the dashboard's "Affinity Proof" panel displays and
   what `runObserveMode` prints to the terminal.

### 6.4 The algorithm registry

[algorithms.cpp:159–173](algorithms.cpp#L159) defines a `static const
std::vector<AlgoMeta>` returned by `getAlgorithms()`. Each entry is a
record:

```cpp
{"Bubble Sort", "O(n^2)", "#ff6b6b", "Sorting", bubbleSort}
//   name         label    colour     group     function pointer
```

The function pointer is a `std::function<void(std::vector<int>&)>` — a
type-erased callable so all 10 algorithms can be stored in one container
even though their internal logic is wildly different. To add a new
algorithm you implement `void myAlgo(std::vector<int>&)` and push one
more `AlgoMeta` row. No other file changes.

### 6.5 The timing inner loop

`timeAlgorithm` ([benchmarker.cpp:22–51](benchmarker.cpp#L22)) is the heart
of the measurement:

```cpp
for (int t = 0; t < trials; ++t) {
    std::vector<int> copy = data;        // fresh input every trial
    auto t0 = Clock::now();
    fn(copy);                            // ← the only thing timed
    auto t1 = Clock::now();
    times.push_back(Duration(t1 - t0).count());
}
std::sort(times.begin(), times.end());
double median = (trials % 2 == 0)
              ? (times[trials/2 - 1] + times[trials/2]) / 2.0
              : times[trials/2];
```

Note the order: `Clock::now()` is called immediately before and after the
algorithm. Nothing else lives between those two calls — no allocation, no
I/O. Allocations happen *before* `t0`; printing happens after `t1`. This
is the discipline that makes the measurement honest.

### 6.6 Outer benchmark loop

`runBenchmarks` ([benchmarker.cpp:55–95](benchmarker.cpp#L55)) iterates the
algorithm registry, picks the right size list (`cubic_sizes` for matrix,
`normal_sizes` otherwise), and for each `(algo, n)` pair generates fresh
input then calls `timeAlgorithm`. The median is stored in the per-algorithm
`AlgoBenchmark.times_ms` vector that matches index-by-index with `.sizes`.

For matrix multiply, the integer `n` from the size list is interpreted as
the **matrix dimension**, not the buffer length, so we generate an array
of `n × n` ints:

```cpp
int input_n = isCubic ? (n * n) : n;
auto data = generateInput(input_n, cfg.input_kind);
```

### 6.7 Statistics

For every `(algo, n)`, the timer computes:

* `median_ms` — the value plotted on the chart.
* `mean_ms`   — sum / trials.
* `min_ms`    — fastest trial.
* `max_ms`    — slowest trial.
* `stdev_ms`  — sample standard deviation using Bessel's correction
  (`/ (trials - 1)`).

Only the median is written to JSON to keep the dashboard payload small;
the others are printed to the terminal in verbose mode so you can spot
unstable measurements (a high stdev relative to median is a red flag).

### 6.8 Hand-written JSON serialiser

`saveResultsJson` ([benchmarker.cpp:135–204](benchmarker.cpp#L135)) writes
the output file in three sections: `meta`, `algorithms` (an object keyed
by algorithm name), and a trailing close. Strings are escaped by
`jsonEscape`, which handles the two characters that can actually appear
in our data (`\\`, `\"`) and converts CR/LF to spaces. Doubles are
written via `std::ostringstream` at `setprecision(8)` — enough to capture
microsecond timings without scientific notation noise.

### 6.9 The observe loop

`runObserveMode` in [main.cpp](main.cpp) is invoked only when
`--observe` is passed. It:

1. Prints the PID with `GetCurrentProcessId()` (Windows) or `getpid()` (Linux).
2. Prints the list of logical CPUs that were just pinned, taken straight
   from the `AffinityResult` it received as an argument.
3. Builds a payload — either a 90 000-element random buffer for
   `matrixMultiply` (300×300 matrix → 27 million inner multiplies per
   call) or an 8 000-element reverse-sorted buffer for `bubbleSort`
   (worst-case O(n²) ≈ 32 million swaps per call).
4. Loops with a steady-clock deadline, copying the payload, running the
   algorithm, and updating the on-screen countdown each second via a
   `\r` carriage-return so the line redraws in place.
5. After the deadline, prints iteration count, wall time, and
   average-per-iteration so you have an actual number to quote.
6. Returns; `main` then calls `release_affinity()` and exits cleanly.

### 6.10 Affinity release on exit

`release_affinity()` ([cpu_affinity.cpp:203](cpu_affinity.cpp#L203)) rebuilds
a full-system mask (every logical CPU bit set) and calls
`SetProcessAffinityMask`/`sched_setaffinity` again. This is good hygiene:
if a parent shell process had pinned us before we ran, we don't want to
leave our own narrower mask in place when we exit. On Windows the
process's affinity dies with the process anyway, but the explicit release
keeps behaviour identical on both platforms.

### 6.11 Putting it all together

The full call graph for a benchmark run is:

```text
main()
 ├─ parseArgs(argv)                           [main.cpp]
 ├─ printBanner(args)                         [main.cpp]
 ├─ get_core_info()                           [cpu_affinity.cpp]
 ├─ pin_to_physical_core(args.core)           [cpu_affinity.cpp]
 ├─ runBenchmarks(cfg)                        [benchmarker.cpp]
 │    └─ for each algorithm in registry:
 │         for each size n:
 │             ├─ generateInput(n, kind)      [algorithms.cpp]
 │             └─ timeAlgorithm(fn, data, trials)
 │                  └─ for each trial:
 │                       ├─ copy data
 │                       ├─ Clock::now()
 │                       ├─ fn(copy)          [algorithms.cpp]
 │                       └─ Clock::now()
 ├─ saveResultsJson(...)                      [benchmarker.cpp]
 └─ release_affinity()                        [cpu_affinity.cpp]
```

For an observe run, `runBenchmarks` + `saveResultsJson` are replaced by
`runObserveMode(args, aff)`.

---

## 7. How we **prove live** that the algorithm runs on one core

A single algorithm run on small data finishes in microseconds — too fast
to see in Task Manager. So the project ships a special mode that runs a
heavy algorithm **in a loop** for a fixed number of seconds, giving the
presenter time to open Task Manager and visually verify the pinning.

### Running the live demo

```powershell
.\algoscope.exe --core 0 --observe --seconds 30 --algo matrix
```

or from the menu (`run_algoscope.ps1`), choose option **4) LIVE single-core
demo (for presentation)**.

### What you will see in the terminal

```
PID            : 23148
Pinned core    : physical 0  → logical CPUs [0, 1]
Algorithm      : matrix
Duration       : 30 seconds

HOW TO SEE IT LIVE (Windows Task Manager):
1) Press Ctrl + Shift + Esc to open Task Manager.
2) Go to the 'Performance' tab → click 'CPU'.
3) Right-click the big CPU graph →
   'Change graph to' → 'Logical processors'.
4) Watch ONLY the pinned logical CPU(s) above hit ~100%;
   every other core graph should stay near idle.
5) For extra proof: open the 'Details' tab, find PID 23148 (algoscope.exe),
   right-click → 'Set affinity'. The dialog will show that only the
   pinned CPU(s) are checked.

⏳  27 s remaining   (iterations completed: 41)
```

### What you (and the teacher) will see in Task Manager

* Performance → CPU graph in **"Logical processors"** view:
  *one* tile (or two adjacent tiles if hyper-threading is on) goes to
  100%, every other tile is flat near 0–5%.
* Details → right-click `algoscope.exe` → **Set affinity**:
  only the pinned CPUs are ticked; all the others are unticked.

That is your visual proof. **No screenshot or wall-clock measurement is
required** — Windows itself is reporting that the process can only run on
those CPUs.

### Why pin **a physical core** instead of "any logical CPU"?

A physical core has its own L1 and L2 cache. Its two hyper-threads share
that cache. Pinning to the physical core (which pins to **both** of its
logical CPUs) keeps the algorithm on the same cache. Pinning to a single
logical CPU works too, but pinning the **physical** core is what
guarantees no cache flush from migration — and it is what real performance
engineers do when they benchmark.

---

## 8. How the regular benchmark mode works (the chart)

```powershell
.\algoscope.exe --core 0 --trials 5 --input random --dataset large
```

This:

1. Detects logical & physical core counts.
2. Pins the process to physical core 0 via
   `SetProcessAffinityMask` ([cpu_affinity.cpp:154](cpu_affinity.cpp#L154)).
3. For each of the 10 algorithms, runs it at every size in the dataset
   profile, taking the median over `--trials` repetitions.
4. Writes everything to `benchmark_results.json`, including a `meta`
   block with `affinity_pinned`, `pinned_logical_cpus`, the OS API used,
   and the before/after affinity-mask values as "evidence".
5. Releases the affinity at the end.

Open `dashboard.html` in any browser, load the JSON, and the chart will
show measured times vs. input size for every algorithm. The "Affinity
Proof" panel at the top reads those `meta` fields back so the proof is
visible inside the dashboard too.

---

## 9. Quick presentation script (≈ 4 minutes)

1. **Introduce the problem (30 s).**
   "When you measure how long an algorithm runs, the OS keeps moving it
   between CPU cores, which throws away the cache and adds noise. My
   project locks the algorithm to one physical core first, then measures
   it, so the timings are reproducible."

2. **Show the live demo (90 s).**
   Open `run_algoscope.ps1`, pick option **4**, enter `0` for the core,
   `30` for seconds, `matrix` for the algorithm.
   Switch to Task Manager → Performance → CPU → Logical processors.
   Point at the one busy graph and the eleven flat ones.
   Switch to Details → `algoscope.exe` → right-click → Set affinity, show
   the dialog: only the two ticked CPUs match the ones printed in the
   terminal.

3. **Show the benchmark (90 s).**
   Run option **2** "Quick benchmark", then option **5** to open the
   dashboard. Walk through the chart: the flat green line is O(1), the
   nearly-flat blue one is O(log n), the steep red one is O(n²) /
   O(n³). Point out the "Affinity Proof" panel — it shows the same
   physical core, same pinned logical CPUs you just saw in Task Manager.

4. **Conclude (30 s).**
   "Because the process never moved cores during the run, the shape of
   each curve is the actual algorithmic complexity, not scheduler noise.
   That's what makes the measurements trustworthy and what the project
   was built to demonstrate."

---

## 10. Files to read in order, if your teacher asks

1. [cpu_affinity.cpp](cpu_affinity.cpp) — the *only* piece that talks to the
   operating system. Function `pin_to_physical_core` at
   [cpu_affinity.cpp:125](cpu_affinity.cpp#L125) is the heart of the project.
2. [algorithms.cpp](algorithms.cpp) — the ten algorithm implementations,
   plain textbook style.
3. [benchmarker.cpp](benchmarker.cpp) — the timing loop and the JSON writer.
4. [main.cpp](main.cpp) — argument parsing, the live demo (`runObserveMode`),
   and the orchestration that calls the above three files in order.

---

## 11. Build & run cheatsheet

```powershell
# Build (needs g++ from MinGW-w64 on PATH)
g++ -std=c++17 -O2 -Wall -Wextra -I. main.cpp benchmarker.cpp algorithms.cpp cpu_affinity.cpp -o algoscope.exe

# Live single-core demo (for presentation)
.\algoscope.exe --core 0 --observe --seconds 30 --algo matrix

# Full benchmark + dashboard
.\algoscope.exe --core 0 --trials 5 --dataset large
start dashboard.html
```

Or simply double-click `run_algoscope.cmd` and use the menu.
