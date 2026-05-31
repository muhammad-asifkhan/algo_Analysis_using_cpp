param(
    [switch]$NoPause
)

$ErrorActionPreference = "Stop"

Set-Location -Path $PSScriptRoot

function Write-Title {
    Clear-Host
    Write-Host "==============================================" -ForegroundColor Cyan
    Write-Host "  AlgoScope - Windows Build and Run Helper" -ForegroundColor Cyan
    Write-Host "==============================================" -ForegroundColor Cyan
    Write-Host ""
}

function Test-Gpp {
    $cmd = Get-Command g++ -ErrorAction SilentlyContinue
    if (-not $cmd) {
        Write-Host "g++ not found in PATH." -ForegroundColor Red
        Write-Host "Install MinGW-w64 and reopen terminal." -ForegroundColor Yellow
        return $false
    }
    return $true
}

function Build-App {
    if (-not (Test-Gpp)) { return $false }

    $sources = @(
        "main.cpp",
        "benchmarker.cpp",
        "algorithms.cpp",
        "cpu_affinity.cpp"
    )

    Write-Host "Building algoscope.exe ..." -ForegroundColor Yellow
    & g++ -std=c++17 -O2 -Wall -Wextra -I. $sources -o "algoscope.exe"

    if ($LASTEXITCODE -eq 0 -and (Test-Path ".\algoscope.exe")) {
        Write-Host "Build successful: algoscope.exe" -ForegroundColor Green
        return $true
    }

    Write-Host "Build failed." -ForegroundColor Red
    return $false
}

function Run-Quick {
    if (-not (Test-Path ".\algoscope.exe")) {
        Write-Host "Executable not found. Building first..." -ForegroundColor Yellow
        if (-not (Build-App)) { return }
    }

    Write-Host "Running quick benchmark (--dataset standard --trials 1 --quiet)..." -ForegroundColor Yellow
    & ".\algoscope.exe" --dataset standard --trials 1 --quiet --no-pause
}

function Run-Custom {
    if (-not (Test-Path ".\algoscope.exe")) {
        Write-Host "Executable not found. Building first..." -ForegroundColor Yellow
        if (-not (Build-App)) { return }
    }

    Write-Host ""
    $core = Read-Host "Physical core index (default 0)"
    if ([string]::IsNullOrWhiteSpace($core)) { $core = "0" }

    $trials = Read-Host "Trials per size (default 3)"
    if ([string]::IsNullOrWhiteSpace($trials)) { $trials = "3" }

    Write-Host "Input type options: random, sorted, reverse"
    $input = Read-Host "Input type (default random)"
    if ([string]::IsNullOrWhiteSpace($input)) { $input = "random" }

    $outfile = Read-Host "Output JSON file (default benchmark_results.json)"
    if ([string]::IsNullOrWhiteSpace($outfile)) { $outfile = "benchmark_results.json" }

    Write-Host "Dataset options: small, standard, large, xlarge"
    $dataset = Read-Host "Dataset profile (default large)"
    if ([string]::IsNullOrWhiteSpace($dataset)) { $dataset = "large" }

    $quietChoice = Read-Host "Quiet mode? (y/N)"
    $args = @("--core", $core, "--trials", $trials, "--input", $input, "--dataset", $dataset, "--out", $outfile, "--no-pause")
    if ($quietChoice -match "^(y|Y)$") {
        $args += "--quiet"
    }

    Write-Host ""
    Write-Host "Running: .\algoscope.exe $($args -join ' ')" -ForegroundColor Yellow
    & ".\algoscope.exe" @args
}

function Run-LiveDemo {
    if (-not (Test-Path ".\algoscope.exe")) {
        Write-Host "Executable not found. Building first..." -ForegroundColor Yellow
        if (-not (Build-App)) { return }
    }

    Write-Host ""
    Write-Host "LIVE SINGLE-CORE DEMO — for teacher presentation" -ForegroundColor Cyan
    Write-Host "This runs a CPU-heavy algorithm in a loop so you can OPEN"
    Write-Host "Task Manager and watch ONE core spike to 100% while others"
    Write-Host "stay idle. The PID is printed so you can also verify"
    Write-Host "affinity via Task Manager > Details > Set affinity."
    Write-Host ""

    $core = Read-Host "Physical core index to pin (default 0)"
    if ([string]::IsNullOrWhiteSpace($core)) { $core = "0" }

    $seconds = Read-Host "Duration in seconds (default 30)"
    if ([string]::IsNullOrWhiteSpace($seconds)) { $seconds = "30" }

    Write-Host "Algorithm options: matrix (O(n^3)), bubble (O(n^2))"
    $algo = Read-Host "Algorithm (default matrix)"
    if ([string]::IsNullOrWhiteSpace($algo)) { $algo = "matrix" }

    Write-Host ""
    Write-Host ">>> Open Task Manager NOW (Ctrl+Shift+Esc), Performance > CPU," -ForegroundColor Yellow
    Write-Host ">>> right-click graph > Change graph to > Logical processors." -ForegroundColor Yellow
    Write-Host ""

    & ".\algoscope.exe" --core $core --observe --seconds $seconds --algo $algo --no-pause
}

function Open-Dashboard {
    if (Test-Path ".\dashboard.html") {
        Start-Process ".\dashboard.html"
        Write-Host "Opened dashboard.html in your browser." -ForegroundColor Green
    } else {
        Write-Host "dashboard.html not found." -ForegroundColor Red
    }
}

function Start-DashboardBridge {
    $py = Get-Command python -ErrorAction SilentlyContinue
    if (-not $py) {
        Write-Host "python not found in PATH." -ForegroundColor Red
        Write-Host "Install Python 3 and run: python .\dashboard_bridge.py" -ForegroundColor Yellow
        return
    }
    Write-Host "Starting local dashboard bridge at http://127.0.0.1:8765" -ForegroundColor Yellow
    Start-Process python -ArgumentList ".\dashboard_bridge.py" -WorkingDirectory $PSScriptRoot
}

function Clean-Artifacts {
    $targets = @("algoscope.exe", "benchmark_results.json")
    foreach ($t in $targets) {
        if (Test-Path $t) {
            Remove-Item $t -Force
            Write-Host "Removed $t" -ForegroundColor Yellow
        }
    }
    Write-Host "Clean complete." -ForegroundColor Green
}

function Pause-IfNeeded {
    if (-not $NoPause) {
        Write-Host ""
        Read-Host "Press Enter to continue"
    }
}

while ($true) {
    Write-Title
    Write-Host "1) Build project"
    Write-Host "2) Run quick benchmark"
    Write-Host "3) Run custom benchmark (interactive)"
    Write-Host "4) LIVE single-core demo (for presentation)" -ForegroundColor Cyan
    Write-Host "5) Open dashboard"
    Write-Host "6) Start dashboard bridge (control from browser)"
    Write-Host "7) Clean generated files"
    Write-Host "0) Exit"
    Write-Host ""

    $choice = Read-Host "Choose an option"
    switch ($choice) {
        "1" { Build-App | Out-Null; Pause-IfNeeded }
        "2" { Run-Quick; Pause-IfNeeded }
        "3" { Run-Custom; Pause-IfNeeded }
        "4" { Run-LiveDemo; Pause-IfNeeded }
        "5" { Open-Dashboard; Pause-IfNeeded }
        "6" { Start-DashboardBridge; Pause-IfNeeded }
        "7" { Clean-Artifacts; Pause-IfNeeded }
        "0" { break }
        default {
            Write-Host "Invalid choice: $choice" -ForegroundColor Red
            Pause-IfNeeded
        }
    }
}
