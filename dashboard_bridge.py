#!/usr/bin/env python3
import json
import os
import subprocess
import threading
import time
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path


ROOT = Path(__file__).resolve().parent
HOST = "127.0.0.1"
PORT = 8765
EXE = ROOT / "algoscope.exe"
JSON_OUT = ROOT / "benchmark_results.json"

lock = threading.Lock()
state = {
    "running": False,
    "last_exit_code": None,
    "last_error": "",
    "last_started_at": None,
    "last_finished_at": None,
    "last_command": "",
    "last_log_tail": [],
}


def read_tail_lines(text, n=60):
    lines = text.splitlines()
    return lines[-n:]


def compile_app():
    cmd = [
        "g++",
        "-std=c++17",
        "-O2",
        "-Wall",
        "-Wextra",
        "-I.",
        "main.cpp",
        "benchmarker.cpp",
        "algorithms.cpp",
        "cpu_affinity.cpp",
        "-o",
        "algoscope.exe",
    ]
    proc = subprocess.run(
        cmd,
        cwd=str(ROOT),
        capture_output=True,
        text=True,
        shell=False,
    )
    out = (proc.stdout or "") + ("\n" if proc.stdout and proc.stderr else "") + (proc.stderr or "")
    return proc.returncode, out


def run_benchmark(args):
    cmd = [str(EXE), "--core", str(args.get("core", 0))]
    cmd += ["--trials", str(args.get("trials", 3))]
    cmd += ["--input", str(args.get("input_kind", "random"))]
    cmd += ["--dataset", str(args.get("dataset", "large"))]
    cmd += ["--out", str(args.get("out", "benchmark_results.json"))]
    if args.get("quiet", False):
        cmd += ["--quiet"]

    with lock:
        state["running"] = True
        state["last_error"] = ""
        state["last_started_at"] = time.time()
        state["last_command"] = " ".join(cmd)
        state["last_log_tail"] = ["Starting benchmark..."]

    proc = subprocess.Popen(
        cmd,
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        shell=False,
    )

    log_lines = []
    for line in iter(proc.stdout.readline, ""):
        line = line.rstrip("\n")
        if not line:
            continue
        log_lines.append(line)
        with lock:
            state["last_log_tail"] = log_lines[-80:]
    proc.wait()

    with lock:
        state["running"] = False
        state["last_exit_code"] = proc.returncode
        state["last_finished_at"] = time.time()
        state["last_log_tail"] = log_lines[-80:]
        if proc.returncode != 0:
            state["last_error"] = "Benchmark process failed."


def start_benchmark_thread(payload):
    t = threading.Thread(target=run_benchmark, args=(payload,), daemon=True)
    t.start()


class Handler(SimpleHTTPRequestHandler):
    def translate_path(self, path):
        if path.startswith("/api/"):
            return super().translate_path(path)
        path = path.split("?", 1)[0].split("#", 1)[0]
        if path == "/":
            return str(ROOT / "dashboard.html")
        return str(ROOT / path.lstrip("/"))

    def _json(self, obj, code=200):
        raw = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        if self.path.startswith("/api/status"):
            with lock:
                return self._json(dict(state))

        if self.path.startswith("/api/results"):
            if not JSON_OUT.exists():
                return self._json({"ok": False, "error": "benchmark_results.json not found"}, 404)
            try:
                data = json.loads(JSON_OUT.read_text(encoding="utf-8"))
                return self._json({"ok": True, "data": data})
            except Exception as exc:
                return self._json({"ok": False, "error": str(exc)}, 500)

        return super().do_GET()

    def do_POST(self):
        if self.path.startswith("/api/build"):
            if state["running"]:
                return self._json({"ok": False, "error": "Benchmark is running."}, 409)
            code, out = compile_app()
            with lock:
                state["last_log_tail"] = read_tail_lines(out, 80)
                state["last_exit_code"] = code
                state["last_error"] = "" if code == 0 else "Build failed."
            return self._json({"ok": code == 0, "exit_code": code, "output_tail": state["last_log_tail"]}, 200 if code == 0 else 500)

        if self.path.startswith("/api/run"):
            if state["running"]:
                return self._json({"ok": False, "error": "Benchmark already running."}, 409)
            try:
                length = int(self.headers.get("Content-Length", "0"))
                payload = json.loads(self.rfile.read(length).decode("utf-8") or "{}")
            except Exception as exc:
                return self._json({"ok": False, "error": f"Invalid JSON: {exc}"}, 400)
            start_benchmark_thread(payload)
            return self._json({"ok": True, "message": "Benchmark started"})

        return self._json({"ok": False, "error": "Unknown API route"}, 404)


def main():
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"Dashboard bridge running at http://{HOST}:{PORT}")
    print("Open that URL in your browser.")
    server.serve_forever()


if __name__ == "__main__":
    main()
