"""SHRM Control Center: the full demo platform (Python standard library only).

Adds on top of the original dashboard/server.py (which it imports and reuses):
  * Attendance portal  - everyone is PRESENT by default; students mark themselves ABSENT
                         per meal (stored in the `absences` table, UNIQUE per student+meal).
  * Planning agent     - forecasts real diners, proposes a cooking plan, human approves,
                         stock is deducted in ONE transaction (see agent.py).
  * Performance proof  - runs the real C++ benchmark / test programs and shows results.
  * Database view      - tables, indexes, trigger, live foreign-key test.

Run:  python dashboard/app.py          Then open http://127.0.0.1:8080  (SHRM_PORT to change)
"""
import json
import os
import random
import re
import sqlite3
import subprocess
import sys
import threading
from datetime import date, datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import agent                                   # noqa: E402
import server as base                          # noqa: E402  (stream, schedules, race helpers)

ROOT = base.ROOT
APP_DB = ROOT / "shrm_app.db"
SCHEMA = ROOT / "server" / "db" / "schema.sql"
PAGE = HERE / "app.html"
N_STUDENTS = 96
WRITE_LOCK = threading.Lock()

FIRST = ["Aanya", "Rohan", "Isha", "Kabir", "Meera", "Vivaan", "Diya", "Arnav", "Sara", "Aditya", "Nisha", "Yash",
         "Pooja", "Harsh", "Riya", "Karan", "Sneha", "Dev", "Tanvi", "Manav", "Kavya", "Ishaan", "Anjali", "Rahul"]
LAST = ["Sharma", "Verma", "Gupta", "Mehta", "Singh", "Jain", "Patel", "Khurana"]
TEAM = [("Tanishq Agrawal", "B-204"), ("Prateek Agarwal", "B-205"), ("Anant Agarwal", "C-310"),
        ("Arjun Singh Poonia", "C-311")]
INVENTORY = [("Rice", "kg", 60.0, 20.0), ("Wheat Flour", "kg", 45.0, 15.0), ("Dal", "kg", 30.0, 10.0),
             ("Vegetables", "kg", 55.0, 15.0), ("Milk", "l", 40.0, 10.0), ("Cooking Oil", "l", 6.0, 5.0)]
DISHES = {  # meal -> today's dish by weekday (Mon..Sun)
    "breakfast": ["Poha", "Idli Sambhar", "Upma", "Paratha with Curd", "Bread Omelette", "Sandwich", "Chole Bhature"],
    "lunch": ["Rajma Chawal", "Dal Tadka with Rice", "Chole with Rice", "Veg Pulao", "Rice with Sambhar", "Veg Biryani", "Khichdi"],
    "dinner": ["Roti with Mixed Veg", "Paneer Curry with Roti", "Kadhi Chawal", "Roti with Dal Fry",
               "Chana Masala with Roti", "Mix Veg with Roti", "Paneer Butter Masala with Roti"],
}


def connect():
    con = sqlite3.connect(APP_DB, timeout=5, isolation_level=None)
    con.row_factory = sqlite3.Row
    con.execute("PRAGMA foreign_keys = ON")      # per-connection setting in SQLite
    con.execute("PRAGMA busy_timeout = 3000")
    return con


