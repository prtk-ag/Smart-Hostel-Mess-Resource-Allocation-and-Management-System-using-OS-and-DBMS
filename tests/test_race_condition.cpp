// Phase 4/5 deliverable: reproduces the classic lost-update race on
// purpose, then shows the exact same concurrent workload run through
// the transactional path from Phase 1 and NOT losing anything.
//
// The workload: N threads each call a prep-logging function
// `iterations` times on the SAME inventory item, each deducting a fixed
// amount. If every deduction lands, the final quantity is exactly
// (initial - N*iterations*amount). Run 1 uses DbManager::logPrepUnsafe
// (read, sleep, write -- no transaction); run 2 uses the real
// DbManager::logPrep (wrapped in BEGIN IMMEDIATE/COMMIT). Comparing the
// two final quantities against the expected value is the whole demo.
//
// Usage: ./shrm_race_demo [threads] [iterations_per_thread]

#include "db_manager.h"
#include "paths.h"
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

namespace {

// Runs `iterations` deductions of `amount` on `item_id`, split evenly
// across `num_threads` threads, using whichever function is passed in
// via `use_safe_path` -- so the exact same workload driver exercises
// both the unsafe and the safe path. Each thread opens its OWN
// connection to `db_path`: logPrep's BEGIN IMMEDIATE/COMMIT is only
// safely concurrent across separate connections (SQLite's busy_timeout
// then makes a second connection's BEGIN wait for the first's COMMIT
// via real file locking); threads sharing one connection would instead
// get an immediate "transaction within a transaction" error the moment
// two BEGINs overlap, since transaction state lives on the connection.
double runWorkload(const std::string& db_path, int meal_id, int item_id, int num_threads,
                    int iterations_per_thread, double amount, bool use_safe_path) {
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t] {
            DbManager db(db_path); // one connection per thread
            for (int i = 0; i < iterations_per_thread; ++i) {
                std::string who = "thread-" + std::to_string(t);
                if (use_safe_path) {
                    db.logPrep(meal_id, item_id, amount, who);
                } else {
                    db.logPrepUnsafe(meal_id, item_id, amount, who);
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    DbManager reader(db_path);
    auto item = reader.getInventoryItem(item_id);
    return item->quantity_available;
}

} // namespace

int main(int argc, char** argv) {
    const int num_threads = (argc > 1) ? std::stoi(argv[1]) : 10;
    const int iterations_per_thread = (argc > 2) ? std::stoi(argv[2]) : 20;
    const double amount = 1.0;
    const double initial_qty = 1'000'000.0; // deliberately huge -- this demo is about
                                             // lost updates, not the rollback path,
                                             // so stock should never run out

    const int total_deductions = num_threads * iterations_per_thread;
    const double expected_final = initial_qty - total_deductions * amount;

    std::cout << "SHRM Phase 4/5 -- race condition demo (unsafe vs transactional)\n";
    std::cout << "===================================================================\n";
    std::cout << "Threads: " << num_threads
              << "  Iterations/thread: " << iterations_per_thread
              << "  Deduction each: " << amount << "\n";
    std::cout << "Starting stock: " << initial_qty
              << "  Expected final (if nothing is lost): " << expected_final << "\n\n";

    // --- Run 1: the unsafe path -- expect this to FAIL -----------------
    {
        std::remove("shrm_race_unsafe.db");
        DbManager db("shrm_race_unsafe.db");
        db.initSchema(schemaPath());
        int item_id = db.addInventoryItem("Rice", "kg", initial_qty, 0.0);
        int meal_id = db.createMeal("2026-01-01", "lunch");

        double final_qty = runWorkload("shrm_race_unsafe.db", meal_id, item_id, num_threads,
                                        iterations_per_thread, amount, /*use_safe_path=*/false);
        double lost = final_qty - expected_final; // positive = deductions went missing

        std::cout << std::fixed << std::setprecision(1);
        std::cout << "-- Run 1: logPrepUnsafe (no transaction) --\n";
        std::cout << "  Final stock:    " << final_qty << "\n";
        std::cout << "  Expected stock: " << expected_final << "\n";
        std::cout << "  Lost updates:   " << (lost / amount) << " deduction(s) worth "
                  << lost << " kg\n";
        std::cout << "  Result: " << (lost > 0.0 ? "RACE REPRODUCED -- updates were lost, as expected.\n"
                                                  : "no loss this run (race is timing-dependent -- rerun,\n"
                                                    "  or increase thread/iteration counts, to reproduce it).\n");
    }

    std::cout << "\n";

    // --- Run 2: the real, transactional path -- expect this to PASS ----
    {
        std::remove("shrm_race_safe.db");
        DbManager db("shrm_race_safe.db");
        db.initSchema(schemaPath());
        int item_id = db.addInventoryItem("Rice", "kg", initial_qty, 0.0);
        int meal_id = db.createMeal("2026-01-01", "lunch");

        double final_qty = runWorkload("shrm_race_safe.db", meal_id, item_id, num_threads,
                                        iterations_per_thread, amount, /*use_safe_path=*/true);
        bool exact = (final_qty == expected_final);

        std::cout << "-- Run 2: logPrep (BEGIN IMMEDIATE transaction) --\n";
        std::cout << "  Final stock:    " << final_qty << "\n";
        std::cout << "  Expected stock: " << expected_final << "\n";
        std::cout << "  Result: " << (exact ? "PASS -- every deduction landed, nothing lost.\n"
                                             : "FAIL -- unexpected mismatch, investigate.\n");
    }

    return 0;
}
