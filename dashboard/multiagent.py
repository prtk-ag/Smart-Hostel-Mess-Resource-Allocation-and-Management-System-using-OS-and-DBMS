"""SHRM multi-agent system.

Five agents, each running in its OWN THREAD with its own INBOX QUEUE and talking only by
sending messages (the same producer-consumer pattern as the C++ event queue):

   Coordinator  -- starts a round, collects reports, resolves conflicts, publishes the final plan
   Forecaster   -- predicts diners and proposes plates (uses agent.py)
   WasteAnalyst -- reviews the Forecaster's proposal against past over/under-cooking, suggests a trim
   Procurement  -- checks the final plates against stock for ALL meals, drafts reorders
   Anomaly      -- looks for unusual absences and forecast misses

Message flow in one round (numbers are the order you will see in the timeline):
   Coordinator -> Forecaster, WasteAnalyst, Procurement(no), Anomaly       tasks
   Forecaster  -> WasteAnalyst   "review my proposal"
   WasteAnalyst-> Forecaster     "trim N plates"        (agent-to-agent, no coordinator involved)
   Forecaster  -> Procurement    "here are my final plates"
   Forecaster / WasteAnalyst / Procurement / Anomaly -> Coordinator   reports
   Coordinator resolves (e.g. caps plates to what stock allows) -> final plan.

Nothing is applied automatically: a human still approves each meal. Statistical and rule-based
agents only, no language model, fully offline.
"""
import json
import math
import queue
import threading
import time
from statistics import mean, pstdev

import agent as core

AGENTS = ("Coordinator", "Forecaster", "WasteAnalyst", "Procurement", "Anomaly")
ROUND_TIMEOUT = 6.0
_round_lock = threading.Lock()


class Bus:
    """Inboxes + a persisted message log."""

    def __init__(self):
        self.inbox = {a: queue.Queue() for a in AGENTS}
        self.round = None
        self.seq = 0
        self.t0 = 0.0
        self.log_fn = None
        self.messages = []
        self.lock = threading.Lock()

    def send(self, sender, recipient, kind, summary, payload=None, rid=None):
        with self.lock:
            self.seq += 1
            msg = {"seq": self.seq, "round": rid or self.round, "t_ms": round((time.perf_counter() - self.t0) * 1000, 1),
                   "from": sender, "to": recipient, "kind": kind, "summary": summary, "payload": payload or {}}
            self.messages.append(msg)
        if self.log_fn:
            try:
                self.log_fn(msg)
            except Exception:       # logging must never break the agents
                pass
        self.inbox[recipient].put(msg)
        return msg


BUS = Bus()


# ------------------------------------------------------------------ agent behaviours
def forecaster(msg, state):
    ctx = msg["payload"]
    if msg["kind"] == "task_forecast":
        state["ctx"] = ctx
        out = []
        for m in ctx["meals"]:
            f = core.forecast(ctx["history"], ctx["total"], m["declared_absent"], m["meal_type"], ctx["weekday"])
            out.append({"meal_id": m["meal_id"], "meal_type": m["meal_type"], "declared_absent": m["declared_absent"],
                        "expected": round(f["expected"], 1), "sd": round(f["sd"], 2), "base_plates": f["plates"],
                        "eligible": ctx["total"] - m["declared_absent"]})
        state["base"] = out
        BUS.send("Forecaster", "WasteAnalyst", "review_request",
                 "Base plates: " + ", ".join(f"{o['meal_type']} {o['base_plates']}" for o in out) + ". Please review.",
                 {"proposals": out})
    elif msg["kind"] == "trim":
        trims = {t["meal_type"]: t["trim"] for t in msg["payload"]["trims"]}
        final = []
        for o in state["base"]:
            trim = trims.get(o["meal_type"], 0)
            floor_ = math.ceil(o["expected"] + 0.5 * o["sd"])           # never cook below the likely demand
            plates = max(min(floor_, o["eligible"]), min(o["eligible"], o["base_plates"] - trim))
            final.append({**o, "trim": trim, "plates": plates})
        BUS.send("Forecaster", "Procurement", "final_plates",
                 "Final plates after review: " + ", ".join(f"{o['meal_type']} {o['plates']}" for o in final),
                 {"plans": final})
        BUS.send("Forecaster", "Coordinator", "forecast_report", "Forecast and final plates ready.", {"plans": final})