def init_db():
    con = connect()
    con.executescript(SCHEMA.read_text())
    con.executescript("""
      CREATE TABLE IF NOT EXISTS history (
        hist_id INTEGER PRIMARY KEY AUTOINCREMENT, date TEXT NOT NULL, weekday INTEGER NOT NULL,
        meal_type TEXT NOT NULL, declared_absent INTEGER NOT NULL, attended INTEGER NOT NULL,
        cooked INTEGER NOT NULL, source TEXT NOT NULL DEFAULT 'synthetic', UNIQUE(date, meal_type));
      CREATE TABLE IF NOT EXISTS agent_plans (
        plan_id INTEGER PRIMARY KEY AUTOINCREMENT, meal_id INTEGER NOT NULL UNIQUE REFERENCES meals(meal_id),
        plates INTEGER NOT NULL, expected REAL NOT NULL, declared_absent INTEGER NOT NULL,
        approved_at TEXT NOT NULL DEFAULT (datetime('now')), actual_diners INTEGER, waste_plates INTEGER,
        closed_at TEXT);
    """)
    if con.execute("SELECT COUNT(*) FROM students").fetchone()[0] == 0:
        rng = random.Random(11)
        names = list(TEAM)
        used = {n for n, _ in names}
        while len(names) < N_STUDENTS:
            n = f"{rng.choice(FIRST)} {rng.choice(LAST)}"
            if n not in used:
                used.add(n)
                names.append((n, f"{rng.choice('ABCD')}-{rng.randint(1,4)}{rng.randint(0,2)}{rng.randint(1,9)}"))
        con.execute("BEGIN")
        con.executemany("INSERT INTO students(name, room_no) VALUES(?,?)", names)
        con.executemany("INSERT INTO inventory(item_name, unit, quantity_available, reorder_threshold) VALUES(?,?,?,?)", INVENTORY)
        con.commit()
    if con.execute("SELECT COUNT(*) FROM history").fetchone()[0] == 0:
        n = con.execute("SELECT COUNT(*) FROM students").fetchone()[0]
        con.execute("BEGIN")
        con.executemany("INSERT OR IGNORE INTO history(date,weekday,meal_type,declared_absent,attended,cooked) VALUES(?,?,?,?,?,?)",
                        [(r["date"], r["weekday"], r["meal_type"], r["declared_absent"], r["attended"], r["cooked"])
                         for r in agent.make_history(n)])
        con.commit()
    con.close()


def ensure_today(con):
    today = date.today().isoformat()
    for m in agent.MEALS:
        if not con.execute("SELECT 1 FROM meals WHERE date=? AND meal_type=?", (today, m)).fetchone():
            con.execute("INSERT INTO meals(date, meal_type) VALUES(?,?)", (today, m))
    return [dict(r) for r in con.execute("SELECT meal_id, meal_type FROM meals WHERE date=? ORDER BY meal_id", (today,))]


def history_rows(con, before=None):
    q = "SELECT date, weekday, meal_type, declared_absent, attended, cooked FROM history"
    args = ()
    if before:
        q += " WHERE date < ?"
        args = (before,)
    return [dict(r) for r in con.execute(q + " ORDER BY date", args)]


def inventory_rows(con):
    return [dict(r) for r in con.execute(
        "SELECT item_id, item_name, unit, quantity_available, reorder_threshold FROM inventory ORDER BY item_id")]


def meal_overview(con, meal, total, hist, inv):
    mid = meal["meal_id"]
    absent = con.execute("SELECT COUNT(*) FROM absences WHERE meal_id=?", (mid,)).fetchone()[0]
    plan = con.execute("SELECT * FROM agent_plans WHERE meal_id=?", (mid,)).fetchone()
    wd = date.today().weekday()
    fc = agent.forecast(hist, total, absent, meal["meal_type"], wd)
    use = plan["plates"] if plan else fc["plates"]
    ing = agent.ingredient_plan(meal["meal_type"], use, inv if not plan else inventory_rows_before_plan(con, inv, plan, meal["meal_type"]))
    return {"meal_id": mid, "meal_type": meal["meal_type"], "dish": DISHES[meal["meal_type"]][wd],
            "total": total, "declared_absent": absent, "eligible": total - absent,
            "forecast": {k: (round(v, 2) if isinstance(v, float) else v) for k, v in fc.items()},
            "ingredients": ing, "naive_plates": total - absent,
            "plan": dict(plan) if plan else None, "locked": bool(plan)}


def inventory_rows_before_plan(con, inv, plan, meal_type):
    """After approval the stock is already reduced; add it back so the plan view still shows what was needed."""
    add = {n: round(per * plan["plates"], 2) for n, per in agent.PORTIONS[meal_type].items()}
    out = []
    for i in inv:
        j = dict(i)
        j["quantity_available"] = i["quantity_available"] + add.get(i["item_name"], 0)
        out.append(j)
    return out


