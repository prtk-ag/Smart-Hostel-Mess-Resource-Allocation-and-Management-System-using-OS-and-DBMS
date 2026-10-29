// Phase 3 deliverable: proves the event queue is actually safe under
// concurrent producers and consumers before anything in Phase 4 (real
// worker threads writing to the DB) gets built on top of it.
//
// Several producer threads push a known total number of uniquely
// numbered events; several consumer threads drain them concurrently.
// Correctness isn't just "the counts match" (that alone wouldn't catch
// a duplicated-and-dropped pair cancelling out) -- every single event
// ID is tracked in a `seen` array and must be consumed EXACTLY once.
//
// Usage: ./shrm_queue_test [producers] [events_per_producer] [consumers]
// Defaults: 4 producers x 2000 events, 3 consumers.

#include "event_queue.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    const int num_producers = (argc > 1) ? std::stoi(argv[1]) : 4;
    const int events_per_producer = (argc > 2) ? std::stoi(argv[2]) : 2000;
    const int num_consumers = (argc > 3) ? std::stoi(argv[3]) : 3;
    const int total_events = num_producers * events_per_producer;

    std::cout << "SHRM Phase 3 -- event queue producer/consumer test\n";
    std::cout << "====================================================\n";
    std::cout << "Producers: " << num_producers
              << "  Events/producer: " << events_per_producer
              << "  Consumers: " << num_consumers
              << "  Total events: " << total_events << "\n\n";

    EventQueue queue;
    std::atomic<int> next_id{0};
    std::atomic<long> consumed_count{0};

    // seen[i] must end up at exactly 1 for every i -- 0 means an event
    // was lost, 2+ means it was delivered more than once. Either would
    // mean the queue has a race in it.
    std::vector<std::atomic<int>> seen(total_events);
    for (auto& s : seen) s.store(0);

    auto start = std::chrono::steady_clock::now();

    // --- Producers ---------------------------------------------------
    std::vector<std::thread> producers;
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < events_per_producer; ++i) {
                int id = next_id.fetch_add(1, std::memory_order_relaxed);
                // Reusing the Attendance event shape purely as a labelled
                // envelope here -- student_id carries the unique id,
                // meal_id carries which producer thread sent it. Real
                // attendance/prep/waste events are exercised in Phase 4.
                queue.push(Event::makeAttendance(id, /*meal_id=*/p));
            }
        });
    }

    // --- Consumers -----------------------------------------------------
    std::vector<std::thread> consumers;
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&] {
            Event e;
            while (queue.pop(e)) {
                seen[e.student_id].fetch_add(1, std::memory_order_relaxed);
                consumed_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : producers) t.join();
    // All producers are done and every event they made is sitting in
    // (or already drained from) the queue. Shutting down now is safe:
    // shutdown() only stops pop() once the queue is BOTH told to stop
    // AND empty, so nothing already queued gets dropped.
    queue.shutdown();
    for (auto& t : consumers) t.join();

    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Produced:  " << total_events << "\n";
    std::cout << "Consumed:  " << consumed_count.load() << "\n";
    std::cout << "Elapsed:   " << ms << " ms\n\n";

    int lost = 0, duplicated = 0;
    for (int i = 0; i < total_events; ++i) {
        int count = seen[i].load();
        if (count == 0) lost++;
        else if (count > 1) duplicated++;
    }

    std::cout << "Lost events:       " << lost << "\n";
    std::cout << "Duplicated events: " << duplicated << "\n\n";

    bool pass = (lost == 0 && duplicated == 0 &&
                 consumed_count.load() == total_events &&
                 queue.size() == 0);

    std::cout << (pass ? "PASS -- every event delivered exactly once, queue drained.\n"
                        : "FAIL -- see counts above.\n");

    return pass ? 0 : 1;
}
