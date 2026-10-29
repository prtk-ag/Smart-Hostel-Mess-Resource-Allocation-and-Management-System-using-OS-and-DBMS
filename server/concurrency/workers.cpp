#include "workers.h"
#include "db_manager.h"
#include <iostream>

// IMPORTANT: each worker opens its own connection to the same DB file,
// rather than being handed a shared DbManager. logPrep's BEGIN
// IMMEDIATE/COMMIT pair is only safe under concurrency when different
// threads use different connections -- SQLite's busy_timeout then makes
// a second connection's BEGIN IMMEDIATE wait for the first connection's
// COMMIT, via real file-level locking. Two threads sharing ONE
// connection instead get an immediate "cannot start a transaction
// within a transaction" error the moment their BEGINs overlap, because
// transaction state lives on the connection, not per-thread -- we hit
// exactly that bug during development before switching to this design.
void runWorker(EventQueue& queue, const std::string& db_path, int worker_id, WorkerStats* stats) {
    DbManager db(db_path);

    Event e;
    while (queue.pop(e)) {
        try {
            switch (e.type) {
                case Event::Type::Attendance:
                    db.logAttendance(e.student_id, e.meal_id);
                    break;
                case Event::Type::Prep:
                    db.logPrep(e.meal_id, e.item_id, e.quantity, e.prepared_by);
                    break;
                case Event::Type::Waste:
                    db.logWaste(e.meal_id, e.item_id, e.quantity);
                    break;
            }
            if (stats) ++stats->applied;
        } catch (const std::exception& ex) {
            // Deliberately non-fatal: a single rejected event (e.g.
            // insufficient stock) shouldn't stop the worker from
            // processing the rest of the queue.
            if (stats) ++stats->rejected;
            std::cerr << "[worker " << worker_id << "] event failed: " << ex.what() << "\n";
        }
    }
}
