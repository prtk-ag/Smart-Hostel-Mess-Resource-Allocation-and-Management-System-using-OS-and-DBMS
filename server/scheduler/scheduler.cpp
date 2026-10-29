#include "scheduler.h"

#include <algorithm>
#include <iomanip>
#include <string>

ScheduleResult buildResult(const std::string& algorithm, std::vector<JobResult> executed) {
    ScheduleResult result;
    result.algorithm = algorithm;

    int previous_end = 0;   // idle time is measured from t=0, so a late first arrival counts too
    double total_waiting = 0.0, total_turnaround = 0.0, total_response = 0.0;

    for (auto& r : executed) {
        r.completion_time = r.start_time + r.job.burst_time;
        r.waiting_time = r.start_time - r.job.arrival_time;
        r.turnaround_time = r.completion_time - r.job.arrival_time;
        r.response_time = r.start_time - r.job.arrival_time;

        if (r.start_time > previous_end) {
            result.idle_time += r.start_time - previous_end;
        }
        previous_end = r.completion_time;

        total_waiting += r.waiting_time;
        total_turnaround += r.turnaround_time;
        total_response += r.response_time;
    }

    if (!executed.empty()) {
        const double n = static_cast<double>(executed.size());
        result.avg_waiting_time = total_waiting / n;
        result.avg_turnaround_time = total_turnaround / n;
        result.avg_response_time = total_response / n;
        result.makespan = previous_end;
    }
    result.jobs = std::move(executed);
    return result;
}

ScheduleResult scheduleFCFS(std::vector<Job> jobs) {
    // Arrival order, ties broken by id. stable_sort + the explicit id
    // tiebreak keeps this deterministic regardless of input order.
    std::stable_sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) {
        if (a.arrival_time != b.arrival_time) return a.arrival_time < b.arrival_time;
        return a.id < b.id;
    });

    std::vector<JobResult> executed;
    executed.reserve(jobs.size());

    int clock = 0;
    for (const Job& job : jobs) {
        JobResult r;
        r.job = job;
        // If the job hasn't arrived yet, the station idles until it does.
        r.start_time = std::max(clock, job.arrival_time);
        clock = r.start_time + job.burst_time;
        executed.push_back(r);
    }

    return buildResult("FCFS", std::move(executed));
}

namespace {

struct Segment {
    std::string label;
    int start;
    int end;
};

// Execution order -> Gantt segments, with explicit "idle" segments in any gap.
std::vector<Segment> toSegments(const ScheduleResult& result) {
    std::vector<Segment> segments;
    int cursor = 0;
    for (const auto& r : result.jobs) {
        if (r.start_time > cursor) {
            segments.push_back({"idle", cursor, r.start_time});
        }
        segments.push_back({r.job.name, r.start_time, r.completion_time});
        cursor = r.completion_time;
    }
    return segments;
}

} // namespace

void printSchedule(const ScheduleResult& result, std::ostream& out) {
    out << "Algorithm: " << result.algorithm << "\n";

    // --- Metrics table -------------------------------------------------
    const int name_w = 24;
    out << std::left << std::setw(4) << "ID" << std::setw(name_w) << "Dish"
        << std::right << std::setw(9) << "Arrival" << std::setw(7) << "Burst"
        << std::setw(7) << "Start" << std::setw(7) << "End"
        << std::setw(9) << "Waiting" << std::setw(12) << "Turnaround" << "\n";
    out << std::string(4 + name_w + 9 + 7 + 7 + 7 + 9 + 12, '-') << "\n";
    for (const auto& r : result.jobs) {
        out << std::left << std::setw(4) << r.job.id << std::setw(name_w) << r.job.name
            << std::right << std::setw(9) << r.job.arrival_time << std::setw(7) << r.job.burst_time
            << std::setw(7) << r.start_time << std::setw(7) << r.completion_time
            << std::setw(9) << r.waiting_time << std::setw(12) << r.turnaround_time << "\n";
    }
    out << std::fixed << std::setprecision(2);
    out << "\nAverage waiting time:    " << result.avg_waiting_time << " min\n";
    out << "Average turnaround time: " << result.avg_turnaround_time << " min\n";
    out << "Average response time:   " << result.avg_response_time << " min\n";
    out << "Makespan:                " << result.makespan << " min"
        << "   (station idle for " << result.idle_time << " min)\n\n";

    // --- Gantt chart ---------------------------------------------------
    const auto segments = toSegments(result);
    if (segments.empty()) {
        out << "(no jobs)\n";
        return;
    }

    std::vector<int> widths;
    for (const auto& s : segments) {
        widths.push_back(std::max<int>(static_cast<int>(s.label.size()) + 2, 7));
    }

    auto border = [&] {
        for (int w : widths) out << '+' << std::string(w, '-');
        out << "+\n";
    };

    border();
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const int w = widths[i];
        const int pad_total = w - static_cast<int>(segments[i].label.size());
        const int left = pad_total / 2;
        out << '|' << std::string(left, ' ') << segments[i].label
            << std::string(pad_total - left, ' ');
    }
    out << "|\n";
    border();

    // Time labels sit under each segment's left border; the last one marks the end.
    std::string times;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const std::string t = std::to_string(segments[i].start);
        times += t + std::string(static_cast<std::size_t>(widths[i] + 1) > t.size()
                                      ? widths[i] + 1 - t.size() : 1, ' ');
    }
    times += std::to_string(segments.back().end);
    out << times << "\n";
}
