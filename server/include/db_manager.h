#pragma once

#include <string>
#include <vector>
#include <optional>
#include <stdexcept>
#include <sqlite3.h>

// Thrown when a transactional operation fails and is rolled back
// (e.g. insufficient stock). Callers use this to distinguish a
// deliberate business-rule failure from a real DB error.
struct InsufficientStockError : public std::runtime_error {
    explicit InsufficientStockError(const std::string& msg) : std::runtime_error(msg) {}
};

struct InventoryItem {
    int item_id;
    std::string item_name;
    std::string unit;
    double quantity_available;
    double reorder_threshold;
};

struct Alert {
    int alert_id;
    int item_id;
    std::string message;
    std::string timestamp;
};

// Wraps a single SQLite connection and exposes the operations SHRM
// needs. Not thread-safe by itself -- Phase 2 adds a mutex around
// calls into this class from multiple worker threads.
class DbManager {
public:
    explicit DbManager(const std::string& db_path);
    ~DbManager();

    DbManager(const DbManager&) = delete;
    DbManager& operator=(const DbManager&) = delete;

    // Runs schema.sql (idempotent -- safe to call every startup).
    void initSchema(const std::string& schema_path);

    // --- Basic CRUD (Phase 1) ---
    int addStudent(const std::string& name, const std::string& room_no);
    int addInventoryItem(const std::string& name, const std::string& unit,
                          double initial_qty, double reorder_threshold);
    int createMeal(const std::string& date, const std::string& meal_type);
    void logAttendance(int student_id, int meal_id);

    // Transactional: deducts stock and logs the prep event atomically.
    // Throws InsufficientStockError (after rolling back) if there is
    // not enough stock -- this is the rollback demo for the report.
    void logPrep(int meal_id, int item_id, double quantity_used,
                 const std::string& prepared_by);

    void logWaste(int meal_id, int item_id, double quantity_wasted);

    std::vector<InventoryItem> getAllInventory();
    std::optional<InventoryItem> getInventoryItem(int item_id);
    std::vector<Alert> getAlerts();

    // Raw handle, exposed for later phases (e.g. concurrency tests)
    // that need to run their own statements against the same DB file.
    sqlite3* raw() { return db_; }

private:
    sqlite3* db_ = nullptr;
    void exec(const std::string& sql);
};