def waste_analyst(msg, state):
    if msg["kind"] == "task_waste":
        state["ctx"] = msg["payload"]
        ctx = msg["payload"]
        h = ctx["history"]
        # lowest-turnout weekday/meal cells (share of non-opted-out students who ate)
        cells = {}
        for r in h:
            cells.setdefault((r["weekday"], r["meal_type"]), []).append(r["attended"] / max(1, ctx["total"] - r["declared_absent"]))
        low = sorted(((mean(v), k) for k, v in cells.items() if len(v) >= 2))[:3]
        days = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
        state["insights"] = [f"{days[k[0]]} {k[1]}: only {v*100:.0f}% of non-opted-out students eat" for v, k in low]
        state["waste_waiting"] = True
    elif msg["kind"] == "review_request":
        ctx = state["ctx"]
        trims = []
        notes = []
        for p in msg["payload"]["proposals"]:
            h = [r for r in ctx["history"] if r["meal_type"] == p["meal_type"]][-14:]
            sur = short = 0
            for i, r in enumerate(h):
                past = [x for x in ctx["history"] if x["date"] < r["date"]]
                f = core.forecast(past, ctx["total"], r["declared_absent"], r["meal_type"], r["weekday"])
                sur += max(0, f["plates"] - r["attended"])
                short += 1 if r["attended"] > f["plates"] else 0
            n = max(1, len(h))
            avg_sur = sur / n
            if short >= 2:
                trim, why = -2, f"ran short {short} times in {n} days, add 2 plates"
            else:
                trim = int(avg_sur * 0.5)
                why = f"over-cooked {avg_sur:.1f} plates on average over {n} days, trim {trim}"
            trims.append({"meal_type": p["meal_type"], "trim": trim, "why": why})
            notes.append(f"{p['meal_type']}: {why}")
        BUS.send("WasteAnalyst", "Forecaster", "trim", "Suggested trims: " + "; ".join(notes), {"trims": trims})
        BUS.send("WasteAnalyst", "Coordinator", "waste_report", "Waste review done: " + "; ".join(notes),
                 {"trims": trims, "insights": state["insights"]})


def procurement(msg, state):
    if msg["kind"] == "task_stock":
        state["ctx"] = msg["payload"]
    elif msg["kind"] == "final_plates":
        ctx = state["ctx"]
        stock = {i["item_name"]: i["quantity_available"] for i in ctx["inventory"]}
        info = {i["item_name"]: i for i in ctx["inventory"]}
        need_total = {n: 0.0 for n in stock}
        per_meal = []
        for p in sorted(msg["payload"]["plans"], key=lambda x: core.MEALS.index(x["meal_type"])):
            shortages, feasible = [], p["plates"]
            for name, per in core.PORTIONS[p["meal_type"]].items():
                if name not in stock:
                    continue
                need = per * p["plates"]
                if stock[name] < need - 1e-9:
                    shortages.append(name)
                    feasible = min(feasible, int(stock[name] / per))
                    need_alloc = max(0.0, stock[name])
                else:
                    need_alloc = need
                stock[name] -= need_alloc
                need_total[name] += need_alloc
            per_meal.append({"meal_type": p["meal_type"], "plates": p["plates"], "max_feasible": max(0, feasible),
                             "short_items": shortages})
        # days of stock left and reorder list
        daily = {n: 0.0 for n in stock}
        days_in_hist = max(1, len({r["date"] for r in ctx["history"][-21:]}))
        for r in ctx["history"][-21:]:
            for name, per in core.PORTIONS[r["meal_type"]].items():
                daily[name] = daily.get(name, 0.0) + per * r["cooked"] / days_in_hist
        orders, days_left = [], []
        for name, i in info.items():
            after = stock[name]
            use = max(daily.get(name, 0.0), 0.01)
            left = round(after / use, 1)
            days_left.append({"item": name, "unit": i["unit"], "after_today": round(after, 2), "days_left": left})
            if after < i["reorder_threshold"] or left < 3:
                qty = math.ceil(max(0.0, use * 4 + i["reorder_threshold"] - after))
                orders.append({"item": name, "unit": i["unit"], "qty": qty, "after_today": round(after, 2),
                               "reason": "below reorder level after today's plans" if after < i["reorder_threshold"]
                               else f"only {left} days of stock left"})
        short = [m for m in per_meal if m["short_items"]]
        BUS.send("Procurement", "Coordinator", "procurement_report",
                 (f"Stock cannot cover {', '.join(m['meal_type'] for m in short)}." if short else "Stock covers all planned meals.")
                 + (f" Drafted {len(orders)} reorder(s)." if orders else ""),
                 {"per_meal": per_meal, "orders": orders, "days_left": days_left})


