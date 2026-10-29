cmake_minimum_required(VERSION 3.16)
project(SHRM CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# Threads will be linked once Phase 2 (concurrency layer) lands.
find_package(Threads REQUIRED)

add_executable(shrm_server
    server/main.cpp
    server/db/db_manager.cpp
)

target_include_directories(shrm_server PRIVATE
    server/include
    ${SQLITE3_INCLUDE_DIRS}
)

target_link_libraries(shrm_server PRIVATE
    ${SQLITE3_LIBRARIES}
    Threads::Threads
)

# Phase 2: seed-data CLI -- populates a fresh DB with sample students,
# inventory, a week's menu, and today's meals/attendance.
add_executable(shrm_seed
    server/seed_main.cpp
    server/db/db_manager.cpp
    server/db/seed_data.cpp
)

target_include_directories(shrm_seed PRIVATE
    server/include
    ${SQLITE3_INCLUDE_DIRS}
)

target_link_libraries(shrm_seed PRIVATE
    ${SQLITE3_LIBRARIES}
    Threads::Threads
)

# Phase 3: thread-safe producer-consumer event queue + its test.
# No DB/sqlite dependency yet -- this validates the queue in isolation
# before Phase 4 wires real DB-writing worker threads on top of it.
add_executable(shrm_queue_test
    tests/test_event_queue.cpp
    server/concurrency/event_queue.cpp
)

target_include_directories(shrm_queue_test PRIVATE
    server/concurrency
)

target_link_libraries(shrm_queue_test PRIVATE
    Threads::Threads
)

# Phase 4: worker threads wired to the Phase 3 queue, writing to the DB
# through DbManager -- the actual attendance/prep/waste pipeline.
add_executable(shrm_pipeline_demo
    server/pipeline_demo.cpp
    server/db/db_manager.cpp
    server/db/seed_data.cpp
    server/concurrency/event_queue.cpp
    server/concurrency/workers.cpp
)

target_include_directories(shrm_pipeline_demo PRIVATE
    server/include
    server/concurrency
    ${SQLITE3_INCLUDE_DIRS}
)

target_link_libraries(shrm_pipeline_demo PRIVATE
    ${SQLITE3_LIBRARIES}
    Threads::Threads
)

# Phase 4/5: race condition demo -- logPrepUnsafe (reproduces the lost
# update) vs logPrep (the transactional fix), same concurrent workload.
add_executable(shrm_race_demo
    tests/test_race_condition.cpp
    server/db/db_manager.cpp
)

target_include_directories(shrm_race_demo PRIVATE
    server/include
    ${SQLITE3_INCLUDE_DIRS}
)

target_link_libraries(shrm_race_demo PRIVATE
    ${SQLITE3_LIBRARIES}
    Threads::Threads
)

# Phase 6: scheduler -- Job model, shared metrics, FCFS. Pure C++ with no
# DB or threading dependency, so it builds and tests on its own. Phase 7
# adds SJF and Priority to the same library.
add_library(shrm_scheduler STATIC
    server/scheduler/scheduler.cpp
)

target_include_directories(shrm_scheduler PUBLIC
    server/scheduler
)

add_executable(shrm_scheduler_demo
    server/scheduler_demo.cpp
)

target_link_libraries(shrm_scheduler_demo PRIVATE
    shrm_scheduler
)

add_executable(shrm_scheduler_test
    tests/test_scheduler.cpp
)

target_link_libraries(shrm_scheduler_test PRIVATE
    shrm_scheduler
)