# ------------------------------------------------------------------ API actions
def api_overview():
    con = connect()
    meals = ensure_today(con)
    total = con.execute("SELECT COUNT(*) FROM students").fetchone()[0]
    hist = history_rows(con)
    inv = inventory_rows(con)
    out = {"date": date.today().isoformat(), "weekday": date.today().strftime("%A"), "total": total,
           "meals": [meal_overview(con, m, total, hist, inv) for m in meals],
           "inventory": inv,
           "alerts": [dict(r) for r in con.execute(
               "SELECT a.alert_id, a.message, a.timestamp FROM alerts a ORDER BY a.alert_id DESC LIMIT 8")],
           "backtest": agent.backtest(hist, total),
           "history": hist[-42:]}
    con.close()
    return out


def api_roster(query=""):
    con = connect()
    meals = ensure_today(con)
    ids = [m["meal_id"] for m in meals]
    locked = {r["meal_id"] for r in con.execute("SELECT meal_id FROM agent_plans")}
    rows = con.execute("SELECT s.student_id, s.name, s.room_no FROM students s "
                       "WHERE s.name LIKE ? OR s.room_no LIKE ? ORDER BY s.name", (f"%{query}%", f"%{query}%")).fetchall()
    absent = {}
    for r in con.execute(f"SELECT student_id, meal_id, reason FROM absences WHERE meal_id IN ({','.join('?'*len(ids))})", ids):
        absent.setdefault(r["student_id"], {})[r["meal_id"]] = r["reason"] or ""
    con.close()
    return {"meals": [{"meal_id": m["meal_id"], "meal_type": m["meal_type"], "locked": m["meal_id"] in locked} for m in meals],
            "students": [{"student_id": r["student_id"], "name": r["name"], "room": r["room_no"],
                          "absent": {str(k): v for k, v in absent.get(r["student_id"], {}).items()}} for r in rows]}


class ApiError(Exception):
    def __init__(self, msg, code=400):
        super().__init__(msg)
        self.code = code


def _check_unlocked(con, meal_id):
    if not con.execute("SELECT 1 FROM meals WHERE meal_id=?", (meal_id,)).fetchone():
        raise ApiError("unknown meal", 404)
    if con.execute("SELECT 1 FROM agent_plans WHERE meal_id=?", (meal_id,)).fetchone():
        raise ApiError("This meal's plan is already approved, so attendance is locked.", 409)


def api_absence(body):
    """Mark one student absent/present for one meal, or for the whole day (meal_id = 'day')."""
    sid = int(body["student_id"])
    absent = bool(body.get("absent", True))
    reason = (body.get("reason") or "").strip()[:80] or None
    with WRITE_LOCK:
        con = connect()
        try:
            meals = ensure_today(con)
            targets = [m["meal_id"] for m in meals] if body.get("meal_id") == "day" else [int(body["meal_id"])]
            con.execute("BEGIN IMMEDIATE")
            for mid in targets:
                _check_unlocked(con, mid)
                if absent:
                    con.execute("INSERT OR IGNORE INTO absences(student_id, meal_id, reason) VALUES(?,?,?)", (sid, mid, reason))
                else:
                    con.execute("DELETE FROM absences WHERE student_id=? AND meal_id=?", (sid, mid))
            con.execute("COMMIT")
        except sqlite3.IntegrityError as e:
            con.execute("ROLLBACK")
            raise ApiError(f"Rejected by the database: {e}")
        except Exception:
            if con.in_transaction:
                con.execute("ROLLBACK")
            raise
        finally:
            con.close()
    return {"ok": True}


def api_absence_bulk(body):
    mid = int(body["meal_id"])
    mode = body.get("mode")
    with WRITE_LOCK:
        con = connect()
        try:
            con.execute("BEGIN IMMEDIATE")
            _check_unlocked(con, mid)
            if mode == "reset":
                con.execute("DELETE FROM absences WHERE meal_id=?", (mid,))
            elif mode == "random":
                n = max(0, min(int(body.get("count", 8)), 60))
                ids = [r[0] for r in con.execute(
                    "SELECT student_id FROM students WHERE student_id NOT IN (SELECT student_id FROM absences WHERE meal_id=?)", (mid,))]
                for sid in random.sample(ids, min(n, len(ids))):
                    con.execute("INSERT OR IGNORE INTO absences(student_id, meal_id, reason) VALUES(?,?,?)", (sid, mid, "demo"))
            else:
                raise ApiError("unknown mode")
            con.execute("COMMIT")
        except Exception:
            if con.in_transaction:
                con.execute("ROLLBACK")
            raise
        finally:
            con.close()
    return {"ok": True}


