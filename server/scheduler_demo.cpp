// Phase 6/7: runs FCFS, SJF and Priority scheduling over the same lunch-prep
// workload, prints each schedule, then a side-by-side comparison.
//
// The jobs are the dishes for one lunch service. Arrival = when the dish can
// start (ingredients out), burst = rough minutes to prepare, priority =
// how urgent it is for service (1 = most urgent).
//
// Usage: ./shrm_scheduler_demo

#include "scheduler.h"
#include <iomanip>
#include <iostream>

int main() {
    std::cout << "SHRM Phase 7 -- FCFS vs SJF vs Priority scheduling of lunch prep\n";
    std::cout << "==================================================================\n\n";

    std::vector<Job> lunch_prep;
    auto add = [&](int id, const char* name, int arrival, int burst, int priority) {
        Job j;
        j.id = id;
        j.name = name;
        j.arrival_time = arrival;
        j.burst_time = burst;
        j.priority = priority;
        lunch_prep.push_back(j);
    };
    // id, dish, arrival (min), burst (min), priority (1 = most urgent)
    add(1, "Rice",       0, 25, 2);
    add(2, "Dal Tadka",  2, 35, 1);
    add(3, "Roti",       4, 40, 2);
    add(4, "Mixed Veg",  6, 20, 4);
    add(5, "Salad",      9,  8, 3);
    add(6, "Raita",     12, 10, 3);

    std::cout << "Workload: " << lunch_prep.size() << " dishes, one cooking station.\n\n";

    const ScheduleResult results[] = {scheduleFCFS(lunch_prep), scheduleSJF(lunch_prep),
                                      schedulePriority(lunch_prep)};
    for (const auto& r : results) {
        printSchedule(r, std::cout);
        std::cout << "\n";
    }

    std::cout << "Comparison (minutes)\n";
    std::cout << std::left << std::setw(12) << "Algorithm" << std::right << std::setw(11) << "Avg wait"
              << std::setw(16) << "Avg turnaround" << std::setw(10) << "Makespan" << "\n";
    std::cout << std::string(49, '-') << "\n" << std::fixed << std::setprecision(2);
    for (const auto& r : results) {
        std::cout << std::left << std::setw(12) << r.algorithm << std::right << std::setw(11)
                  << r.avg_waiting_time << std::setw(16) << r.avg_turnaround_time << std::setw(10)
                  << r.makespan << "\n";
    }

    std::cout << "\nTakeaways:\n"
                 " - Makespan is identical: all three keep the station busy; only the ORDER changes.\n"
                 " - SJF has the lowest average wait (short Salad/Raita jump ahead of Roti)\n"
                 "   but pushes the 35-40 min dishes to the end and can starve them.\n"
                 " - Priority serves the urgent Dal Tadka first. Its average wait only beats FCFS slightly\n"
                 "   because urgency ignores job length; Mixed Veg (lowest priority) waits longest.\n"
                 "   Real kitchens need aging so low-priority dishes don't starve.\n";
    return 0;
}
