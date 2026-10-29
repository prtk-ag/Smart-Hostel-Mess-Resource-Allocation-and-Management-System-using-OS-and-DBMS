// Phase 6 deliverable: checks the FCFS scheduler against answers worked out
// by hand, then against invariants that must hold for ANY workload.
//
// Hand-computed cases come first on purpose -- a test that only compares the
// scheduler to itself can't catch a metric defined wrongly in the first place.
//
// CHECK is used instead of assert() so the tests still run in Release builds
// (assert is compiled out under NDEBUG).

#include "scheduler.h"

#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond, message)                                                        \
    do {                                                                            \
        ++g_checks;                                                                 \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::cout << "  FAIL: " << (message) << "   [" #cond "]\n";              \
        }                                                                           \
    } while (0)

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

Job makeJob(int id, const std::string& name, int arrival, int burst, int priority = 0) {
    Job j;
    j.id = id;
    j.name = name;
    j.arrival_time = arrival;
    j.burst_time = burst;
    j.priority = priority;
    return j;
}

// Classic textbook example: three jobs arriving together, one long, two short.
// Order P1, P2, P3 -> waiting 0, 24, 27 -> average 17.
void testTextbookExample() {
    std::cout << "[1] Textbook example (all arrive at t=0)\n";
    auto r = scheduleFCFS({makeJob(1, "P1", 0, 24), makeJob(2, "P2", 0, 3), makeJob(3, "P3", 0, 3)});

    CHECK(r.jobs.size() == 3, "three jobs scheduled");
    CHECK(r.jobs[0].waiting_time == 0 && r.jobs[1].waiting_time == 24 && r.jobs[2].waiting_time == 27,
          "waiting times are 0, 24, 27");
    CHECK(r.jobs[0].turnaround_time == 24 && r.jobs[1].turnaround_time == 27 && r.jobs[2].turnaround_time == 30,
          "turnaround times are 24, 27, 30");
    CHECK(near(r.avg_waiting_time, 17.0), "average waiting time is 17");
    CHECK(near(r.avg_turnaround_time, 27.0), "average turnaround time is 27");
    CHECK(r.makespan == 30 && r.idle_time == 0, "makespan 30, no idle time");
}

// Staggered arrivals: starts 0,5,8,16 -> waiting 0,4,6,13 -> average 5.75.
void testStaggeredArrivals() {
    std::cout << "[2] Staggered arrivals\n";
    auto r = scheduleFCFS({makeJob(1, "P1", 0, 5), makeJob(2, "P2", 1, 3),
                           makeJob(3, "P3", 2, 8), makeJob(4, "P4", 3, 6)});

    CHECK(r.jobs[0].start_time == 0 && r.jobs[1].start_time == 5 &&
          r.jobs[2].start_time == 8 && r.jobs[3].start_time == 16, "start times are 0, 5, 8, 16");
    CHECK(r.jobs[3].completion_time == 22, "last job completes at 22");
    CHECK(near(r.avg_waiting_time, 5.75), "average waiting time is 5.75");
    CHECK(near(r.avg_turnaround_time, 11.25), "average turnaround time is 11.25");
}

// A gap between jobs: the station must sit idle, not start a job early.
void testIdleGaps() {
    std::cout << "[3] Idle gaps (late arrivals)\n";
    auto r = scheduleFCFS({makeJob(1, "A", 2, 3), makeJob(2, "B", 10, 4)});

    CHECK(r.jobs[0].start_time == 2, "first job waits for its arrival (starts at 2, not 0)");
    CHECK(r.jobs[1].start_time == 10, "second job starts at its arrival (10), not right after the first");
    CHECK(r.jobs[0].waiting_time == 0 && r.jobs[1].waiting_time == 0, "neither job waits in the queue");
    CHECK(r.idle_time == 7, "idle time is 7 (t=0..2 and t=5..10)");
    CHECK(r.makespan == 14, "makespan is 14");
    CHECK(r.makespan == 3 + 4 + r.idle_time, "makespan = total burst + idle");
}

// Input order must not matter; equal arrivals are broken by id; input untouched.
void testOrderingAndTies() {
    std::cout << "[4] Input order, tie-breaking, caller's list untouched\n";
    std::vector<Job> input = {makeJob(5, "late-id", 0, 2), makeJob(9, "later", 4, 1), makeJob(2, "early-id", 0, 3)};
    const std::vector<Job> copy = input;
    auto r = scheduleFCFS(input);

    CHECK(r.jobs[0].job.id == 2 && r.jobs[1].job.id == 5 && r.jobs[2].job.id == 9,
          "ran id 2 before id 5 (same arrival), then id 9 (later arrival)");
    bool untouched = input.size() == copy.size();
    for (std::size_t i = 0; untouched && i < input.size(); ++i) untouched = input[i].id == copy[i].id;
    CHECK(untouched, "caller's job list was not reordered");
}

void testEmptyInput() {
    std::cout << "[5] Empty input\n";
    auto r = scheduleFCFS({});
    CHECK(r.jobs.empty(), "no jobs in result");
    CHECK(near(r.avg_waiting_time, 0.0) && near(r.avg_turnaround_time, 0.0), "averages are 0, not NaN");
    CHECK(r.makespan == 0 && r.idle_time == 0, "makespan and idle are 0");
}

// Properties that must hold for every workload, checked over many random ones.
void testRandomWorkloadInvariants() {
    std::cout << "[6] Invariants over 500 random workloads\n";
    std::mt19937 rng(12345);   // fixed seed: a failure here is reproducible
    int bad = 0;

    for (int trial = 0; trial < 500; ++trial) {
        std::vector<Job> jobs;
        const int n = 1 + static_cast<int>(rng() % 12);
        for (int i = 0; i < n; ++i) {
            jobs.push_back(makeJob(i + 1, "J" + std::to_string(i + 1),
                                   static_cast<int>(rng() % 40), 1 + static_cast<int>(rng() % 20)));
        }
        auto r = scheduleFCFS(jobs);

        bool ok = r.jobs.size() == jobs.size();
        int total_burst = 0, prev_end = 0, prev_arrival = -1;
        for (const auto& j : r.jobs) {
            ok = ok && j.start_time >= j.job.arrival_time;                       // never starts before it arrives
            ok = ok && j.start_time >= prev_end;                                  // one job at a time: no overlap
            ok = ok && j.turnaround_time == j.waiting_time + j.job.burst_time;    // turnaround = waiting + burst
            ok = ok && j.waiting_time >= 0;
            ok = ok && j.job.arrival_time >= prev_arrival;                        // FCFS: arrival order respected
            prev_end = j.completion_time;
            prev_arrival = j.job.arrival_time;
            total_burst += j.job.burst_time;
        }
        ok = ok && r.makespan == total_burst + r.idle_time;                       // time is fully accounted for
        if (!ok) ++bad;
    }
    CHECK(bad == 0, "all 500 workloads satisfy every invariant (failed: " + std::to_string(bad) + ")");
}

} // namespace

int main() {
    std::cout << "SHRM Phase 6 -- FCFS scheduler tests\n";
    std::cout << "======================================\n";

    testTextbookExample();
    testStaggeredArrivals();
    testIdleGaps();
    testOrderingAndTies();
    testEmptyInput();
    testRandomWorkloadInvariants();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    std::cout << (g_failures == 0 ? "PASS" : "FAIL") << "\n";
    return g_failures == 0 ? 0 : 1;
}
