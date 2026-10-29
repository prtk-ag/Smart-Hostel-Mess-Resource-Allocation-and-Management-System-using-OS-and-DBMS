// Phase 10: indexing experiment. Same queries, same data, with and without
// the index; reports timings and the SQLite query plan (SCAN vs SEARCH).
// Usage: shrm_index_bench [--rows N] [--meals M] [--json]
#include <sqlite3.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>

static void run(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string m = err ? err : "error"; sqlite3_free(err);
        throw std::runtime_error(m + " | " + sql);
    }
}
static std::string plan(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* st; std::string out;
    sqlite3_prepare_v2(db, ("EXPLAIN QUERY PLAN " + sql).c_str(), -1, &st, nullptr);
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (!out.empty()) out += "; ";
        out += (const char*)sqlite3_column_text(st, 3);
    }
    sqlite3_finalize(st);
    return out;
}
// Runs the lookup `reps` times with different ids; returns total ms.
static double timeQuery(sqlite3* db, const char* sql, int reps, int idRange, long& checksum) {
    sqlite3_stmt* st; sqlite3_prepare_v2(db, sql, -1, &st, nullptr);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; ++i) {
        sqlite3_bind_int(st, 1, 1 + (i * 7919) % idRange);
        while (sqlite3_step(st) == SQLITE_ROW) checksum += sqlite3_column_int(st, 0);
        sqlite3_reset(st);
    }
    auto t1 = std::chrono::steady_clock::now();
    sqlite3_finalize(st);
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main(int argc, char** argv) {
    int rows = 200000, meals = 2000; bool json = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--rows") && i + 1 < argc) rows = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--meals") && i + 1 < argc) meals = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--json")) json = true;
    }
    if (rows < 1000 || meals < 1) { fprintf(stderr, "bad args\n"); return 1; }
    sqlite3* db; sqlite3_open(":memory:", &db);
    run(db, "CREATE TABLE attendance(attendance_id INTEGER PRIMARY KEY, student_id INTEGER NOT NULL,"
            " meal_id INTEGER NOT NULL, timestamp TEXT);");
    run(db, "BEGIN");
    sqlite3_stmt* ins; sqlite3_prepare_v2(db, "INSERT INTO attendance(student_id,meal_id,timestamp) VALUES(?,?,'x')", -1, &ins, nullptr);
    for (int i = 0; i < rows; ++i) {
        sqlite3_bind_int(ins, 1, 1 + i % 800); sqlite3_bind_int(ins, 2, 1 + (i * 31) % meals);
        sqlite3_step(ins); sqlite3_reset(ins);
    }
    sqlite3_finalize(ins); run(db, "COMMIT");

    const char* q1 = "SELECT COUNT(*) FROM attendance WHERE meal_id = ?";
    const char* q2 = "SELECT student_id FROM attendance WHERE meal_id = ? AND student_id = 5";
    const int reps = 300; long cs = 0;
    std::string p1a = plan(db, q1), p2a = plan(db, q2);
    double t1a = timeQuery(db, q1, reps, meals, cs), t2a = timeQuery(db, q2, reps, meals, cs);
    long csNo = cs; cs = 0;
    run(db, "CREATE INDEX idx_attendance_meal_student ON attendance(meal_id, student_id)");
    std::string p1b = plan(db, q1), p2b = plan(db, q2);
    double t1b = timeQuery(db, q1, reps, meals, cs), t2b = timeQuery(db, q2, reps, meals, cs);
    // t1a/t2a were summed into csNo together; recompute fairly for equality check
    long csIdx = cs;
    if (csNo != csIdx) { fprintf(stderr, "RESULT MISMATCH %ld vs %ld\n", csNo, csIdx); return 2; }

    if (json) {
        printf("{\"rows\":%d,\"meals\":%d,\"reps\":%d,\"queries\":["
               "{\"name\":\"count by meal\",\"no_index_ms\":%.2f,\"index_ms\":%.2f,\"plan_no_index\":\"%s\",\"plan_index\":\"%s\"},"
               "{\"name\":\"meal + student lookup\",\"no_index_ms\":%.2f,\"index_ms\":%.2f,\"plan_no_index\":\"%s\",\"plan_index\":\"%s\"}]}\n",
               rows, meals, reps, t1a, t1b, p1a.c_str(), p1b.c_str(), t2a, t2b, p2a.c_str(), p2b.c_str());
    } else {
        printf("Index experiment: %d attendance rows, %d meals, %d lookups per query\n\n", rows, meals, reps);
        printf("Q1  %s\n  no index : %8.2f ms   plan: %s\n  index    : %8.2f ms   plan: %s\n  speed-up : %.0fx\n\n", q1, t1a, p1a.c_str(), t1b, p1b.c_str(), t1a / (t1b > 0.01 ? t1b : 0.01));
        printf("Q2  %s\n  no index : %8.2f ms   plan: %s\n  index    : %8.2f ms   plan: %s\n  speed-up : %.0fx\n\n", q2, t2a, p2a.c_str(), t2b, p2b.c_str(), t2a / (t2b > 0.01 ? t2b : 0.01));
        printf("Results identical with and without the index (checksum %ld).\n", csIdx);
    }
    sqlite3_close(db);
    return 0;
}
