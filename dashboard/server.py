"""SHRM dashboard: a tiny local web server (Python standard library only).

It does NOT reimplement anything. It runs the real C++ programs and shows
their results:
  * "Start stream"  -> shrm_event_generator | shrm_ipc_server  (two processes, one pipe)
                       and reads the SQLite database they write.
  * Scheduling tab  -> shrm_scheduler_json (the C++ FCFS / SJF / Priority code)
  * Race tab        -> shrm_race_demo

Run from anywhere:   python dashboard/server.py      (Windows: py dashboard\\server.py)
Then open:           http://127.0.0.1:8080
"""
import json
import os
import re
import sqlite3
import subprocess
import sys
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PAGE = Path(__file__).resolve().parent / "index.html"
DB_PATH = ROOT / "shrm_dashboard.db"
EXE = ".exe" if os.name == "nt" else ""
PORT = int(os.environ.get("SHRM_PORT", "8080"))


def find_bin(name):
    """Looks in build/ (Linux, MinGW) and build/Release|Debug (Visual Studio)."""
    for sub in ("build", "build/Release", "build/Debug", "build/RelWithDebInfo"):
        p = ROOT / sub / (name + EXE)
        if p.exists():
            return p
    return None


class Stream:
    """Owns the generator | server pipeline and the last lines of its log."""

    def __init__(self):
        self.lock = threading.Lock()
        self.server = None
        self.generator = None
        self.log = deque(maxlen=60)
        self.started_at = None
        self.finished_at = None

    def running(self):
        return self.server is not None and self.server.poll() is None

    def _pump(self, proc, tag):
        for line in proc.stderr:
            self.log.append(f"[{tag}] {line.rstrip()}")

    def start(self, count, delay_ms, bad):
        with self.lock:
            if self.running():
                return "already running"
            gen_bin, srv_bin = find_bin("shrm_event_generator"), find_bin("shrm_ipc_server")
            if not gen_bin or not srv_bin:
                return "programs not built yet - run the build first (see README)"
            self.log.clear()
            self.finished_at = None
            self.server = subprocess.Popen(
                [str(srv_bin), "--db", str(DB_PATH), "--workers", "3"],
                stdin=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT)
            self.generator = subprocess.Popen(
                [str(gen_bin), "--count", str(count), "--delay-ms", str(delay_ms), "--bad", str(bad),
                 "--seed", str(int(time.time()) % 100000)],
                stdout=self.server.stdin, stderr=subprocess.PIPE, text=True, cwd=ROOT)
            self.server.stdin.close()  # parent's copy; the generator keeps the real write end
            for proc, tag in ((self.server, "server"), (self.generator, "generator")):
                threading.Thread(target=self._pump, args=(proc, tag), daemon=True).start()
            self.started_at = time.time()
            threading.Thread(target=self._watch, daemon=True).start()
        return None

    def _watch(self):
        self.server.wait()
        self.finished_at = time.time()

    def stop(self):
        with self.lock:
            for p in (self.generator, self.server):
                if p is not None and p.poll() is None:
                    p.terminate()
            self.finished_at = time.time()


STREAM = Stream()