def api_approve(body):
    """Human approves the agent's plan: deduct stock + log prep in ONE transaction (all or nothing)."""
    mid = int(body["meal_id"])
    with WRITE_LOCK:
        con = connect()
        try:
            total = con.execute("SELECT COUNT(*) FROM students").fetchone()[0]
            meal = con.execute("SELECT meal_id, meal_type FROM meals WHERE meal_id=?", (mid,)).fetchone()
            if not meal:
                raise ApiError("unknown meal", 404)
            con.execute("BEGIN IMMEDIATE")
            _check_unlocked(con, mid)
            absent = con.execute("SELECT COUNT(*) FROM absences WHERE meal_id=?", (mid,)).fetchone()[0]
            fc = agent.forecast(history_rows(con), total, absent, meal["meal_type"], date.today().weekday())
            plates = int(body.get("plates") or fc["plates"])
            if plates < 1 or plates > total:
                raise ApiError("plates out of range")
            plan = agent.ingredient_plan(meal["meal_type"], plates, inventory_rows(con))
            for p in plan:
                cur = con.execute("UPDATE inventory SET quantity_available = quantity_available - ? "
                                  "WHERE item_id=? AND quantity_available >= ?", (p["need"], p["item_id"], p["need"]))
                if cur.rowcount == 0:
                    raise ApiError(f"Not enough {p['item']} in stock ({p['stock']} {p['unit']} left, need {p['need']}). "
                                   "Nothing was changed.", 409)
                con.execute("INSERT INTO meal_prep_logs(meal_id,item_id,quantity_used,prepared_by) VALUES(?,?,?,?)",
                            (mid, p["item_id"], p["need"], "planning-agent"))
            con.execute("INSERT INTO agent_plans(meal_id, plates, expected, declared_absent) VALUES(?,?,?,?)",
                        (mid, plates, fc["expected"], absent))
            con.execute("COMMIT")
        except Exception:
            if con.in_transaction:
                con.execute("ROLLBACK")
            raise
        finally:
            con.close()
    return {"ok": True, "plates": plates}


def api_close(body):
    """Record how many people really ate. Surplus plates become waste_logs; history is updated (the agent learns)."""
    mid = int(body["meal_id"])
    with WRITE_LOCK:
        con = connect()
        try:
            con.execute("BEGIN IMMEDIATE")
            plan = con.execute("SELECT * FROM agent_plans WHERE meal_id=?", (mid,)).fetchone()
            meal = con.execute("SELECT * FROM meals WHERE meal_id=?", (mid,)).fetchone()
            if not plan:
                raise ApiError("Approve the plan before closing the meal.", 409)
            if plan["closed_at"]:
                raise ApiError("This meal is already closed.", 409)
            if body.get("simulate"):
                eligible = con.execute("SELECT COUNT(*) FROM students").fetchone()[0] - plan["declared_absent"]
                rate = agent.BASE_SHOW[meal["meal_type"]] + agent.DAY_EFFECT[meal["meal_type"]][date.today().weekday()]
                actual = max(0, min(eligible, int(round(eligible * (rate + random.gauss(0, 0.025))))))
            else:
                actual = int(body["actual"])
            if actual < 0 or actual > con.execute("SELECT COUNT(*) FROM students").fetchone()[0]:
                raise ApiError("actual diners out of range")
            waste = max(0, plan["plates"] - actual)
            for name, per in agent.PORTIONS[meal["meal_type"]].items():
                row = con.execute("SELECT item_id FROM inventory WHERE item_name=?", (name,)).fetchone()
                if row and waste:
                    con.execute("INSERT INTO waste_logs(meal_id,item_id,quantity_wasted) VALUES(?,?,?)",
                                (mid, row["item_id"], round(per * waste, 2)))
            con.execute("UPDATE agent_plans SET actual_diners=?, waste_plates=?, closed_at=datetime('now') WHERE meal_id=?",
                        (actual, waste, mid))
            con.execute("INSERT OR REPLACE INTO history(date,weekday,meal_type,declared_absent,attended,cooked,source) "
                        "VALUES(?,?,?,?,?,?, 'live')",
                        (meal["date"], date.today().weekday(), meal["meal_type"], plan["declared_absent"], actual, plan["plates"]))
            con.execute("COMMIT")
        except Exception:
            if con.in_transaction:
                con.execute("ROLLBACK")
            raise
        finally:
            con.close()
    return {"ok": True, "actual": actual, "waste_plates": waste}


