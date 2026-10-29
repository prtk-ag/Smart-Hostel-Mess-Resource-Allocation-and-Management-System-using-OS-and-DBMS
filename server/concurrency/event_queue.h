#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

// A single unit of work moving through the system: something a
// worker thread should apply to the database. This shape is deliberately
// the same information the event generator (Phase 4's IPC layer) will
// eventually send over the named pipe -- the queue doesn't care whether
// an event came from a real socket/pipe or was pushed directly by a
// thread, which is what lets us test the queue on its own here before
// IPC exists.
struct Event {
    enum class Type { Attendance, Prep, Waste };

    Type type;

    // Attendance: student_id + meal_id
    // Prep:       meal_id + item_id + quantity + prepared_by
    // Waste:      meal_id + item_id + quantity
    int student_id = 0;
    int meal_id = 0;
    int item_id = 0;
    double quantity = 0.0;
    std::string prepared_by;

    static Event makeAttendance(int student_id, int meal_id) {
        Event e;
        e.type = Type::Attendance;
        e.student_id = student_id;
        e.meal_id = meal_id;
        return e;
    }

    static Event makePrep(int meal_id, int item_id, double quantity, std::string prepared_by) {
        Event e;
        e.type = Type::Prep;
        e.meal_id = meal_id;
        e.item_id = item_id;
        e.quantity = quantity;
        e.prepared_by = std::move(prepared_by);
        return e;
    }

    static Event makeWaste(int meal_id, int item_id, double quantity) {
        Event e;
        e.type = Type::Waste;
        e.meal_id = meal_id;
        e.item_id = item_id;
        e.quantity = quantity;
        return e;
    }
};

// A bounded-free, thread-safe FIFO queue of Events, built around the
// standard mutex + condition_variable producer-consumer pattern.
//
// Multiple producer threads call push() concurrently; multiple consumer
// threads call pop() concurrently, each blocking until an event is
// available. shutdown() is how the main thread tells every blocked
// consumer to wake up and exit once producers are done, instead of
// hanging forever on an empty queue -- without it, join()ing the worker
// threads at program exit would deadlock.
class EventQueue {
public:
    // Adds an event to the back of the queue and wakes one waiting
    // consumer (if any). Safe to call from any number of threads.
    void push(Event event);

    // Blocks until an event is available or the queue is shut down.
    // Returns true and fills `out` if an event was popped; returns
    // false (leaving `out` untouched) once the queue is shut down
    // AND drained -- that's the consumer thread's signal to exit.
    bool pop(Event& out);

    // Wakes every thread blocked in pop(). Events already in the
    // queue are still delivered; pop() only starts returning false
    // once the queue is both shut down and empty.
    void shutdown();

    std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Event> queue_;
    bool shutting_down_ = false;
};
