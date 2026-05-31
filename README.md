# AlgoScope (Windows Quick Start)

AlgoScope benchmarks common algorithms and writes results to `benchmark_results.json`, which you can view in `dashboard.html`.

## Easiest way (interactive menu)

### Option 1: Double-click
- Double-click `run_algoscope.cmd`

### Option 2: PowerShell
- Open terminal in this folder and run:
  - `.\run_algoscope.ps1`

You will get a menu with:
- Build project
- Run quick benchmark
- Run custom benchmark (interactive prompts, including dataset size)
- **LIVE single-core demo** (for presentation — see proof in Task Manager)
- Open dashboard in browser
- Start dashboard bridge (full control from browser)
- Clean generated files

> For a full write-up of what the project does, why pinning matters, and
> exactly how to demonstrate it to a teacher, see [EXPLANATION.md](EXPLANATION.md).

## Direct commands (advanced)

- Build:
  - `g++ -std=c++17 -O2 -Wall -Wextra -I. main.cpp benchmarker.cpp algorithms.cpp cpu_affinity.cpp -o algoscope.exe`
- Run quick:
  - `.\algoscope.exe --trials 1 --quiet`
- Run custom example:
  - `.\algoscope.exe --core 0 --trials 5 --input random --dataset large --out benchmark_results.json`
- LIVE single-core demo (for presentation — watch one core hit 100% in Task Manager):
  - `.\algoscope.exe --core 0 --observe --seconds 30 --algo matrix`

## Real-run proof — two ways

**Way 1 — Live, in Task Manager (recommended for presentation):**
Run the demo mode:

```powershell
.\algoscope.exe --core 0 --observe --seconds 30 --algo matrix
```

While it runs (it prints a live countdown):
1. Press `Ctrl + Shift + Esc` to open Task Manager.
2. Go to **Performance → CPU**, right-click the graph →
   **Change graph to → Logical processors**.
3. You should see only the pinned logical CPU(s) hit ~100%; every
   other tile stays near idle.
4. For extra proof, open **Details**, find `algoscope.exe`, right-click
   → **Set affinity**. Only the pinned CPU(s) will be ticked.

**Way 2 — In the dashboard, after a normal benchmark run:**
After loading your JSON in `dashboard.html`, check the **Affinity Proof** panel:
- Pin status (`affinity_pinned`)
- Physical and logical core counts detected
- Pinned logical CPU list
- Affinity API used by your OS
- Affinity evidence string (mask before/requested/after on Windows)
- Dataset profile, total measured points, and run duration

## Control everything from dashboard

To control build/run directly from dashboard:

1. Start the bridge:
   - `python .\dashboard_bridge.py`
   - or double-click `start_dashboard_control.cmd`
2. Open:
   - `http://127.0.0.1:8765`
3. Use the **Run from dashboard (local bridge)** panel to:
   - Build
   - Run benchmark with core/trials/input/dataset
   - Load latest generated JSON
   - Watch live benchmark log output

## Notes

- If PowerShell blocks script execution, run:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\run_algoscope.ps1`
- Requires `g++` available in your `PATH` (MinGW-w64 is fine).
