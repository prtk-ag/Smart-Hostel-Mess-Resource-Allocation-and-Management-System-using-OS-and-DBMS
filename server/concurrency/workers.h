#pragma once

#include "event_queue.h"
#include <atomic>
#include <string>

// Optional counters shared by all workers (Phase 8: the IPC server reports them).
struct WorkerStats {
    std::atomic<int> applied{0};   // events committed to the database
    std::atomic<int> rejected{0};  // events the DB refused (e.g. insufficient stock)
};

// Runs on its own thread: opens its OWN SQLite connection to `db_path`
// (deliberately not sharing one connection across threads -- see the
// comment in workers.cpp for why that matters), pops events off `queue`
// and applies them via that connection, until the queue is shut down
// and drained (see EventQueue::pop).
//
// Any DbManager exception (e.g. InsufficientStockError from logPrep)
// is caught and logged to stderr rather than crashing the worker --
// one bad event shouldn't take down the thread processing everything
// after it.
void runWorker(EventQueue& queue, const std::string& db_path, int worker_id, WorkerStats* stats = nullptr);
