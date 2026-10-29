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
