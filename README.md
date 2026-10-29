# SHRM — Smart Hostel Mess Resource Manager

A software system for hostel mess inventory, attendance-driven demand, and waste
tracking, built to demonstrate real OS and DBMS concepts (concurrency, scheduling,
IPC, deadlock handling, transactions, locking, triggers, indexing) rather than as
four separate syllabus-topic demos bolted together.

See `docs/` (or the project proposal PDF) for the full architecture writeup.

## SHRM Control Center (the demo platform)

`python3 dashboard/app.py` (set `SHRM_PORT=8090` if 8080 is taken) then open the printed address.
Start on **Home**: a live thali-tray view of today's three meals, a **Run the guided demo** button that plays the whole flow
(fresh day, students opt out, agents plan, kitchen approves, service, results) in about 15 seconds, and a table that maps every
course concept to the tab that shows it. The **Student portal** is the student's view: pick a student and tap "I'm not eating" per meal.
Tabs:
Overview, **Attendance** (everyone present by default; mark yourself absent per meal or for the day,
stored in the `absences` table with a UNIQUE(student, meal) constraint, locked once a meal's plan is
approved), **AI planning agent**, Live pipeline, Scheduling, Concurrency (race + deadlock), Performance
(index benchmark, test programs, scheduler, throughput) and Database (tables, indexes, trigger, live foreign-key test).

**The planning agent** (`dashboard/agent.py`) is a statistical agent, not an LLM, and works offline: it
forecasts real diners from opt-outs and the historical no-show rate (exponential smoothing, weighted
toward the same weekday, with a 90% safety margin), turns that into plates and ingredient needs, checks
stock, and waits for a human to approve. Approval deducts stock and logs prep in ONE transaction
(`BEGIN IMMEDIATE`, all or nothing; the low-stock trigger fires on its own). Closing a meal with the real
diner count writes waste logs and adds the day to history, so the agent learns. Past history is
synthetic demo data; the 7-day replay compares the agent with the old habit of cooking for everyone.
**Multi-agent tab** (`dashboard/multiagent.py`): five agents (Coordinator, Forecaster, Waste analyst,
Procurement, Anomaly), each in its own thread with its own inbox queue (the same producer-consumer
pattern as the C++ `EventQueue`), communicating only by messages that are also saved to the
`agent_messages` table. The Forecaster proposes plates, the Waste analyst reviews them and sends back a
trim (agent to agent), the Forecaster sends final plates to Procurement, which checks stock for all meals
and drafts reorders, while the Anomaly agent flags unusual opt-outs and forecast misses. The Coordinator
resolves conflicts (for example capping plates to what stock allows) and a human approves each meal.
Try it: Attendance, "Mark 40 absent" on lunch, then Multi-agent, "Run the agents".

App data lives in `shrm_app.db` (delete it to start fresh). The original `dashboard/server.py` still works on its own.

## Current status: Phases 1–10 complete

- **Phase 9 (deadlock):** `shrm_deadlock_demo` builds the classic two-lock deadlock on purpose
  (worker A: inventory→alerts, worker B: alerts→inventory), detects it with timed lock attempts,
  then shows two fixes: a global lock order (inventory always first) and back-off (timed try,
  release, retry). Passed 12/12 repeated runs.
- **Phase 10 (indexing):** `shrm_index_bench` loads 200,000 attendance rows and times the same
  queries with and without `idx_attendance_meal_student`. Typical result: full table SCAN
  about 1.8 s per 300 lookups vs index SEARCH about 1 ms (over 1,000x faster); results are checked identical.
  Run `shrm_index_bench --rows 5000` for the paper-scale dataset.
- **Foreign keys fix:** `PRAGMA foreign_keys = ON` now runs in the `DbManager` constructor, so every
  worker connection enforces them (before, only the schema-loading connection did).

## Phase 8 — IPC: separate event-generator process + pipe (on top of the Phase 4/5 concurrency work)

What's implemented right now:
- Normalized schema (`server/db/schema.sql`): students, inventory, meals, menu,
  attendance, meal_prep_logs, waste_logs, alerts.
