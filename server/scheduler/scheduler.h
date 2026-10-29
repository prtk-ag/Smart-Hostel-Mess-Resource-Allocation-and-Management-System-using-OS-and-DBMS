#pragma once

#include "job.h"
#include <ostream>
#include <vector>

// First-Come, First-Served: jobs run to completion in order of arrival. Ties
// (same arrival time) are broken by job id so the result is deterministic.
// Non-preemptive. If the next job hasn't arrived yet when the previous one
// finishes, the station sits idle until it does -- that gap is recorded in
// ScheduleResult::idle_time and shows up as "idle" in the Gantt chart.
//
// Takes the jobs by value: the caller's list is left untouched, which lets
// Phase 7 run several algorithms over the same input and compare them.
ScheduleResult scheduleFCFS(std::vector<Job> jobs);

// Fills in the per-job waiting/turnaround/response times and the averages
// for a list of jobs that have already been placed in execution order with
// start times chosen. Shared by every algorithm so they can't disagree on
// how a metric is defined -- Phase 7's SJF and Priority reuse this as-is.
ScheduleResult buildResult(const std::string& algorithm, std::vector<JobResult> executed);

// Console output: a metrics table, then a Gantt chart of the execution order.
void printSchedule(const ScheduleResult& result, std::ostream& out);
