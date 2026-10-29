// Phase 6/7 deliverable: checks the FCFS, SJF and Priority schedulers against answers worked out
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

std::vector<Job> lunchWorkload() {
    return {makeJob(1, "Rice", 0, 25, 2), makeJob(2, "Dal Tadka", 2, 35, 1), makeJob(3, "Roti", 4, 40, 2),
            makeJob(4, "Mixed Veg", 6, 20, 4), makeJob(5, "Salad", 9, 8, 3), makeJob(6, "Raita", 12, 10, 3)};
}

std::vector<int> order(const ScheduleResult& r) {
    std::vector<int> ids;
    for (const auto& j : r.jobs) ids.push_back(j.job.id);
    return ids;
}

// Textbook: bursts 6, 8, 7, 3 all at t=0 -> order P4,P1,P3,P2, waits 0,3,9,16 -> avg 7.
void testSjfTextbook() {
    std::cout << "[7] SJF textbook example\n";
    auto r = scheduleSJF({makeJob(1, "P1", 0, 6), makeJob(2, "P2", 0, 8), makeJob(3, "P3", 0, 7), makeJob(4, "P4", 0, 3)});
    CHECK((order(r) == std::vector<int>{4, 1, 3, 2}), "order is P4, P1, P3, P2");
    CHECK(r.jobs[0].waiting_time == 0 && r.jobs[1].waiting_time == 3 && r.jobs[2].waiting_time == 9 &&
          r.jobs[3].waiting_time == 16, "waiting times 0, 3, 9, 16");
    CHECK(near(r.avg_waiting_time, 7.0), "average waiting time is 7");
    CHECK(r.algorithm == "SJF", "algorithm label");
}

// Non-preemptive: a 1-minute job arriving at t=1 must still wait for the running 10-minute job.
void testSjfNonPreemptive() {
    std::cout << "[8] SJF does not preempt; idle gap; ties\n";
    auto r = scheduleSJF({makeJob(1, "Long", 0, 10), makeJob(2, "Short", 1, 1)});
    CHECK((order(r) == std::vector<int>{1, 2}), "running job is never interrupted");
    CHECK(r.jobs[1].start_time == 10 && r.jobs[1].waiting_time == 9, "short job waits 9");

    auto g = scheduleSJF({makeJob(1, "A", 5, 3), makeJob(2, "B", 20, 2)});
    CHECK(g.jobs[0].start_time == 5 && g.jobs[1].start_time == 20 && g.idle_time == 5 + 12,
          "idles until each arrival (idle 17)");

    auto t = scheduleSJF({makeJob(3, "C", 0, 4), makeJob(1, "A", 0, 4), makeJob(2, "B", 0, 4)});
    CHECK((order(t) == std::vector<int>{1, 2, 3}), "equal bursts and arrivals fall back to id order");
}

// Textbook: (burst, priority) = P1(10,3) P2(1,1) P3(2,4) P4(1,5) P5(5,2) -> P2,P5,P1,P3,P4; waits 0,1,6,16,18 -> 8.2
void testPriorityTextbook() {
    std::cout << "[9] Priority textbook example\n";
    auto r = schedulePriority({makeJob(1, "P1", 0, 10, 3), makeJob(2, "P2", 0, 1, 1), makeJob(3, "P3", 0, 2, 4),
                               makeJob(4, "P4", 0, 1, 5), makeJob(5, "P5", 0, 5, 2)});
    CHECK((order(r) == std::vector<int>{2, 5, 1, 3, 4}), "order is P2, P5, P1, P3, P4");
    CHECK(r.jobs[0].waiting_time == 0 && r.jobs[1].waiting_time == 1 && r.jobs[2].waiting_time == 6 &&
          r.jobs[3].waiting_time == 16 && r.jobs[4].waiting_time == 18, "waiting times 0, 1, 6, 16, 18");
    CHECK(near(r.avg_waiting_time, 8.2), "average waiting time is 8.2");

    // Urgent job arriving later does not preempt; equal priority -> earlier arrival first.
    auto n = schedulePriority({makeJob(1, "Low", 0, 10, 5), makeJob(2, "Urgent", 1, 1, 1)});
    CHECK((order(n) == std::vector<int>{1, 2}), "urgent job does not preempt a running one");
    auto e = schedulePriority({makeJob(1, "X", 0, 5, 9), makeJob(3, "Late", 3, 1, 2), makeJob(2, "Early", 1, 1, 2)});
    CHECK((order(e) == std::vector<int>{1, 2, 3}), "equal priority: earlier arrival first");
}

