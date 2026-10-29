// SHRM Phase 1 demo: schema + transactional CRUD.
//
// This intentionally has no threads or IPC yet -- that's Phase 2/4.
// Right now it proves the DB layer works correctly: a normal prep
// transaction commits, an over-large prep request rolls back cleanly,
// and the low-stock trigger fires and is visible via getAlerts().

#include "db_manager.h"
#include <iostream>
#include <iomanip>

static void printInventory(DbManager& db) {
    std::cout << "\n-- Inventory --\n";
    for (const auto& item : db.getAllInventory()) {
        std::cout << "  [" << item.item_id << "] " << item.item_name
                  << ": " << item.quantity_available << " " << item.unit
                  << " (reorder below " << item.reorder_threshold << ")\n";
    }
}

static void printAlerts(DbManager& db) {
    std::cout << "\n-- Alerts --\n";
    auto alerts = db.getAlerts();
    if (alerts.empty()) {
        std::cout << "  (none)\n";
        return;
    }
    for (const auto& a : alerts) {
        std::cout << "  #" << a.alert_id << " item " << a.item_id
                  << ": " << a.message << " @ " << a.timestamp << "\n";
    }
}

int main() {
    const std::string db_path = "shrm.db";
    DbManager db(db_path);
    db.initSchema("server/db/schema.sql");

    std::cout << "SHRM Phase 1 demo\n";
    std::cout << "=================\n";

    // Seed a couple of students, a meal, and one inventory item.
    int alice = db.addStudent("Alice", "A-101");
    int bob   = db.addStudent("Bob",   "B-204");

    int rice_id = db.addInventoryItem("Rice", "kg", /*qty=*/100.0, /*threshold=*/20.0);
    int meal_id = db.createMeal("2026-09-08", "lunch");

    db.logAttendance(alice, meal_id);
    db.logAttendance(bob, meal_id);

    printInventory(db);

    // --- Case 1: a normal prep transaction, well within stock. ---
    std::cout << "\n>> Logging prep: 15kg rice used for lunch\n";
    db.logPrep(meal_id, rice_id, 15.0, "kitchen_staff_1");
    printInventory(db); // 100 -> 85, no alert yet (threshold is 20)

    // --- Case 2: push stock below the reorder threshold on purpose,
    //     to demonstrate the low-stock trigger firing. ---
    std::cout << "\n>> Logging prep: 70kg rice used for dinner prep (pushes below threshold)\n";
    int dinner_id = db.createMeal("2026-09-08", "dinner");
    db.logPrep(dinner_id, rice_id, 70.0, "kitchen_staff_2");
    printInventory(db); // 85 -> 15, which is below the 20kg threshold
    printAlerts(db);    // trigger should have inserted a low-stock alert

    // --- Case 3: deliberately request more than what's left, to
    //     demonstrate the transaction rolling back cleanly. ---
    std::cout << "\n>> Attempting to log prep: 50kg rice (only ~15kg left) -- should fail and roll back\n";
    try {
        db.logPrep(dinner_id, rice_id, 50.0, "kitchen_staff_2");
    } catch (const InsufficientStockError& e) {
        std::cout << "  Caught expected error: " << e.what() << "\n";
    }
    printInventory(db); // unchanged from Case 2 -- proves the rollback worked

    std::cout << "\nDone. DB file: " << db_path << "\n";
    return 0;
}