- A low-stock **trigger** that fires automatically when an inventory update drops
  a quantity below its reorder threshold.
- Indexes on the hot query paths (`meal_prep_logs.meal_id`,
  `attendance(meal_id, student_id)`, `inventory.item_name`).
- `DbManager::logPrep()` — the core **transactional** operation: deducts stock and
  logs the prep event atomically, rolling back cleanly on insufficient stock
  (see `InsufficientStockError`).
- `server/main.cpp` — a runnable demo that exercises all of the above.
- `server/db/seed_data.cpp` + `server/seed_main.cpp` — a `shrm_seed` CLI that
  populates a fresh DB with students, inventory, a week's menu, and today's
  meals with attendance already logged.
- `server/concurrency/event_queue.h/.cpp` — a thread-safe `EventQueue`
  (mutex + condition variable) carrying `Event` structs between producer and
  consumer threads, verified with an exact per-event delivery test and
  ThreadSanitizer.
- **New in Phase 4:** `server/concurrency/workers.h/.cpp` — `runWorker()`, the
  real consumer side of the queue: pops events and applies them to the
  database via `DbManager::logAttendance/logPrep/logWaste`.
  `server/pipeline_demo.cpp` (`shrm_pipeline_demo`) wires it all together —
  producer threads pushing a mix of attendance/prep/waste events, worker
  threads draining the queue into the DB, concurrently.
- **New in Phase 4/5:** `DbManager::logPrepUnsafe()` — a deliberately naive
  read-modify-write version of `logPrep` with no transaction, built purely to
  demonstrate the lost-update race on purpose. `tests/test_race_condition.cpp`
  (`shrm_race_demo`) runs the identical concurrent workload through both
  paths: the unsafe path reliably loses updates (**180 lost out of 200
  deductions** at 10 threads × 20 iterations, scaling to **1,450 lost out of
  1,500** at 30×50 — reproduced consistently across repeated runs), while the
  transactional path lands every single deduction exactly, every time. This
  is the race-condition experiment the proposal calls for.
