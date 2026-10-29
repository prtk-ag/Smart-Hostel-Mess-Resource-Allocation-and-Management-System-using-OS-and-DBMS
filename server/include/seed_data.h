#pragma once

#include "db_manager.h"

// Summary of what seedDatabase() inserted, so callers (the seed CLI,
// and later the test suite) can print or assert on counts without
// re-querying the DB.
struct SeedSummary {
    int students_added = 0;
    int inventory_items_added = 0;
    int menu_entries_added = 0;
    int meals_created = 0;
    int attendance_rows_added = 0;
};

// Populates a freshly-initialized DB with a realistic starting dataset:
// a handful of students, a stocked inventory, a full week's menu, three
// meal instances for today (breakfast/lunch/dinner), and attendance for
// each. This is what Phase 3 (the concurrency layer) and Phase 4 (the
// scheduler) need real rows to work against, instead of the single
// hand-inserted item main.cpp uses for the transaction demo.
SeedSummary seedDatabase(DbManager& db);
