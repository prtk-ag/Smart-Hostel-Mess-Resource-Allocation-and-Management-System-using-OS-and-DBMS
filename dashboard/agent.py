"""SHRM planning agent: forecasts how many students will actually eat and turns
that into a cooking plan.

Honest description: this is a small statistical agent (exponential smoothing +
safety margin + rules), not a language model. It runs fully offline. Every
decision comes with a plain-English reason so it can be explained in a viva.

Loop:  observe (declared absences, history, stock)  ->  forecast  ->  plan
       -> human approves  ->  stock is deducted in one transaction
       -> meal closed with the real diner count  ->  history updated (it learns).
"""
import math
import random
from datetime import date, timedelta

MEALS = ("breakfast", "lunch", "dinner")

# kg (or litres) of each ingredient used per plate
PORTIONS = {
    "breakfast": {"Wheat Flour": 0.06, "Milk": 0.10, "Vegetables": 0.04, "Cooking Oil": 0.010},
    "lunch":     {"Rice": 0.12, "Dal": 0.05, "Vegetables": 0.10, "Cooking Oil": 0.015},
    "dinner":    {"Wheat Flour": 0.09, "Dal": 0.04, "Vegetables": 0.10, "Cooking Oil": 0.015, "Milk": 0.05},
}
# typical share of NOT-declared-absent students who really turn up (used only to build demo history)
BASE_SHOW = {"breakfast": 0.66, "lunch": 0.93, "dinner": 0.86}
DAY_EFFECT = {  # weekday 0=Mon
    "breakfast": [0, 0, 0, 0, -0.02, -0.04, -0.08],
    "lunch": [0, 0, 0, 0, -0.03, -0.05, 0.0],
    "dinner": [0, 0, 0, 0, -0.07, -0.05, 0.02],
}
ALPHA = 0.4          # smoothing weight on the most recent observation
Z = 1.28             # ~90% chance of not running short


def make_history(n_students, days=28, today=None, seed=7):
    """Synthetic past service data for the demo (clearly labelled as synthetic in the UI)."""
    rng = random.Random(seed)
    today = today or date.today()
    rows = []
    for back in range(days, 0, -1):
        d = today - timedelta(days=back)
        for m in MEALS:
            absent = max(0, int(rng.gauss(0.07, 0.025) * n_students))
            rate = BASE_SHOW[m] + DAY_EFFECT[m][d.weekday()] + rng.gauss(0, 0.025)
            rate = min(0.99, max(0.3, rate))
            attended = int(round((n_students - absent) * rate))
            rows.append({"date": d.isoformat(), "weekday": d.weekday(), "meal_type": m,
                         "declared_absent": absent, "attended": attended,
                         "cooked": n_students - absent})   # old habit: cook for everyone not absent
    return rows


def _smooth(values, alpha=ALPHA):
    """Exponential smoothing; values are oldest -> newest."""
    s = values[0]
    for v in values[1:]:
        s = alpha * v + (1 - alpha) * s
    return s


def forecast(history, n_students, declared_absent, meal_type, weekday):
    """history: rows for ALL meals, oldest first. Returns a dict with the forecast and its reasoning."""
    h = [r for r in history if r["meal_type"] == meal_type]
    eligible = n_students - declared_absent
    if len(h) < 4:   # cold start: no history -> cook for everyone who did not opt out
        plates = eligible
        return {"show_rate": None, "expected": eligible, "low": eligible, "high": eligible, "plates": plates,
                "sd": 0, "samples": len(h),
                "reason": "Not enough history yet, so the agent plans for everyone who did not opt out."}
    rates = [r["attended"] / max(1, n_students - r["declared_absent"]) for r in h]
    same_day = [r["attended"] / max(1, n_students - r["declared_absent"]) for r in h if r["weekday"] == weekday]
    overall = _smooth(rates)
    rate = 0.6 * _smooth(same_day) + 0.4 * overall if len(same_day) >= 2 else overall
    # residual spread of the same model on its own history -> uncertainty
    res = []
    for i in range(3, len(h)):
        pr = _smooth(rates[:i])
        res.append((rates[i] - pr) * (n_students - h[i]["declared_absent"]))
    sd = math.sqrt(sum(x * x for x in res) / len(res)) if res else 0.0
    expected = eligible * rate
    plates = min(eligible, math.ceil(expected + Z * sd))
    low, high = max(0, round(expected - Z * sd)), min(eligible, round(expected + Z * sd))
    reason = (f"{declared_absent} of {n_students} students opted out, leaving {eligible}. "
              f"Recent {meal_type} history says about {rate*100:.0f}% of those actually eat "
              f"(weighted toward the same weekday). Expected {expected:.0f} diners, "
              f"+/-{Z*sd:.0f} safety margin, so cook {plates} plates.")
    return {"show_rate": rate, "expected": expected, "low": low, "high": high, "plates": plates,
            "sd": sd, "samples": len(h), "reason": reason}


def ingredient_plan(meal_type, plates, inventory):
    """inventory: list of dict(item_id,item_name,unit,quantity_available,reorder_threshold)."""
    inv = {i["item_name"]: i for i in inventory}
    out = []
    for name, per in PORTIONS[meal_type].items():
        item = inv.get(name)
        if not item:
            continue
        need = round(per * plates, 2)
        after = item["quantity_available"] - need
        if after < 0:
            status = "short"
        elif after < item["reorder_threshold"]:
            status = "low"
        else:
            status = "ok"
        reorder = 0
        if status != "ok":
            reorder = math.ceil(max(0.0, item["reorder_threshold"] * 3 - max(after, 0)))
        out.append({"item_id": item["item_id"], "item": name, "unit": item["unit"], "need": need,
                    "stock": round(item["quantity_available"], 2), "after": round(after, 2),
                    "status": status, "reorder": reorder})
    return out


def backtest(history, n_students, last_days=7):
    """Replay the last `last_days` days: forecast each meal using only data before it.
    Compares the agent with the old habit (cook for everyone who did not opt out)."""
    days = sorted({r["date"] for r in history})
    test_days = set(days[-last_days:])
    agent_err = naive_err = 0.0
    a_waste = n_waste = a_short = n_short = count = 0
    for r in history:
        if r["date"] not in test_days:
            continue
        past = [x for x in history if x["date"] < r["date"]]
        f = forecast(past, n_students, r["declared_absent"], r["meal_type"], r["weekday"])
        naive = n_students - r["declared_absent"]
        agent_err += abs(f["expected"] - r["attended"])
        naive_err += abs(naive - r["attended"])
        a_waste += max(0, f["plates"] - r["attended"]); n_waste += max(0, naive - r["attended"])
        a_short += max(0, r["attended"] - f["plates"]); n_short += max(0, r["attended"] - naive)
        count += 1
    if not count:
        return None
    return {"meals_tested": count,
            "agent_mae": round(agent_err / count, 2), "naive_mae": round(naive_err / count, 2),
            "agent_wasted_plates": a_waste, "naive_wasted_plates": n_waste,
            "agent_short_plates": a_short, "naive_short_plates": n_short}
