#include "db_manager.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>

DbManager::DbManager(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error(std::string("Failed to open DB: ") + sqlite3_errmsg(db_));
    }
    // Busy timeout so concurrent access (Phase 2) waits briefly on a
    // locked DB instead of failing immediately with SQLITE_BUSY.
    sqlite3_busy_timeout(db_, 3000);
}

DbManager::~DbManager() {
    if (db_) sqlite3_close(db_);
}

void DbManager::exec(const std::string& sql) {
    char* errmsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::string msg = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error("SQL error: " + msg + " | statement: " + sql);
    }
}

void DbManager::initSchema(const std::string& schema_path) {
    std::ifstream file(schema_path);
    if (!file) throw std::runtime_error("Cannot open schema file: " + schema_path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    exec(buffer.str());
}

int DbManager::addStudent(const std::string& name, const std::string& room_no) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO students (name, room_no) VALUES (?, ?);";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, room_no.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("addStudent failed: " + err);
    }
    sqlite3_finalize(stmt);
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

int DbManager::addInventoryItem(const std::string& name, const std::string& unit,
                                 double initial_qty, double reorder_threshold) {
    sqlite3_stmt* stmt;
    const char* sql =
        "INSERT INTO inventory (item_name, unit, quantity_available, reorder_threshold) "
        "VALUES (?, ?, ?, ?);";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, unit.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, initial_qty);
    sqlite3_bind_double(stmt, 4, reorder_threshold);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("addInventoryItem failed: " + err);
    }
    sqlite3_finalize(stmt);
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

int DbManager::createMeal(const std::string& date, const std::string& meal_type) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO meals (date, meal_type) VALUES (?, ?);";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, meal_type.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("createMeal failed: " + err);
    }
    sqlite3_finalize(stmt);
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

void DbManager::logAttendance(int student_id, int meal_id) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO attendance (student_id, meal_id) VALUES (?, ?);";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, student_id);
    sqlite3_bind_int(stmt, 2, meal_id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("logAttendance failed: " + err);
    }
    sqlite3_finalize(stmt);
}

int DbManager::addMenuEntry(const std::string& day_of_week, const std::string& meal_type,
                             const std::string& dish_name) {
    sqlite3_stmt* stmt;
    const char* sql =
        "INSERT INTO menu (day_of_week, meal_type, dish_name) VALUES (?, ?, ?);";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, day_of_week.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, meal_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, dish_name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("addMenuEntry failed: " + err);
    }
    sqlite3_finalize(stmt);
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

// This is the core transactional operation described in the proposal:
// deduct stock + insert the prep log, atomically. If stock is
// insufficient, the whole transaction is rolled back and an
// InsufficientStockError is thrown -- nothing is left half-written.
void DbManager::logPrep(int meal_id, int item_id, double quantity_used,
                         const std::string& prepared_by) {
    exec("BEGIN IMMEDIATE;"); // IMMEDIATE grabs the write lock now, not lazily --
                              // this is what Phase 2's concurrency test exercises.

    try {
        auto item = getInventoryItem(item_id);
        if (!item.has_value()) {
            exec("ROLLBACK;");
            throw std::runtime_error("logPrep failed: item_id " + std::to_string(item_id) + " not found");
        }
        if (item->quantity_available < quantity_used) {
            exec("ROLLBACK;");
            throw InsufficientStockError(
                "Not enough " + item->item_name + ": have " +
                std::to_string(item->quantity_available) + ", need " +
                std::to_string(quantity_used));
        }

        sqlite3_stmt* stmt;
        const char* update_sql =
            "UPDATE inventory SET quantity_available = quantity_available - ? WHERE item_id = ?;";
        sqlite3_prepare_v2(db_, update_sql, -1, &stmt, nullptr);
        sqlite3_bind_double(stmt, 1, quantity_used);
        sqlite3_bind_int(stmt, 2, item_id);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::string err = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            exec("ROLLBACK;");
            throw std::runtime_error("logPrep update failed: " + err);
        }
        sqlite3_finalize(stmt);

        const char* insert_sql =
            "INSERT INTO meal_prep_logs (meal_id, item_id, quantity_used, prepared_by) "
            "VALUES (?, ?, ?, ?);";
        sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, meal_id);
        sqlite3_bind_int(stmt, 2, item_id);
        sqlite3_bind_double(stmt, 3, quantity_used);
        sqlite3_bind_text(stmt, 4, prepared_by.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::string err = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            exec("ROLLBACK;");
            throw std::runtime_error("logPrep insert failed: " + err);
        }
        sqlite3_finalize(stmt);

        exec("COMMIT;");
    } catch (...) {
        // Safety net: if anything above threw without rolling back
        // (shouldn't happen given the explicit ROLLBACKs, but this
        // guards against future edits), make sure we don't leave an
        // open transaction hanging.
        if (sqlite3_get_autocommit(db_) == 0) {
            exec("ROLLBACK;");
        }
        throw;
    }
}

void DbManager::logWaste(int meal_id, int item_id, double quantity_wasted) {
    sqlite3_stmt* stmt;
    const char* sql =
        "INSERT INTO waste_logs (meal_id, item_id, quantity_wasted) VALUES (?, ?, ?);";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, meal_id);
    sqlite3_bind_int(stmt, 2, item_id);
    sqlite3_bind_double(stmt, 3, quantity_wasted);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("logWaste failed: " + err);
    }
    sqlite3_finalize(stmt);
}

std::vector<InventoryItem> DbManager::getAllInventory() {
    std::vector<InventoryItem> items;
    sqlite3_stmt* stmt;
    const char* sql =
        "SELECT item_id, item_name, unit, quantity_available, reorder_threshold FROM inventory;";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        items.push_back(InventoryItem{
            sqlite3_column_int(stmt, 0),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
            sqlite3_column_double(stmt, 3),
            sqlite3_column_double(stmt, 4)
        });
    }
    sqlite3_finalize(stmt);
    return items;
}

std::optional<InventoryItem> DbManager::getInventoryItem(int item_id) {
    sqlite3_stmt* stmt;
    const char* sql =
        "SELECT item_id, item_name, unit, quantity_available, reorder_threshold "
        "FROM inventory WHERE item_id = ?;";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, item_id);
    std::optional<InventoryItem> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = InventoryItem{
            sqlite3_column_int(stmt, 0),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
            sqlite3_column_double(stmt, 3),
            sqlite3_column_double(stmt, 4)
        };
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<Alert> DbManager::getAlerts() {
    std::vector<Alert> alerts;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT alert_id, item_id, message, timestamp FROM alerts ORDER BY alert_id;";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        alerts.push_back(Alert{
            sqlite3_column_int(stmt, 0),
            sqlite3_column_int(stmt, 1),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))
        });
    }
    sqlite3_finalize(stmt);
    return alerts;
}
