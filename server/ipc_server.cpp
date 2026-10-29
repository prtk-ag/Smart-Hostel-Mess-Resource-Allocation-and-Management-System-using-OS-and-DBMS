// Phase 8: the server side of the IPC link. Reads events from another
// process over a pipe, validates each line, and feeds the Phase 3 queue.
// The Phase 4/5 worker threads then write to the database exactly as before.
//
//   generator PROCESS --pipe--> [this: reader thread = main] --> EventQueue --> workers --> SQLite
//
// Usage: shrm_ipc_server [--in stdio|pipe:NAME] [--db PATH] [--workers N]
// Starts from a freshly seeded database every run (deletes PATH first).
// Defaults: in=stdio, db=shrm_ipc.db, workers=3.
// With --in stdio the report goes to stderr so it doesn't mix with the data.

#include "channel.h"
#include "db_manager.h"
#include "event_codec.h"
#include "event_queue.h"
#include "paths.h"
#include "seed_data.h"
#include "workers.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    std::string in_spec = "stdio", db_path = "shrm_ipc.db";
    int num_workers = 3;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) { std::cerr << flag << " needs a value\n"; std::exit(2); }
            return argv[++i];
        };
        if (a == "--in") in_spec = next("--in");
        else if (a == "--db") db_path = next("--db");
        else if (a == "--workers") num_workers = std::atoi(next("--workers"));
        else { std::cerr << "unknown option " << a << "\n"; return 2; }
    }
    if (num_workers < 1) num_workers = 1;

    std::remove(db_path.c_str());
    {
        DbManager db(db_path);
        db.initSchema(schemaPath());
        seedDatabase(db);
    }

    std::cerr << "ipc_server: database ready, waiting for events on '" << in_spec << "'...\n";
    std::string error;
    auto in = openReader(in_spec, error);
    if (!in) { std::cerr << "ipc_server: " << error << "\n"; return 1; }

    EventQueue queue;
    WorkerStats stats;
    std::vector<std::thread> workers;
    for (int w = 0; w < num_workers; ++w) workers.emplace_back(runWorker, std::ref(queue), db_path, w, &stats);

    int received = 0, malformed = 0;
    std::string line;
    while (in->readLine(line)) {
        ++received;
        Event e;
        std::string why;
        if (decodeEvent(line, e, &why)) {
            queue.push(e);
        } else {
            ++malformed;
            std::cerr << "ipc_server: rejected line " << received << " (" << why << "): '" << line << "'\n";
        }
    }
    queue.shutdown();
    for (auto& t : workers) t.join();

    std::cerr << "\n--- ipc_server report ---\n"
              << "lines received:    " << received << "\n"
              << "malformed/skipped: " << malformed << "\n"
              << "applied to DB:     " << stats.applied.load() << "\n"
              << "refused by DB:     " << stats.rejected.load() << "  (e.g. insufficient stock)\n";
    DbManager db(db_path);
    std::cerr << "inventory after run:\n";
    for (const auto& item : db.getAllInventory())
        std::cerr << "  [" << item.item_id << "] " << item.item_name << ": " << item.quantity_available << " " << item.unit << "\n";
    return 0;
}
