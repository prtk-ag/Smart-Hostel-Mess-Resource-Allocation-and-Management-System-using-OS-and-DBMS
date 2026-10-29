-- SHRM database schema (Phase 1)
-- Normalized to 3NF. SQLite dialect.

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS students (
    student_id   INTEGER PRIMARY KEY AUTOINCREMENT,
    name         TEXT NOT NULL,
    room_no      TEXT
);

CREATE TABLE IF NOT EXISTS inventory (
    item_id             INTEGER PRIMARY KEY AUTOINCREMENT,
    item_name           TEXT NOT NULL UNIQUE,
    unit                TEXT NOT NULL,
    quantity_available  REAL NOT NULL DEFAULT 0,
    reorder_threshold   REAL NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS meals (
    meal_id     INTEGER PRIMARY KEY AUTOINCREMENT,
    date        TEXT NOT NULL,
    meal_type   TEXT NOT NULL CHECK (meal_type IN ('breakfast','lunch','dinner'))
);

CREATE TABLE IF NOT EXISTS menu (
    menu_id       INTEGER PRIMARY KEY AUTOINCREMENT,
    day_of_week   TEXT NOT NULL,
    meal_type     TEXT NOT NULL,
    dish_name     TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS attendance (
    attendance_id INTEGER PRIMARY KEY AUTOINCREMENT,
    student_id    INTEGER NOT NULL REFERENCES students(student_id),
    meal_id       INTEGER NOT NULL REFERENCES meals(meal_id),
    timestamp     TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Opt-out attendance: every student is assumed PRESENT for a meal unless a row
-- exists here. UNIQUE stops the same student being marked absent twice.
CREATE TABLE IF NOT EXISTS absences (
    absence_id  INTEGER PRIMARY KEY AUTOINCREMENT,
    student_id  INTEGER NOT NULL REFERENCES students(student_id),
    meal_id     INTEGER NOT NULL REFERENCES meals(meal_id),
    reason      TEXT,
    timestamp   TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE (student_id, meal_id)
);

CREATE TABLE IF NOT EXISTS meal_prep_logs (
    prep_id        INTEGER PRIMARY KEY AUTOINCREMENT,
    meal_id        INTEGER NOT NULL REFERENCES meals(meal_id),
    item_id        INTEGER NOT NULL REFERENCES inventory(item_id),
    quantity_used  REAL NOT NULL,
    prepared_by    TEXT,
    timestamp      TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS waste_logs (
    waste_id         INTEGER PRIMARY KEY AUTOINCREMENT,
    meal_id          INTEGER NOT NULL REFERENCES meals(meal_id),
    item_id          INTEGER NOT NULL REFERENCES inventory(item_id),
    quantity_wasted  REAL NOT NULL,
    timestamp        TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS alerts (
    alert_id   INTEGER PRIMARY KEY AUTOINCREMENT,
    item_id    INTEGER NOT NULL REFERENCES inventory(item_id),
    message    TEXT NOT NULL,
    timestamp  TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Indexes on hot query paths (Phase 1 groundwork for the indexing experiment)
CREATE INDEX IF NOT EXISTS idx_prep_meal ON meal_prep_logs(meal_id);
CREATE INDEX IF NOT EXISTS idx_attendance_meal_student ON attendance(meal_id, student_id);
CREATE INDEX IF NOT EXISTS idx_absence_meal ON absences(meal_id);
CREATE INDEX IF NOT EXISTS idx_inventory_name ON inventory(item_name);

-- Low-stock trigger: fires whenever an inventory update drops quantity
-- below its reorder threshold. This is the software equivalent of the
-- load-cell threshold alert used in the IoT reference papers.
CREATE TRIGGER IF NOT EXISTS trg_low_stock
AFTER UPDATE OF quantity_available ON inventory
WHEN NEW.quantity_available < NEW.reorder_threshold
BEGIN
    INSERT INTO alerts (item_id, message)
    VALUES (NEW.item_id,
            'Low stock: ' || NEW.item_name || ' at ' || NEW.quantity_available || ' ' || NEW.unit);
END;