def read_state():
    state = {"db": False, "inventory": [], "alerts": [], "counts": {}, "by_meal": []}
    if DB_PATH.exists():
        try:
            con = sqlite3.connect(f"file:{DB_PATH}?mode=ro", uri=True, timeout=2)
            con.row_factory = sqlite3.Row
            state["db"] = True
            state["inventory"] = [dict(r) for r in con.execute(
                "SELECT item_id, item_name, unit, quantity_available, reorder_threshold FROM inventory ORDER BY item_id")]
            state["alerts"] = [dict(r) for r in con.execute(
                "SELECT alert_id, item_id, message, timestamp FROM alerts ORDER BY alert_id DESC LIMIT 8")]
            state["counts"] = {
                "attendance": con.execute("SELECT COUNT(*) FROM attendance").fetchone()[0],
                "prep": con.execute("SELECT COUNT(*) FROM meal_prep_logs").fetchone()[0],
                "waste": con.execute("SELECT COUNT(*) FROM waste_logs").fetchone()[0],
                "alerts": con.execute("SELECT COUNT(*) FROM alerts").fetchone()[0],
                "waste_qty": con.execute("SELECT COALESCE(SUM(quantity_wasted),0) FROM waste_logs").fetchone()[0],
            }
            state["by_meal"] = [dict(r) for r in con.execute(
                "SELECT m.meal_type AS meal, COUNT(a.attendance_id) AS n FROM meals m "
                "LEFT JOIN attendance a ON a.meal_id = m.meal_id GROUP BY m.meal_id ORDER BY m.meal_id")]
            con.close()
        except sqlite3.Error as exc:  # DB being re-created by a new run; next poll will work
            state["error"] = str(exc)
    state["stream"] = {
        "running": STREAM.running(),
        "log": list(STREAM.log),
        "elapsed": round((STREAM.finished_at or time.time()) - STREAM.started_at, 1) if STREAM.started_at else 0,
    }
    return state


def run_schedules():
    exe = find_bin("shrm_scheduler_json")
    if not exe:
        return {"error": "shrm_scheduler_json not built"}
    out = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30, cwd=ROOT)
    return json.loads(out.stdout)


def run_race(threads=10, iters=20):
    exe = find_bin("shrm_race_demo")
    if not exe:
        return {"error": "shrm_race_demo not built"}
    try:
        out = subprocess.run([str(exe), str(threads), str(iters)], capture_output=True, text=True,
                             timeout=90, cwd=ROOT)
    except subprocess.TimeoutExpired:
        return {"error": "demo timed out"}
    text = out.stdout
    finals = [float(x) for x in re.findall(r"Final stock:\s+([\d.]+)", text)]
    expected = [float(x) for x in re.findall(r"Expected stock:\s+([\d.]+)", text)]
    if len(finals) < 2 or len(expected) < 2:
        return {"error": "demo did not finish (try again)", "output": text[-600:] + out.stderr[-300:]}
    total = threads * iters
    return {"threads": threads, "iterations": iters, "total": total,
            "unsafe_lost": round(finals[0] - expected[0]), "safe_lost": round(finals[1] - expected[1]),
            "output": text}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _send(self, code, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self._send(200, PAGE.read_bytes(), "text/html; charset=utf-8")
        elif self.path == "/api/state":
            self._send(200, json.dumps(read_state()))
        elif self.path == "/api/schedules":
            self._send(200, json.dumps(run_schedules()))
        else:
            self._send(404, json.dumps({"error": "not found"}))

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        try:
            body = json.loads(self.rfile.read(length) or b"{}")
        except ValueError:
            body = {}
        if self.path == "/api/stream/start":
            count = max(1, min(int(body.get("count", 300)), 5000))
            delay = max(0, min(int(body.get("delay_ms", 40)), 1000))
            bad = max(0, min(int(body.get("bad", 4)), 50))
            err = STREAM.start(count, delay, bad)
            self._send(200 if not err else 409, json.dumps({"error": err} if err else {"ok": True}))
        elif self.path == "/api/stream/stop":
            STREAM.stop()
            self._send(200, json.dumps({"ok": True}))
        elif self.path == "/api/race":
            self._send(200, json.dumps(run_race()))
        else:
            self._send(404, json.dumps({"error": "not found"}))


if __name__ == "__main__":
    missing = [n for n in ("shrm_event_generator", "shrm_ipc_server", "shrm_scheduler_json", "shrm_race_demo")
               if not find_bin(n)]
    if missing:
        print("Not built yet:", ", ".join(missing), "\nBuild the project first (see README), then run this again.")
        sys.exit(1)
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)  # localhost only
    print(f"SHRM dashboard running at http://127.0.0.1:{PORT}   (Ctrl+C to stop)")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        STREAM.stop()
        print("\nStopped.")