- **A real concurrency bug we hit and fixed along the way** (worth keeping in
  the report as-is — it's a genuine finding, not staged): the first version of
  the worker threads shared one `DbManager`/SQLite connection across threads.
  `BEGIN`/`COMMIT` pairs are connection-scoped in SQLite, so two threads
  overlapping their `BEGIN IMMEDIATE` calls on the *same* connection fail
  immediately with "cannot start a transaction within a transaction" — SQLite
  doesn't queue that the way it queues separate connections contending for a
  file lock. The fix was giving each worker thread its own connection to the
  same DB file; `busy_timeout` (already set in Phase 1) then makes SQLite's
  real file-level locking serialize the transactions correctly, which is what
  the race demo above relies on.

- **New in Phase 8:** the first real **inter-process** communication. A separate
  program, `shrm_event_generator`, invents attendance/prep/waste events and
  sends them as text lines over a pipe to `shrm_ipc_server`, which validates
  every line (`server/ipc/event_codec.*` — strict parser, rejects garbage),
  pushes good ones onto the Phase 3 queue, and lets the Phase 4/5 workers
  write them to SQLite. Two channel types behind one interface
  (`server/ipc/channel.*`): `stdio` (generator `|` server — identical on
  every OS) and `pipe:NAME` (a **named pipe**: `mkfifo` FIFO on Linux/macOS,
  `CreateNamedPipe` on Windows, since Windows has no `mkfifo`). Start order
  doesn't matter. Checked: 300 events + 4 malformed lines → 304 received, 4
  rejected, 300 applied, DB rows match the generator's own counts exactly
  (122 attendance, 99 prep, 79 waste). `shrm_ipc_test` has 82 checks (codec
  round trip, 23 bad inputs rejected, 500 lines through a real pipe in
  order); two deliberate codec bugs were caught.
  **Windows:** the Windows named-pipe code is written against the Win32 API
  but has not been compiled yet (no Windows machine here) — the stdio pipe
  path is the safe fallback. Also new: CMake finds SQLite without pkg-config
  (or downloads it), and programs find `schema.sql` from any folder.

- **Phase 7:** `scheduleSJF()` and `schedulePriority()` (both
  non-preemptive, ties broken by arrival then id) share one engine and the same
  `buildResult()` metrics as FCFS. `shrm_scheduler_demo` now runs all three on
  the same lunch workload and prints a comparison table. Results (minutes):

  | Algorithm | Avg wait | Avg turnaround | Makespan |
  |---|---|---|---|
  | FCFS | 66.67 | 89.67 | 138 |
  | SJF | 38.17 | 61.17 | 138 |
  | Priority | 63.00 | 86.00 | 138 |

  Same makespan everywhere (only the order changes); SJF wins on average wait
  but can starve long dishes, Priority can starve low-priority ones. Tests grew
  to 47 checks: textbook SJF/Priority cases, non-preemption, idle gaps, the
  hand-worked lunch numbers, 500 random workloads checking the selection rule
  itself, and SJF optimality when all jobs arrive together. Three deliberate
  bugs (reversed SJF, reversed Priority, off-by-one arrival check) were all
  caught (two by failed checks, one by an infinite loop).

- **Phase 6:** `server/scheduler/` — a `Job` model (arrival time, burst
  time, priority), a `ScheduleResult` type, and `scheduleFCFS()`: first-come,
  first-served, non-preemptive, ties broken by job id, with the station
  correctly sitting idle when the next job hasn't arrived yet. Metrics
  (waiting, turnaround, response time, makespan, idle time) are computed in
  one shared `buildResult()` so every algorithm added later is measured by
  the same definitions. `shrm_scheduler_demo` runs it on a six-dish lunch
  prep workload and prints a metrics table plus a Gantt chart.
  `tests/test_scheduler.cpp` (`shrm_scheduler_test`) checks FCFS against
  answers worked out by hand (the textbook 0/24/27 example, staggered
  arrivals, idle gaps), plus invariants over 500 random workloads — and was
  confirmed able to fail: deliberately breaking FCFS made 6 checks fail and
  flagged 451 of the 500 random workloads.

Not implemented yet (see roadmap below): SJF and Priority scheduling, IPC,
the event generator process, and the deadlock demo.

## Dashboard (browser interface)

A small local web page that runs the real C++ programs and shows what they do:
live inventory, alerts and record counts while the generator streams events
through the pipe into the database; the FCFS / SJF / Priority Gantt charts; and
a before/after bar chart of the race condition. Needs only Python 3 (no pip
installs) and the project built first.

```bash
python3 dashboard/app.py          # Windows: py dashboard\app.py   (or run_dashboard.bat)
# then open http://127.0.0.1:8080
```

It listens on localhost only. Press **Start stream** on the first tab and watch
the numbers move; when the oil or dal runs low the low-stock trigger fires and
workers log "Not enough ..." for requests the database correctly refuses.

## Building on Windows

Easiest: install **CMake** and **Visual Studio Build Tools** (workload "Desktop
development with C++"), open the project folder in a normal terminal, then:

```bat
build_windows.bat
run_ipc_demo.bat
```

Or with MSYS2/MinGW: `pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,sqlite3}` and
use the same CMake commands. Or WSL, where every Linux command above works
unchanged. The executables end up in `build\Release\` (Visual Studio) or
`build\` (MinGW) and can be run from any folder.

## Building

Requires CMake, a C++17 compiler, and `libsqlite3-dev`.

```bash
# one-time setup (Ubuntu/Debian)
sudo apt-get install cmake g++ libsqlite3-dev

mkdir build && cd build
cmake ..
make
cd ..                       # binaries load relative paths (schema.sql), so
./build/shrm_server           # run everything from the project root, not build/
./build/shrm_seed
./build/shrm_queue_test       # optional args: [producers] [events/producer] [consumers]
./build/shrm_pipeline_demo    # queue + worker threads + DB, wired together
./build/shrm_race_demo        # optional args: [threads] [iterations/thread]
./build/shrm_scheduler_demo   # FCFS/SJF/Priority tables, Gantt charts, comparison (no database needed)
./build/shrm_scheduler_test   # all three checked against hand-computed answers
./build/shrm_ipc_test         # codec + named-pipe channel tests
./build/shrm_event_generator --count 300 --bad 4 | ./build/shrm_ipc_server   # two processes, one pipe
# named pipe version (two terminals):
#   ./build/shrm_ipc_server --in pipe:demo
#   ./build/shrm_event_generator --count 300 --out pipe:demo
```

`shrm_server` creates `shrm.db` and prints inventory/alert state as it runs
through the transaction/rollback demo. `shrm_seed` creates (or overwrites)
`shrm_seed.db` with the full sample dataset described above; it's safe to
re-run — it deletes the target file first rather than erroring on the
`inventory.item_name UNIQUE` constraint. `shrm_queue_test` touches no
database — it's a standalone correctness check for the concurrency queue.
`shrm_pipeline_demo` seeds `shrm_pipeline.db` and runs the full producer →
queue → worker → DB pipeline. `shrm_race_demo` creates
`shrm_race_unsafe.db`/`shrm_race_safe.db` and prints the lost-update
comparison described above.

## Project structure

```
shrm/
├── server/
│   ├── main.cpp              # Phase 1 demo entry point (shrm_server)
│   ├── seed_main.cpp         # Phase 2 seed CLI entry point (shrm_seed)
│   ├── pipeline_demo.cpp     # Phase 4 full-pipeline demo (shrm_pipeline_demo)
│   ├── db/
│   │   ├── schema.sql        # table definitions, trigger, indexes
│   │   ├── db_manager.cpp    # SQLite wrapper: transactional logPrep() +
│   │   │                     # deliberately unsafe logPrepUnsafe() for the race demo
│   │   └── seed_data.cpp     # sample students/inventory/menu/meals/attendance
│   ├── scheduler/
│   │   ├── job.h             # Job / JobResult / ScheduleResult
│   │   └── scheduler.h/.cpp  # scheduleFCFS/SJF/Priority(), shared buildResult(), Gantt printer
│   ├── scheduler_demo.cpp    # Phase 6/7 demo (shrm_scheduler_demo)
│   ├── ipc/                  # Phase 8: event_codec, channel (stdio / named pipe, POSIX + Win32)
│   ├── event_generator.cpp   # Phase 8 generator process (shrm_event_generator)
│   ├── ipc_server.cpp        # Phase 8 server process (shrm_ipc_server)
│   ├── concurrency/
│   │   ├── event_queue.h/.cpp  # Event struct + thread-safe EventQueue
│   │   └── workers.h/.cpp      # runWorker() -- the real queue consumer
│   └── include/
│       ├── db_manager.h
│       └── seed_data.h
├── tests/
│   ├── test_event_queue.cpp     # Phase 3 correctness check (shrm_queue_test)
│   ├── test_race_condition.cpp  # Phase 4/5 race demo (shrm_race_demo)
│   └── test_scheduler.cpp       # Phase 6/7 scheduler tests (shrm_scheduler_test)
├── dashboard/                # browser interface: app.py + app.html (Control Center), agent.py (planning agent), server.py + index.html (original, reused)
├── CMakeLists.txt
└── README.md
```

As later phases land, this grows into the structure from the proposal:
`server/concurrency/`, `server/scheduler/`, `server/ipc/`
(the event generator), and `tests/` for the race-condition and deadlock demos.

## Roadmap (2-day update cadence)

Each block below is sized to be a realistic 2-day chunk of work and a
meaningful commit — update the repo at the end of each one.

| # | Focus | What "done" looks like |
|---|---|---|
| 1 | ✅ Schema + transactional CRUD | Phase 1 commit. Builds, runs, trigger + rollback demo work. |
| 2 | ✅ Seed data script | Phase 2 commit. `shrm_seed` populates 8 students, 6 inventory items, a week's menu, and today's meals + attendance. |
| 3 | ✅ Producer-consumer threading (part 1) | Phase 3 commit. Thread-safe `EventQueue`, verified with multi-producer/multi-consumer tests and ThreadSanitizer. |
| 4 | ✅ Producer-consumer threading (part 2) | This commit. Real worker threads (`runWorker`) draining the queue into the DB via `DbManager`; `shrm_pipeline_demo` proves the full pipeline end to end. |
| 5 | ✅ Fix the race condition | This commit (bundled with #4 — they're the same experiment). `shrm_race_demo` reproduces the lost-update race with `logPrepUnsafe` (180/200 deductions lost at 10×20 threads), then shows `logPrep`'s transaction losing zero, every run. Also fixed a real bug hit along the way: worker threads need one SQLite connection *each*, not a shared one — see the README section above. |
| 6 | ✅ Scheduler (part 1) | `Job` model + FCFS with waiting/turnaround/response metrics, Gantt chart output, and tests checked against hand-computed answers. |
| 7 | ✅ Scheduler (part 2) | SJF + Priority on a shared engine, side-by-side comparison with FCFS, 47 tests. |
| 8 | ✅ IPC | This commit. Separate event-generator process → pipe (stdio or named pipe, POSIX + Windows) → IPC server → queue → workers → DB. Strict line codec, 82 tests. |
| 9 | ✅ Deadlock demo (`shrm_deadlock_demo`) | Construct the two-thread, two-lock deadlock on purpose (`tests/test_deadlock.cpp`), show it hang, then fix with consistent lock ordering. |
| 10 | ✅ Indexing experiment (`shrm_index_bench`) | Seed a larger synthetic dataset (~5,000 rows, matching the scale used in the reference papers), measure a hot query before/after the existing indexes, capture the numbers for the report. |
| 11+ | File logging, CLI polish, report writeup | Concurrent-safe file logger, tidy up the demo CLI, start writing the final report using the four experiment results as the centerpiece. |

Adjust pacing as needed — the important thing for the mentor-facing commit
history is that each 2-day commit corresponds to one working, demonstrable
piece, not partial/broken code.

## Team & role assignments

| Phase | What | Owner | Status |
|---|---|---|---|
| 1 | DB schema, transactions, trigger, indexing | Prateek Agarwal | ✅ Done |
| 2 | Seed data script | Prateek Agarwal | ✅ Done |
| 3 | Thread-safe producer-consumer queue | Anant Agarwal | ✅ Done |
| 4/5 | Real worker threads + race condition (reproduce & fix) | Anant Agarwal (DB/transaction side: Prateek Agarwal) | ✅ Done |
| 6 | Scheduler part 1 — FCFS | Arjun Singh Poonia | ✅ Done |
| 7 | Scheduler part 2 — SJF, Priority, comparison | Arjun Singh Poonia | ✅ Done |
| 8 | IPC — event generator process, named pipe | Arjun Singh Poonia | ✅ Done |
| 9 | Deadlock demo (construct → fix) | Anant Agarwal | ⬜ |
| 10 | Indexing experiment at scale (~5,000 rows) | Prateek Agarwal | ⬜ |
| 11+ | File logging, CLI polish, integration, final report | Tanishq Agrawal (Team Lead) | ⬜ |

**Why it's split this way:**
- **Prateek — DBMS layer** (1, 2, 10): schema, transactions, seed data, indexing.
  Phase 10 extends work already owned rather than starting a new subsystem.
- **Anant — Concurrency** (3, 4/5, 9): the queue, worker threads, the race
  condition, and now the deadlock demo all reuse the same threading/locking
  primitives, so one owner avoids two people re-learning the same subsystem.
- **Arjun — Scheduler + IPC** (6, 7, 8): fully self-contained, no dependency
  on the DB or concurrency code already merged, so this can run fully in
  parallel with Anant's deadlock work.
- **Tanishq — Integration + report** (11+): stays out of individual modules
  to focus on merging branches, keeping the build green, and assembling the
  four headline experiments (race condition, deadlock, scheduling comparison,
  indexing) into the final report and demo script.

**Workflow:** one branch per owner (`scheduler-ipc`, `deadlock-demo`,
`indexing-experiment`), merged into `main` every 2 days. Whoever merges runs
a full build check first — all executables listed under Building above must
still compile and pass before the merge lands.