def api_reset_today():
    """Demo helper: undo today's approvals so the flow can be shown again."""
    with WRITE_LOCK:
        con = connect()
        try:
            con.execute("BEGIN IMMEDIATE")
            today = date.today().isoformat()
            mids = [r[0] for r in con.execute("SELECT meal_id FROM meals WHERE date=?", (today,))]
            q = ",".join("?" * len(mids))
            if mids:
                con.execute(f"DELETE FROM waste_logs WHERE meal_id IN ({q})", mids)
                con.execute(f"DELETE FROM meal_prep_logs WHERE meal_id IN ({q})", mids)
                con.execute(f"DELETE FROM agent_plans WHERE meal_id IN ({q})", mids)
                con.execute(f"DELETE FROM absences WHERE meal_id IN ({q})", mids)
                con.execute("DELETE FROM alerts")
            con.execute("DELETE FROM history WHERE source='live' AND date=?", (today,))
            for (name, unit, qty, thr) in INVENTORY:
                con.execute("UPDATE inventory SET quantity_available=? WHERE item_name=?", (qty, name))
            con.execute("DELETE FROM alerts")
            con.execute("COMMIT")
        except Exception:
            if con.in_transaction:
                con.execute("ROLLBACK")
            raise
        finally:
            con.close()
    return {"ok": True}


