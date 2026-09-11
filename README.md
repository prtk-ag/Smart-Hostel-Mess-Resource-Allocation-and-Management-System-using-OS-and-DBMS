# Smart-Hostel-Mess-Resource-Allocation-and-Management-System-using-OS-and-DBMS

A software system for hostel mess inventory, attendance-driven demand, and waste
tracking, built to demonstrate real OS and DBMS concepts (concurrency, scheduling,
IPC, deadlock handling, transactions, locking, triggers, indexing) rather than as
four separate syllabus-topic demos bolted together.

## Current status: Phase 1 — schema + transactional CRUD

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
- `server/main.cpp` — a runnable demo that exercises all of the above: a normal
  prep transaction, a prep that pushes stock below threshold (trigger fires), and
  a prep that's deliberately too large (transaction rolls back).

Not implemented yet (see roadmap below): multithreading, the scheduler, IPC, the
race-condition/deadlock demos, and the event generator process.

## Building

Requires CMake, a C++17 compiler, and `libsqlite3-dev`.

```bash
# one-time setup (Ubuntu/Debian)
sudo apt-get install cmake g++ libsqlite3-dev

mkdir build && cd build
cmake ..
make
./shrm_server
```

This creates `shrm.db` in the working directory and prints inventory/alert state
as it runs through the demo scenarios.

## Project structure

```
shrm/
├── server/
│   ├── main.cpp              # Phase 1 demo entry point
│   ├── db/
│   │   ├── schema.sql        # table definitions, trigger, indexes
│   │   └── db_manager.cpp    # SQLite wrapper + transactional logPrep()
│   └── include/
│       └── db_manager.h
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
| 1 | ✅ Schema + transactional CRUD | This commit. Builds, runs, trigger + rollback demo work. |
| 2 | Attendance & menu CRUD polish, seed data script | A `seed.sql` or small seeding function with realistic data (~5–10 students, a week's menu) so later phases have something to schedule/forecast against. |
| 3 | Producer-consumer threading (part 1) | Thread-safe event queue class (`server/concurrency/event_queue.h/.cpp`) with mutex + condition variable, unit-tested with a couple of producer/consumer threads pushing dummy events. |
| 4 | Producer-consumer threading (part 2) | Wire real worker threads (attendance/prep/waste) into the queue, writing to the DB via `DbManager`. Multiple threads calling `logPrep` on the *same* item — reproduce the lost-update race on purpose. |
| 5 | Fix the race condition | Add row-level locking (the `BEGIN IMMEDIATE` groundwork is already in `logPrep`) and show before/after: race reproduced, then fixed. This is one of your four report-highlight experiments. |
| 6 | Scheduler (part 1) | `Job` struct (arrival time, burst estimate, priority) + FCFS implementation with waiting/turnaround time metrics printed to console. |
| 7 | Scheduler (part 2) | Add SJF and Priority scheduling, same metrics, and a small comparison table/printout across all three. |
| 8 | IPC — event generator process | Split `sensor_simulator/` into its own binary, generating synthetic attendance/waste/stock events and writing them to a named pipe (`mkfifo`). Main server reads from the pipe instead of hardcoded demo calls. |
| 9 | Deadlock demo | Construct the two-thread, two-lock deadlock on purpose (`tests/test_deadlock.cpp`), show it hang, then fix with consistent lock ordering. |
| 10 | Indexing experiment + polish | Seed a larger synthetic dataset (~5,000 rows, matching the scale used in the reference papers), measure a hot query before/after the existing indexes, capture the numbers for the report. |
| 11+ | File logging, CLI polish, report writeup | Concurrent-safe file logger, tidy up the demo CLI, start writing the final report using the four experiment results as the centerpiece. |

Adjust pacing as needed — the important thing for the mentor-facing commit
history is that each 2-day commit corresponds to one working, demonstrable
piece, not partial/broken code.