def anomaly(msg, state):
    if msg["kind"] != "task_scan":
        return
    ctx = msg["payload"]
    flags = []
    for m in ctx["meals"]:
        hist = [r["declared_absent"] for r in ctx["history"] if r["meal_type"] == m["meal_type"]]
        if len(hist) >= 5:
            mu, sd = mean(hist), max(pstdev(hist), 1.0)
            z = (m["declared_absent"] - mu) / sd
            if z >= 2.5:      # only unusually MANY opt-outs matter; few opt-outs are normal
                flags.append({"meal_type": m["meal_type"], "severity": "high" if abs(z) >= 4 else "medium",
                              "text": f"{m['declared_absent']} students opted out of {m['meal_type']}; usual is about {mu:.0f} (z = {z:+.1f}). Verify before cooking."})
    for c in ctx["closed"]:
        miss = c["actual"] - c["expected"]
        if abs(miss) > max(6, 0.1 * c["expected"]):
            flags.append({"meal_type": c["meal_type"], "severity": "medium",
                          "text": f"Earlier {c['meal_type']} forecast missed by {miss:+.0f} diners (expected {c['expected']:.0f}, actual {c['actual']})."})
    below = [i["item_name"] for i in ctx["inventory"] if i["quantity_available"] < i["reorder_threshold"]]
    if below:
        flags.append({"meal_type": None, "severity": "medium", "text": "Already below reorder level: " + ", ".join(below)})
    BUS.send("Anomaly", "Coordinator", "anomaly_report",
             f"{len(flags)} anomaly flag(s)." if flags else "Nothing unusual found.", {"flags": flags})


HANDLERS = {"Forecaster": forecaster, "WasteAnalyst": waste_analyst, "Procurement": procurement, "Anomaly": anomaly}
_threads_started = False


def _agent_loop(name):
    state = {}
    while True:
        msg = BUS.inbox[name].get()
        if msg is None:
            return
        if msg["kind"] == "reset":
            state.clear()
            continue
        try:
            HANDLERS[name](msg, state)
        except Exception as exc:            # an agent crash is reported, not fatal
            BUS.send(name, "Coordinator", "error", f"{name} failed: {exc}", {"error": str(exc)})


def start():
    global _threads_started
    if _threads_started:
        return
    _threads_started = True
    for n in HANDLERS:
        threading.Thread(target=_agent_loop, args=(n,), name=f"agent-{n}", daemon=True).start()


