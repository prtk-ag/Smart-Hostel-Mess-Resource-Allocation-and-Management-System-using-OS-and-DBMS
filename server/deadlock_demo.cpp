// Phase 9: deadlock demo.
//
// Two kitchen workers need the same two resources: the INVENTORY lock and the
// ALERTS lock (think: deduct stock, then write a low-stock alert).
//   Worker A takes inventory then alerts.   Worker B takes alerts then inventory.
// Run 1 (BAD ORDER): each grabs its first lock, pauses, then waits forever for
//   the other's. All four Coffman conditions hold -> deadlock. We DETECT it with
//   timed lock attempts (try_lock_for) instead of hanging the demo.
// Run 2 (FIX = lock ordering): both take inventory first, then alerts. The
//   circular-wait condition can never form, so no deadlock.
// Run 3 (FIX = back-off): timed attempt, release everything and retry on failure.
//
// Usage: shrm_deadlock_demo        (exit code 0 = deadlock seen in run 1, none in 2 and 3)
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <atomic>

using namespace std::chrono_literals;
using TimedMutex = std::timed_mutex;

struct Result { bool aDone = false, bDone = false; };

static Result runBadOrder() {
    TimedMutex inventory, alerts; Result r; std::atomic<int> waited{0};
    // Each worker keeps its first lock until BOTH have finished waiting, so the
    // timeout below measures a true circular wait (not a lucky release).
    std::thread a([&] {
        std::unique_lock<TimedMutex> l1(inventory);
        std::this_thread::sleep_for(150ms);              // widen the window
        std::unique_lock<TimedMutex> l2(alerts, std::defer_lock);
        if (l2.try_lock_for(700ms)) r.aDone = true;       // timed so we can detect
        ++waited; while (waited < 2) std::this_thread::yield();
    });
    std::thread b([&] {
        std::unique_lock<TimedMutex> l1(alerts);
        std::this_thread::sleep_for(150ms);
        std::unique_lock<TimedMutex> l2(inventory, std::defer_lock);
        if (l2.try_lock_for(700ms)) r.bDone = true;
        ++waited; while (waited < 2) std::this_thread::yield();
    });
    a.join(); b.join(); return r;
}

static Result runOrdered() {
    TimedMutex inventory, alerts; Result r;
    auto worker = [&](bool& done) {
        std::unique_lock<TimedMutex> l1(inventory);       // ALWAYS inventory first
        std::this_thread::sleep_for(150ms);
        std::unique_lock<TimedMutex> l2(alerts, std::defer_lock);
        if (l2.try_lock_for(2000ms)) done = true;
    };
    std::thread a(worker, std::ref(r.aDone)), b(worker, std::ref(r.bDone));
    a.join(); b.join(); return r;
}

static Result runBackoff(int& retries) {
    TimedMutex inventory, alerts; Result r; std::atomic<int> rt{0};
    auto worker = [&](TimedMutex& first, TimedMutex& second, bool& done, int skew) {
        for (int attempt = 0; attempt < 20 && !done; ++attempt) {
            std::unique_lock<TimedMutex> l1(first);
            std::this_thread::sleep_for(150ms);
            std::unique_lock<TimedMutex> l2(second, std::defer_lock);
            if (l2.try_lock_for(100ms)) { done = true; return; }
            ++rt;                                         // failed: release l1 (scope exit) and retry
            l1.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(skew * (attempt + 1) * 40));
        }
    };
    std::thread a(worker, std::ref(inventory), std::ref(alerts), std::ref(r.aDone), 1);
    std::thread b(worker, std::ref(alerts), std::ref(inventory), std::ref(r.bDone), 3);
    a.join(); b.join(); retries = rt; return r;
}

int main() {
    int fails = 0;
    std::puts("Run 1: opposite lock order (A: inventory->alerts, B: alerts->inventory)");
    Result r1 = runBadOrder();
    bool dead = !r1.aDone && !r1.bDone;
    std::printf("  worker A finished: %s, worker B finished: %s\n  => %s\n\n", r1.aDone ? "yes" : "NO", r1.bDone ? "yes" : "NO",
                dead ? "DEADLOCK detected (circular wait, nobody can proceed)" : "no deadlock this time");
    if (!dead) ++fails;

    std::puts("Run 2: fix by lock ordering (both: inventory -> alerts)");
    Result r2 = runOrdered(); bool ok2 = r2.aDone && r2.bDone;
    std::printf("  worker A finished: %s, worker B finished: %s\n  => %s\n\n", r2.aDone ? "yes" : "NO", r2.bDone ? "yes" : "NO", ok2 ? "no deadlock" : "UNEXPECTED FAILURE");
    if (!ok2) ++fails;

    std::puts("Run 3: fix by back-off (opposite order, but timed try + release + retry)");
    int retries = 0; Result r3 = runBackoff(retries); bool ok3 = r3.aDone && r3.bDone;
    std::printf("  worker A finished: %s, worker B finished: %s, retries: %d\n  => %s\n\n", r3.aDone ? "yes" : "NO", r3.bDone ? "yes" : "NO", retries, ok3 ? "deadlock broken by releasing held lock" : "UNEXPECTED FAILURE");
    if (!ok3) ++fails;

    std::puts(fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
