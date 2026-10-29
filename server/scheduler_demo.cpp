// Phase 6: runs the FCFS scheduler over a realistic lunch-prep workload and
// prints the metrics table and Gantt chart.
//
// The jobs are the dishes for one lunch service. Arrival times model when
// each dish becomes ready to start (ingredients out, previous prep done);
// burst times are rough minutes to prepare. Priority is filled in now so
// Phase 7 (SJF and Priority scheduling) can run on exactly the same
// workload and the three algorithms can be compared like for like.
//
// Usage: ./shrm_scheduler_demo

#include "scheduler.h"
#include <iostream>

int main() {
    std::cout << "SHRM Phase 6 -- FCFS scheduling of lunch prep\n";
    std::cout << "===============================================\n\n";

    // id, dish, arrival (min), burst (min), priority (1 = most urgent)
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
    add(1, "Rice",           0, 25, 2);
    add(2, "Dal Tadka",      2, 35, 1);
    add(3, "Roti",           4, 40, 1);
    add(4, "Mixed Veg",      6, 20, 3);
    add(5, "Salad",          9,  8, 4);
    add(6, "Raita",         12, 10, 4);

    std::cout << "Workload: " << lunch_prep.size() << " dishes, one cooking station.\n\n";

    ScheduleResult fcfs = scheduleFCFS(lunch_prep);
    printSchedule(fcfs, std::cout);

    std::cout << "\nNote: FCFS never reorders -- Salad (8 min) and Raita (10 min) wait behind the\n"
                 "long Roti job. Phase 7 adds SJF and Priority so the trade-off can be compared.\n";
    return 0;
}