# ------------------------------------------------------------------ coordinator
def run_round(snapshot, rid, log_fn=None):
    """snapshot: {total, weekday, meals:[{meal_id,meal_type,declared_absent}], history, inventory, closed}"""
    start()
    with _round_lock:
        BUS.round, BUS.seq, BUS.t0, BUS.messages, BUS.log_fn = rid, 0, time.perf_counter(), [], log_fn
        for n in HANDLERS:
            BUS.inbox[n].put({"kind": "reset"})
        co = BUS.inbox["Coordinator"]
        while not co.empty():
            co.get_nowait()
        base = {"total": snapshot["total"], "weekday": snapshot["weekday"], "meals": snapshot["meals"],
                "history": snapshot["history"], "inventory": snapshot["inventory"]}
        BUS.send("Coordinator", "WasteAnalyst", "task_waste", "Review waste history; be ready to review the Forecaster.", base)
        BUS.send("Coordinator", "Procurement", "task_stock", "Check stock against the final plates when they arrive.", base)
        BUS.send("Coordinator", "Anomaly", "task_scan", "Scan today's absences and earlier forecast misses.",
                 {**base, "closed": snapshot["closed"]})
        # Forecaster goes LAST: it immediately messages WasteAnalyst, which must already hold its task.
        BUS.send("Coordinator", "Forecaster", "task_forecast", "Forecast diners and propose plates for today's open meals.", base)
        need = {"forecast_report", "waste_report", "procurement_report", "anomaly_report"}
        got, errors = {}, []
        deadline = time.perf_counter() + ROUND_TIMEOUT
        while need - set(got) and time.perf_counter() < deadline:
            try:
                m = co.get(timeout=max(0.05, deadline - time.perf_counter()))
            except queue.Empty:
                break
            if m["kind"] == "error":
                errors.append(m["summary"])
                break
            if m["kind"] in need:
                got[m["kind"]] = m["payload"]
        return _resolve(snapshot, got, errors)


def _resolve(snapshot, got, errors):
    if not {"forecast_report", "procurement_report"} <= set(got):
        BUS.send("Coordinator", "Coordinator", "timeout", "Not all agents reported in time." + (" " + "; ".join(errors) if errors else ""))
        return {"error": "Some agents did not report. " + "; ".join(errors), "messages": BUS.messages}
    plans = {p["meal_type"]: p for p in got["forecast_report"]["plans"]}
    proc = {p["meal_type"]: p for p in got["procurement_report"]["per_meal"]}
    flags = got.get("anomaly_report", {}).get("flags", [])
    meals = []
    for m in snapshot["meals"]:
        mt = m["meal_type"]
        p, pr = plans[mt], proc[mt]
        plates, notes, status = p["plates"], [], "ready"
        if p["trim"]:
            notes.append(f"Waste analyst: {'trimmed' if p['trim'] > 0 else 'added'} {abs(p['trim'])} plate(s) vs the base forecast of {p['base_plates']}.")
        if pr["short_items"]:
            plates = pr["max_feasible"]
            status = "capped"
            notes.append(f"Procurement: not enough {', '.join(pr['short_items'])}. Plates capped from {p['plates']} to {plates}.")
            if plates < p["expected"]:
                status = "at_risk"
                notes.append(f"At risk: only {plates} plates against about {p['expected']:.0f} expected diners. Order stock urgently.")
        for f in flags:
            if f["meal_type"] == mt and f["severity"] in ("high", "medium"):
                status = "review" if status == "ready" else status
                notes.append("Anomaly: " + f["text"])
        meals.append({"meal_id": m["meal_id"], "meal_type": mt, "eligible": p["eligible"], "declared_absent": p["declared_absent"],
                      "expected": p["expected"], "base_plates": p["base_plates"], "trim": p["trim"],
                      "plates": plates, "status": status, "notes": notes})
    summary = ("; ".join(f"{x['meal_type']} {x['plates']} plates ({x['status']})" for x in meals))
    BUS.send("Coordinator", "Coordinator", "final_plan", "Final plan: " + summary, {"meals": meals})
    return {"meals": meals, "orders": got["procurement_report"]["orders"], "days_left": got["procurement_report"]["days_left"],
            "anomalies": flags, "insights": got.get("waste_report", {}).get("insights", []),
            "messages": BUS.messages, "duration_ms": BUS.messages[-1]["t_ms"] if BUS.messages else 0}
