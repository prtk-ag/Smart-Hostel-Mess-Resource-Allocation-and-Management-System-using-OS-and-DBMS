# SHRM — Smart Hostel Mess Resource Manager

A software system for hostel mess inventory, attendance-driven demand, and waste
tracking, built to demonstrate real OS and DBMS concepts (concurrency, scheduling,
IPC, deadlock handling, transactions, locking, triggers, indexing) rather than as
four separate syllabus-topic demos bolted together.

See `docs/` (or the project proposal PDF) for the full architecture writeup.

## Current status: Phase 4/5 — real worker threads on the queue, race condition reproduced and fixed

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

Not implemented yet (see roadmap below): the scheduler, IPC, the event
generator process, and the deadlock demo.

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
│   ├── concurrency/
│   │   ├── event_queue.h/.cpp  # Event struct + thread-safe EventQueue
│   │   └── workers.h/.cpp      # runWorker() -- the real queue consumer
│   └── include/
│       ├── db_manager.h
│       └── seed_data.h
├── tests/
│   ├── test_event_queue.cpp     # Phase 3 correctness check (shrm_queue_test)
│   └── test_race_condition.cpp  # Phase 4/5 race demo (shrm_race_demo)
├── CMakeLists.txt
└── README.md
```

As later phases land, this grows into the structure from the proposal:
`server/concurrency/`, `server/scheduler/`, `server/ipc/`, `sensor_simulator/`
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
| 6 | Scheduler (part 1) | `Job` struct (arrival time, burst estimate, priority) + FCFS implementation with waiting/turnaround time metrics printed to console. |
| 7 | Scheduler (part 2) | Add SJF and Priority scheduling, same metrics, and a small comparison table/printout across all three. |
| 8 | IPC — event generator process | Split `sensor_simulator/` into its own binary, generating synthetic attendance/waste/stock events and writing them to a named pipe (`mkfifo`). Main server reads from the pipe instead of hardcoded demo calls. |
| 9 | Deadlock demo | Construct the two-thread, two-lock deadlock on purpose (`tests/test_deadlock.cpp`), show it hang, then fix with consistent lock ordering. |
| 10 | Indexing experiment + polish | Seed a larger synthetic dataset (~5,000 rows, matching the scale used in the reference papers), measure a hot query before/after the existing indexes, capture the numbers for the report. |
| 11+ | File logging, CLI polish, report writeup | Concurrent-safe file logger, tidy up the demo CLI, start writing the final report using the four experiment results as the centerpiece. |

Adjust pacing as needed — the important thing for the mentor-facing commit
history is that each 2-day commit corresponds to one working, demonstrable
piece, not partial/broken code.
