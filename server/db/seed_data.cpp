#include "seed_data.h"
#include <ctime>
#include <array>
#include <vector>
#include <string>

namespace {

// Today's date as "YYYY-MM-DD", using the machine's local clock. Meal
// instances are created for today so whoever runs this gets a DB that
// looks current, regardless of when they run it.
std::string todayIso() {
    std::time_t t = std::time(nullptr);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return std::string(buf);
}

const char* weekdayName(const std::tm& tm_buf) {
    static const char* names[7] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    return names[tm_buf.tm_wday];
}

} // namespace

SeedSummary seedDatabase(DbManager& db) {
    SeedSummary summary;

    // --- Students -------------------------------------------------
    const std::vector<std::pair<std::string, std::string>> students = {
        {"Aanya Khurana",       "A-101"},
        {"Tanishq Agrawal",     "B-204"},
        {"Prateek Agarwal",     "B-205"},
        {"Anant Agarwal",       "C-310"},
        {"Arjun Singh Poonia",  "C-311"},
        {"Bhoomi Garg",         "A-102"},
        {"Aayushi Tripathi",    "D-118"},
        {"Rohan Mehta",         "D-119"},
    };
    std::vector<int> student_ids;
    for (const auto& [name, room] : students) {
        student_ids.push_back(db.addStudent(name, room));
        summary.students_added++;
    }

    // --- Inventory --------------------------------------------------
    // (name, unit, initial qty, reorder threshold)
    const std::vector<std::tuple<std::string, std::string, double, double>> inventory = {
        {"Rice",          "kg", 100.0, 20.0},
        {"Wheat Flour",   "kg",  80.0, 15.0},
        {"Dal",           "kg",  50.0, 10.0},
        {"Vegetables",    "kg",  60.0, 15.0},
        {"Milk",          "l",   40.0, 10.0},
        {"Cooking Oil",   "l",   25.0,  5.0},
    };
    std::vector<int> item_ids;
    for (const auto& [name, unit, qty, threshold] : inventory) {
        item_ids.push_back(db.addInventoryItem(name, unit, qty, threshold));
        summary.inventory_items_added++;
    }

    // --- Weekly menu --------------------------------------------------
    // One representative dish per meal slot, for every day of the week.
    const std::array<std::string, 7> days = {
        "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
    };
    const std::array<std::string, 3> meal_types = {"breakfast", "lunch", "dinner"};
    // [day][meal_type] -> dish name
    const std::array<std::array<std::string, 3>, 7> weekly_dishes = {{
        {"Poha", "Rajma Chawal", "Roti with Mixed Veg"},
        {"Idli Sambhar", "Dal Tadka with Rice", "Paneer Curry with Roti"},
        {"Upma", "Chole with Rice", "Kadhi Chawal"},
        {"Paratha with Curd", "Veg Pulao", "Roti with Dal Fry"},
        {"Bread Omelette", "Rice with Sambhar", "Chana Masala with Roti"},
        {"Sandwich", "Veg Biryani", "Mix Veg with Roti"},
        {"Chole Bhature", "Khichdi", "Paneer Butter Masala with Roti"},
    }};
    for (std::size_t d = 0; d < days.size(); ++d) {
        for (std::size_t m = 0; m < meal_types.size(); ++m) {
            db.addMenuEntry(days[d], meal_types[m], weekly_dishes[d][m]);
            summary.menu_entries_added++;
        }
    }

    // --- Today's meals + attendance ------------------------------------
    std::time_t t = std::time(nullptr);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    const std::string today = todayIso();
    const std::string weekday = weekdayName(tm_buf);
    (void)weekday; // available if a caller wants to cross-reference the menu table

    // Roughly rising attendance across the day: fewer at breakfast,
    // most at lunch, slightly fewer at dinner -- a realistic-enough
    // pattern for the scheduler/threading phases to chew on later.
    const std::array<std::size_t, 3> attendance_count = {5, 8, 6};

    for (std::size_t m = 0; m < meal_types.size(); ++m) {
        int meal_id = db.createMeal(today, meal_types[m]);
        summary.meals_created++;

        std::size_t n = attendance_count[m];
        for (std::size_t i = 0; i < n && i < student_ids.size(); ++i) {
            db.logAttendance(student_ids[i], meal_id);
            summary.attendance_rows_added++;
        }
    }

    return summary;
}