// Lunch workload, worked out by hand (see README Phase 7 table).
void testLunchWorkload() {
    std::cout << "[10] Lunch workload, all three algorithms\n";
    auto jobs = lunchWorkload();
    auto f = scheduleFCFS(jobs), s = scheduleSJF(jobs), p = schedulePriority(jobs);

    CHECK((order(f) == std::vector<int>{1, 2, 3, 4, 5, 6}), "FCFS order Rice, Dal, Roti, Veg, Salad, Raita");
    CHECK(near(f.avg_waiting_time, 400.0 / 6), "FCFS avg wait 400/6");

    CHECK((order(s) == std::vector<int>{1, 5, 6, 4, 2, 3}), "SJF order Rice, Salad, Raita, Veg, Dal, Roti");
    CHECK(near(s.avg_waiting_time, 229.0 / 6), "SJF avg wait 229/6 = 38.17");
    CHECK(near(s.avg_turnaround_time, 367.0 / 6), "SJF avg turnaround 367/6");

    CHECK((order(p) == std::vector<int>{1, 2, 3, 5, 6, 4}), "Priority order Rice, Dal, Roti, Salad, Raita, Veg");
    CHECK(near(p.avg_waiting_time, 63.0), "Priority avg wait 63");
    CHECK(near(p.avg_turnaround_time, 86.0), "Priority avg turnaround 86");

    CHECK(f.makespan == 138 && s.makespan == 138 && p.makespan == 138, "all three finish at 138");
    CHECK(s.avg_waiting_time < f.avg_waiting_time && s.avg_waiting_time < p.avg_waiting_time,
          "SJF has the lowest average wait here");
}

// For every workload: structural invariants, plus the selection rule itself --
// at each start, no job that had already arrived and not yet run is "better".
void testSjfPriorityInvariants() {
    std::cout << "[11] SJF/Priority invariants over 500 random workloads\n";
    std::mt19937 rng(777);
    int bad = 0;
    for (int trial = 0; trial < 500; ++trial) {
        std::vector<Job> jobs;
        const int n = 1 + static_cast<int>(rng() % 12);
        for (int i = 0; i < n; ++i)
            jobs.push_back(makeJob(i + 1, "J", static_cast<int>(rng() % 40), 1 + static_cast<int>(rng() % 20),
                                   1 + static_cast<int>(rng() % 5)));
        for (int algo = 0; algo < 2; ++algo) {
            auto r = algo == 0 ? scheduleSJF(jobs) : schedulePriority(jobs);
            bool ok = r.jobs.size() == jobs.size();
            int total = 0, prev_end = 0;
            for (std::size_t i = 0; i < r.jobs.size(); ++i) {
                const auto& a = r.jobs[i];
                ok = ok && a.start_time >= a.job.arrival_time && a.start_time >= prev_end &&
                     a.turnaround_time == a.waiting_time + a.job.burst_time;
                // Work-conserving: never idle while something is waiting.
                if (a.start_time > prev_end) {
                    for (std::size_t k = i; k < r.jobs.size(); ++k) ok = ok && r.jobs[k].job.arrival_time >= a.start_time;
                }
                // Selection rule: nobody later in the order was ready and better.
                for (std::size_t k = i + 1; k < r.jobs.size(); ++k) {
                    const Job& b = r.jobs[k].job;
                    if (b.arrival_time <= a.start_time) {
                        const int ka = algo == 0 ? a.job.burst_time : a.job.priority;
                        const int kb = algo == 0 ? b.burst_time : b.priority;
                        ok = ok && ka <= kb;
                    }
                }
                prev_end = a.completion_time;
                total += a.job.burst_time;
            }
            ok = ok && r.makespan == total + r.idle_time;
            if (!ok) ++bad;
        }
        // FCFS, SJF, Priority must all do the same total work.
        if (scheduleSJF(jobs).makespan != scheduleFCFS(jobs).makespan) ++bad;
    }
    CHECK(bad == 0, "all workloads satisfy every invariant (failed: " + std::to_string(bad) + ")");

    // When everything arrives together, SJF is provably optimal for waiting time.
    int worse = 0;
    for (int trial = 0; trial < 300; ++trial) {
        std::vector<Job> jobs;
        const int n = 2 + static_cast<int>(rng() % 9);
        for (int i = 0; i < n; ++i) jobs.push_back(makeJob(i + 1, "J", 0, 1 + static_cast<int>(rng() % 20), 1 + static_cast<int>(rng() % 5)));
        const double sjf = scheduleSJF(jobs).avg_waiting_time;
        if (sjf > scheduleFCFS(jobs).avg_waiting_time + 1e-9 || sjf > schedulePriority(jobs).avg_waiting_time + 1e-9) ++worse;
    }
    CHECK(worse == 0, "SJF is never worse than FCFS/Priority when all arrive at t=0 (worse: " + std::to_string(worse) + ")");
}

} // namespace

int main() {
    std::cout << "SHRM Phase 7 -- FCFS / SJF / Priority scheduler tests\n";
    std::cout << "======================================\n";

    testTextbookExample();
    testStaggeredArrivals();
    testIdleGaps();
    testOrderingAndTies();
    testEmptyInput();
    testRandomWorkloadInvariants();
    testSjfTextbook();
    testSjfNonPreemptive();
    testPriorityTextbook();
    testLunchWorkload();
    testSjfPriorityInvariants();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    std::cout << (g_failures == 0 ? "PASS" : "FAIL") << "\n";
    return g_failures == 0 ? 0 : 1;
}
