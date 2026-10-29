# SHRM — Smart Hostel Mess Resource Manager

A software system for hostel mess inventory, attendance-driven demand, and waste
tracking, built to demonstrate real OS and DBMS concepts (concurrency, scheduling,
IPC, deadlock handling, transactions, locking, triggers, indexing) rather than as
four separate syllabus-topic demos bolted together.

See `docs/` (or the project proposal PDF) for the full architecture writeup.

## Current status: Phase 3 — thread-safe producer-consumer event queue

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
- `server/db/seed_data.cpp` + `server/seed_main.cpp` — a `shrm_seed` CLI that
  populates a fresh DB with 8 students, a stocked inventory (6 items), a full
  week's menu (21 entries), and today's three meals with attendance already
  logged.
- **New in Phase 3:** `server/concurrency/event_queue.h/.cpp` — a thread-safe
  FIFO queue (`EventQueue`) built on `std::mutex` + `std::condition_variable`,
  carrying `Event` structs (attendance/prep/waste) between producer and
  consumer threads. `shutdown()` wakes every blocked consumer once producers
  are done, so worker threads can be `join()`ed cleanly instead of hanging.
- `tests/test_event_queue.cpp` (`shrm_queue_test`) — spins up several producer
  threads and several consumer threads, and checks that every one of a known
  number of uniquely-numbered events is delivered **exactly once** (not just
  that the counts match — an exact per-event delivery check, which would catch
  a lost-and-duplicated pair that a simple count comparison would miss).
  Verified passing under several producer/consumer/load combinations, and
  additionally compiled and run under **ThreadSanitizer** with no data-race
  warnings.

Not implemented yet (see roadmap below): real DB-writing worker threads on top
of this queue, the scheduler, IPC, the race-condition/deadlock demos, and the
event generator process.

## Building

Requires CMake, a C++17 compiler, and `libsqlite3-dev`.

```bash
# one-time setup (Ubuntu/Debian)
sudo apt-get install cmake g++ libsqlite3-dev

mkdir build && cd build
cmake ..
make
cd ..                     # binaries load relative paths (schema.sql), so
./build/shrm_server        # run everything from the project root, not build/
./build/shrm_seed
./build/shrm_queue_test    # optional args: [producers] [events/producer] [consumers]
```

`shrm_server` creates `shrm.db` and prints inventory/alert state as it runs
through the transaction/rollback demo. `shrm_seed` creates (or overwrites)
`shrm_seed.db` with the full sample dataset described above; it's safe to
re-run — it deletes the target file first rather than erroring on the
`inventory.item_name UNIQUE` constraint. Pass a path to seed somewhere else:
`./build/shrm_seed path/to/other.db`. `shrm_queue_test` touches no database —
it's a standalone correctness check for the concurrency queue, and prints
PASS/FAIL based on whether every event was delivered exactly once.

## Project structure

```
shrm/
├── server/
│   ├── main.cpp              # Phase 1 demo entry point (shrm_server)
│   ├── seed_main.cpp         # Phase 2 seed CLI entry point (shrm_seed)
│   ├── db/
│   │   ├── schema.sql        # table definitions, trigger, indexes
│   │   ├── db_manager.cpp    # SQLite wrapper + transactional logPrep()
│   │   └── seed_data.cpp     # sample students/inventory/menu/meals/attendance
│   ├── concurrency/
│   │   ├── event_queue.h     # Event struct + thread-safe EventQueue
│   │   └── event_queue.cpp
│   └── include/
│       ├── db_manager.h
│       └── seed_data.h
├── tests/
│   └── test_event_queue.cpp  # Phase 3 correctness check (shrm_queue_test)
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
| 3 | ✅ Producer-consumer threading (part 1) | This commit. Thread-safe `EventQueue` (mutex + condition variable), verified with multi-producer/multi-consumer tests (exact per-event delivery check) and ThreadSanitizer — no lost/duplicated events, no data races. |
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
