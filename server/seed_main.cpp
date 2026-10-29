// Phase 2: seed data CLI.
//
// Creates (or overwrites) a database file with a realistic starting
// dataset -- students, stocked inventory, a full week's menu, and
// today's three meals with attendance already logged. Run this once
// before working on the concurrency layer or scheduler, so those
// phases have real rows to operate on instead of the single item
// main.cpp inserts for its transaction demo.
//
// Usage: ./shrm_seed [db_path]   (defaults to shrm_seed.db)

#include "db_manager.h"
#include "seed_data.h"
#include <iostream>
#include <cstdio>

int main(int argc, char** argv) {
    const std::string db_path = (argc > 1) ? argv[1] : "shrm_seed.db";

    // Start from a clean file each run -- inventory.item_name is
    // UNIQUE, so reseeding an existing DB would fail on the second
    // run otherwise. This is a dev-data tool, not a migration tool.
    std::remove(db_path.c_str());

    std::cout << "SHRM Phase 2 -- seeding " << db_path << "\n";
    std::cout << "=========================================\n";

    try {
        DbManager db(db_path);
        db.initSchema("server/db/schema.sql");

        SeedSummary summary = seedDatabase(db);

        std::cout << "Students added:        " << summary.students_added << "\n";
        std::cout << "Inventory items added:  " << summary.inventory_items_added << "\n";
        std::cout << "Menu entries added:     " << summary.menu_entries_added << "\n";
        std::cout << "Meals created (today):  " << summary.meals_created << "\n";
        std::cout << "Attendance rows added:  " << summary.attendance_rows_added << "\n";

        std::cout << "\nCurrent inventory:\n";
        for (const auto& item : db.getAllInventory()) {
            std::cout << "  [" << item.item_id << "] " << item.item_name
                      << ": " << item.quantity_available << " " << item.unit
                      << " (reorder below " << item.reorder_threshold << ")\n";
        }

        std::cout << "\nDone. DB file: " << db_path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Seeding failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
