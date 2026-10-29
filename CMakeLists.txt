cmake_minimum_required(VERSION 3.16)
project(SHRM CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Programs find schema.sql via this baked-in path, so they run from any folder
# (Visual Studio puts executables in build/Release, not the project root).
add_compile_definitions(SHRM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# --- SQLite: works on Linux, macOS and Windows -------------------------------
# 1) pkg-config (Linux, macOS, MSYS2/MinGW)   2) CMake's own FindSQLite3
#    (vcpkg, Conan, most installs)            3) download the official
#    amalgamation and build it ourselves (plain Windows with nothing installed).
# Whatever is found ends up in SQLITE3_INCLUDE_DIRS / SQLITE3_LIBRARIES.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SQLITE3 QUIET sqlite3)
endif()
if(NOT SQLITE3_FOUND)
    find_package(SQLite3 QUIET)
    if(SQLite3_FOUND)
        set(SQLITE3_INCLUDE_DIRS ${SQLite3_INCLUDE_DIRS})
        set(SQLITE3_LIBRARIES ${SQLite3_LIBRARIES})
        set(SQLITE3_FOUND TRUE)
    endif()
endif()
if(NOT SQLITE3_FOUND)
    message(STATUS "SQLite not installed -- downloading the official amalgamation (needs internet once)")
    include(FetchContent)
    FetchContent_Declare(sqlite_amalgamation
        URL https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip)
    FetchContent_MakeAvailable(sqlite_amalgamation)
    enable_language(C)
    add_library(sqlite3_local STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3_local PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
    set(SQLITE3_INCLUDE_DIRS ${sqlite_amalgamation_SOURCE_DIR})
    set(SQLITE3_LIBRARIES sqlite3_local)
endif()

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

# Phase 8: IPC. Event codec + channel (stdio / named pipe, POSIX and Windows),
# the generator process, the IPC server, and a test for the IPC layer.
add_library(shrm_ipc STATIC
    server/ipc/event_codec.cpp
    server/ipc/channel.cpp
    server/concurrency/event_queue.cpp
)
target_include_directories(shrm_ipc PUBLIC server/ipc server/concurrency)
target_link_libraries(shrm_ipc PUBLIC Threads::Threads)

add_executable(shrm_event_generator server/event_generator.cpp)
target_link_libraries(shrm_event_generator PRIVATE shrm_ipc)

add_executable(shrm_ipc_server
    server/ipc_server.cpp
    server/db/db_manager.cpp
    server/db/seed_data.cpp
    server/concurrency/workers.cpp
)
target_include_directories(shrm_ipc_server PRIVATE server/include ${SQLITE3_INCLUDE_DIRS})
target_link_libraries(shrm_ipc_server PRIVATE shrm_ipc ${SQLITE3_LIBRARIES})

add_executable(shrm_ipc_test tests/test_ipc.cpp)
target_link_libraries(shrm_ipc_test PRIVATE shrm_ipc)

# Phase 8b: dashboard helper -- scheduler results as JSON.
add_executable(shrm_scheduler_json server/scheduler_json.cpp)
target_link_libraries(shrm_scheduler_json PRIVATE shrm_scheduler)
