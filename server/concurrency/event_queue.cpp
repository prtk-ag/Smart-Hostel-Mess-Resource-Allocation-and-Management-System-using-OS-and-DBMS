#include "event_queue.h"

void EventQueue::push(Event event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(event));
    }
    // Notify outside the lock is also fine here, but holding it through
    // notify_one() avoids a (harmless but pointless) race where a woken
    // consumer immediately blocks again because the producer hadn't
    // unlocked yet. Either ordering is correct; this one is simplest to
    // reason about.
    cv_.notify_one();
}

bool EventQueue::pop(Event& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    // Wake up when there's something to take, or we've been told to
    // shut down. The predicate form of wait() also protects against
    // spurious wakeups, which condition_variable is explicitly allowed
    // to produce.
    cv_.wait(lock, [this] { return !queue_.empty() || shutting_down_; });

    if (queue_.empty()) {
        // Only reachable if shutting_down_ is true and nothing is left
        // -- this is the consumer thread's exit signal.
        return false;
    }

    out = std::move(queue_.front());
    queue_.pop();
    return true;
}

void EventQueue::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutting_down_ = true;
    }
    // Every consumer is blocked in wait() on this condition variable;
    // notify_all (not notify_one) is required here so all of them wake
    // up and re-check the predicate, not just one.
    cv_.notify_all();
}

std::size_t EventQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}