def api_db():
    con = connect()
    tables = [{"name": r["name"], "rows": con.execute(f'SELECT COUNT(*) FROM "{r["name"]}"').fetchone()[0]}
              for r in con.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name")]
    idx = [dict(r) for r in con.execute("SELECT name, tbl_name AS tbl FROM sqlite_master WHERE type='index' AND name NOT LIKE 'sqlite_%' ORDER BY name")]
    trg = [dict(r) for r in con.execute("SELECT name, tbl_name AS tbl FROM sqlite_master WHERE type='trigger'")]
    q = "SELECT COUNT(*) FROM absences WHERE meal_id = 1"
    plan = " ; ".join(r["detail"] for r in con.execute("EXPLAIN QUERY PLAN " + q))
    out = {"tables": tables, "indexes": idx, "triggers": trg,
           "foreign_keys": con.execute("PRAGMA foreign_keys").fetchone()[0],
           "integrity": con.execute("PRAGMA integrity_check").fetchone()[0],
           "journal": con.execute("PRAGMA journal_mode").fetchone()[0], "query": q, "plan": plan}
    con.close()
    return out


def api_fk_test():
    """Live proof: try to insert an absence for a student that does not exist."""
    con = connect()
    try:
        con.execute("BEGIN")
        con.execute("INSERT INTO absences(student_id, meal_id) VALUES(999999, 1)")
        con.execute("COMMIT")
        return {"enforced": False, "message": "The database ACCEPTED a row for a student that does not exist."}
    except sqlite3.IntegrityError as e:
        con.execute("ROLLBACK")
        return {"enforced": True, "message": f"Rejected: {e}"}
    finally:
        con.close()


def _run(name, args, timeout):
    exe = base.find_bin(name)
    if not exe:
        return None, f"{name} not built"
    try:
        r = subprocess.run([str(exe)] + args, capture_output=True, text=True, timeout=timeout, cwd=ROOT)
    except subprocess.TimeoutExpired:
        return None, f"{name} timed out"
    return r, None


def api_perf_index():
    r, err = _run("shrm_index_bench", ["--rows", "100000", "--json"], 60)
    return {"error": err} if err else json.loads(r.stdout)


def api_perf_deadlock():
    r, err = _run("shrm_deadlock_demo", [], 60)
    if err:
        return {"error": err}
    return {"passed": r.returncode == 0, "output": r.stdout}


def api_perf_tests():
    res = []
    for name, label in (("shrm_queue_test", "Event queue (threads, exactly-once)"),
                        ("shrm_scheduler_test", "Scheduler (FCFS / SJF / Priority)"),
                        ("shrm_ipc_test", "IPC codec + pipe")):
        r, err = _run(name, [], 120)
        if err:
            res.append({"name": label, "passed": None, "detail": err})
            continue
        lines = [l for l in (r.stdout + r.stderr).strip().splitlines() if l.strip()]
        m = [l for l in lines if re.search(r"\d+\s*/\s*\d+|PASS|FAIL", l)]
        res.append({"name": label, "passed": r.returncode == 0, "detail": (m[-2] if len(m) > 1 else (m or lines or [""])[-1])})
    return {"tests": res}


class Handler(base.Handler):
    def _json(self, fn, *a):
        try:
            self._send(200, json.dumps(fn(*a)))
        except ApiError as e:
            self._send(e.code, json.dumps({"error": str(e)}))
        except (KeyError, ValueError, TypeError) as e:
            self._send(400, json.dumps({"error": f"bad request: {e}"}))
        except sqlite3.Error as e:
            self._send(500, json.dumps({"error": f"database error: {e}"}))

    def do_GET(self):
        path, _, qs = self.path.partition("?")
        if path in ("/", "/index.html"):
            self._send(200, PAGE.read_bytes(), "text/html; charset=utf-8")
        elif path == "/api/overview":
            self._json(api_overview)
        elif path == "/api/roster":
            m = re.search(r"q=([^&]*)", qs)
            from urllib.parse import unquote_plus
            self._json(api_roster, unquote_plus(m.group(1)) if m else "")
        elif path == "/api/db":
            self._json(api_db)
        elif path == "/api/perf/index":
            self._json(api_perf_index)
        elif path == "/api/perf/deadlock":
            self._json(api_perf_deadlock)
        elif path == "/api/perf/tests":
            self._json(api_perf_tests)
        else:
            super().do_GET()

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        try:
            body = json.loads(self.rfile.read(length) or b"{}")
        except ValueError:
            body = {}
        routes = {"/api/absence": api_absence, "/api/absence/bulk": api_absence_bulk, "/api/agent/approve": api_approve,
                  "/api/meal/close": api_close}
        if self.path in routes:
            self._json(routes[self.path], body)
        elif self.path == "/api/reset":
            self._json(api_reset_today)
        elif self.path == "/api/db/fk-test":
            self._json(api_fk_test)
        elif self.path == "/api/stream/start":
            count = max(1, min(int(body.get("count", 300)), 5000))
            delay = max(0, min(int(body.get("delay_ms", 40)), 1000))
            bad = max(0, min(int(body.get("bad", 4)), 50))
            err = base.STREAM.start(count, delay, bad)
            self._send(200 if not err else 409, json.dumps({"error": err} if err else {"ok": True}))
        elif self.path == "/api/stream/stop":
            base.STREAM.stop()
            self._send(200, json.dumps({"ok": True}))
        elif self.path == "/api/race":
            self._send(200, json.dumps(base.run_race()))
        else:
            self._send(404, json.dumps({"error": "not found"}))


if __name__ == "__main__":
    init_db()
    srv = ThreadingHTTPServer(("127.0.0.1", base.PORT), Handler)
    print(f"SHRM Control Center running at http://127.0.0.1:{base.PORT}   (Ctrl+C to stop)")
    missing = [n for n in ("shrm_event_generator", "shrm_ipc_server", "shrm_scheduler_json", "shrm_race_demo",
                           "shrm_index_bench", "shrm_deadlock_demo") if not base.find_bin(n)]
    if missing:
        print("Note: not built yet (some tabs will show an error):", ", ".join(missing))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        base.STREAM.stop()
        print("\nStopped.")
