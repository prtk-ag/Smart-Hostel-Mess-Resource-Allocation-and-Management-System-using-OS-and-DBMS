#pragma once

#include <string>
#include <vector>

// One unit of kitchen work waiting to be scheduled -- in SHRM terms, a dish
// that has to be prepared (e.g. "Rice", "Dal Tadka"). The fields are the
// classic CPU-scheduling job model, so the metrics we report (waiting time,
// turnaround time) mean exactly what they mean in the OS textbook.
//
// All times are in minutes from the start of the prep shift.
struct Job {
    int id = 0;
    std::string name;
    int arrival_time = 0;   // when the dish became ready to start (menu finalized, ingredients out)
    int burst_time = 0;     // how long it takes to prepare, start to finish
    int priority = 0;       // lower number = more urgent (used by Priority scheduling in Phase 7)
};

// What happened to one job once a scheduling algorithm has run.
struct JobResult {
    Job job;
    int start_time = 0;        // first moment the job got the "CPU" (a cooking station)
    int completion_time = 0;   // start_time + burst_time (these jobs are non-preemptive)
    int waiting_time = 0;      // start_time - arrival_time: time spent queued before starting
    int turnaround_time = 0;   // completion_time - arrival_time: total time in the system
    int response_time = 0;     // start_time - arrival_time (equal to waiting for non-preemptive jobs)
};

// The outcome of running one algorithm over a set of jobs. `jobs` is in
// EXECUTION order, which is what the Gantt chart prints.
struct ScheduleResult {
    std::string algorithm;
    std::vector<JobResult> jobs;
    double avg_waiting_time = 0.0;
    double avg_turnaround_time = 0.0;
    double avg_response_time = 0.0;
    int makespan = 0;          // completion time of the last job
    int idle_time = 0;         // total time the station sat idle waiting for jobs to arrive
};
