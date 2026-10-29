// Prints the FCFS / SJF / Priority schedules for the lunch workload as JSON,
// so the dashboard draws exactly what the C++ scheduler computed (no second
// implementation to drift out of sync).
// Usage: shrm_scheduler_json

#include "scheduler.h"
#include <iostream>

namespace {
void printAlgo(const ScheduleResult& r, bool last) {
    std::cout << "    {\"algorithm\": \"" << r.algorithm << "\", \"avg_waiting\": " << r.avg_waiting_time
              << ", \"avg_turnaround\": " << r.avg_turnaround_time << ", \"makespan\": " << r.makespan
              << ", \"idle\": " << r.idle_time << ", \"jobs\": [\n";
    for (std::size_t i = 0; i < r.jobs.size(); ++i) {
        const auto& j = r.jobs[i];
        std::cout << "      {\"id\": " << j.job.id << ", \"name\": \"" << j.job.name << "\", \"arrival\": "
                  << j.job.arrival_time << ", \"burst\": " << j.job.burst_time << ", \"priority\": " << j.job.priority
                  << ", \"start\": " << j.start_time << ", \"end\": " << j.completion_time << ", \"waiting\": "
                  << j.waiting_time << "}" << (i + 1 < r.jobs.size() ? "," : "") << "\n";
    }
    std::cout << "    ]}" << (last ? "" : ",") << "\n";
}
} // namespace

int main() {
    std::vector<Job> jobs;
    auto add = [&](int id, const char* name, int arrival, int burst, int priority) {
        Job j; j.id = id; j.name = name; j.arrival_time = arrival; j.burst_time = burst; j.priority = priority;
        jobs.push_back(j);
    };
    add(1, "Rice", 0, 25, 2);  add(2, "Dal Tadka", 2, 35, 1); add(3, "Roti", 4, 40, 2);
    add(4, "Mixed Veg", 6, 20, 4); add(5, "Salad", 9, 8, 3);  add(6, "Raita", 12, 10, 3);

    std::cout << "{\"schedules\": [\n";
    printAlgo(scheduleFCFS(jobs), false);
    printAlgo(scheduleSJF(jobs), false);
    printAlgo(schedulePriority(jobs), true);
    std::cout << "]}\n";
    return 0;
}
