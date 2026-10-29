// Phase 4 (part 2) deliverable: the Phase 3 queue is no longer just
// tested in isolation -- real worker threads now pop from it and write
// to the database through DbManager, and producer threads push a mix
// of attendance/prep/waste events concurrently, the way the event
// generator (Phase 8's IPC layer) eventually will.
//
// Usage: ./shrm_pipeline_demo [db_path]   (defaults to shrm_pipeline.db)

#include "db_manager.h"
#include "paths.h"
#include "seed_data.h"
#include "event_queue.h"
#include "workers.h"
#include <atomic>
#include <cstdio>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    const std::string db_path = (argc > 1) ? argv[1] : "shrm_pipeline.db";
    std::remove(db_path.c_str());

    std::cout << "SHRM Phase 4 -- full pipeline demo (queue + worker threads + DB)\n";
    std::cout << "==================================================================\n";

    DbManager db(db_path);
    db.initSchema(schemaPath());
    SeedSummary seeded = seedDatabase(db);
    std::cout << "Seeded " << seeded.students_added << " students, "
              << seeded.inventory_items_added << " inventory items, "
              << seeded.meals_created << " meals for today.\n\n";

    // We only need a couple of the seeded IDs for this demo -- students
    // 1..8 and today's lunch is meal_id 2 (breakfast=1, lunch=2, dinner=3,
    // per seedDatabase's insertion order). Rice is item_id 1.
    const int lunch_meal_id = 2;
    const int rice_item_id = 1;

    EventQueue queue;
    const int num_workers = 3;
    const int num_producers = 4;
    const int events_per_producer = 25;

    std::cout << "Workers: " << num_workers
              << "  Producers: " << num_producers
              << "  Events/producer: " << events_per_producer << "\n\n";

    // --- Workers: each opens its own connection to db_path (see the
    // comment in workers.cpp for why sharing one connection across
    // threads breaks logPrep's transactions) ------------------------
    std::vector<std::thread> workers;
    for (int w = 0; w < num_workers; ++w) {
        workers.emplace_back(runWorker, std::ref(queue), db_path, w, nullptr);
    }

    // --- Producers: push a realistic mix of event types -------------
    std::vector<std::thread> producers;
    std::atomic<int> total_pushed{0};
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < events_per_producer; ++i) {
                int student_id = (i % 8) + 1; // cycle through the 8 seeded students
                switch (i % 3) {
                    case 0:
                        queue.push(Event::makeAttendance(student_id, lunch_meal_id));
                        break;
                    case 1:
                        // Small, safe quantity -- this demo is about proving the
                        // pipeline wiring works, not re-testing the rollback path
                        // (that's already covered by shrm_server).
                        queue.push(Event::makePrep(lunch_meal_id, rice_item_id, 0.5,
                                                    "worker-p" + std::to_string(p)));
                        break;
                    case 2:
                        queue.push(Event::makeWaste(lunch_meal_id, rice_item_id, 0.1));
                        break;
                }
                total_pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : producers) t.join();
    std::cout << "All producers done. Total events pushed: " << total_pushed.load() << "\n";
    queue.shutdown();
    for (auto& t : workers) t.join();
    std::cout << "All workers drained and exited.\n\n";

    std::cout << "-- Final inventory --\n";
    for (const auto& item : db.getAllInventory()) {
        std::cout << "  [" << item.item_id << "] " << item.item_name
                  << ": " << item.quantity_available << " " << item.unit << "\n";
    }

    std::cout << "\nDone. DB file: " << db_path << "\n";
    return 0;
}
