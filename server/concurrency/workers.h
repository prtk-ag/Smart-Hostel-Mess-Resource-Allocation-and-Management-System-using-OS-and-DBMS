#pragma once

#include "event_queue.h"
#include <string>

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
void runWorker(EventQueue& queue, const std::string& db_path, int worker_id);
