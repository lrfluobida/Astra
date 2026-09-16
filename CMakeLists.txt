cmake_minimum_required(VERSION 3.10)
project(CoreGeek LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../bin")

set(ASTRA_SOURCES
    src/actions.cpp
    src/combat.cpp
    src/defense.cpp
    src/http_server.cpp
    src/json_io.cpp
    src/main.cpp
    src/match_log.cpp
    src/navigation.cpp
    src/protocol.cpp
    src/session.cpp
    src/strategy.cpp
    src/task_solver.cpp
    sdk/jsoncpp/jsoncpp.cpp)

find_package(Threads REQUIRED)
add_executable(CoreGeek ${ASTRA_SOURCES})
target_include_directories(CoreGeek PRIVATE src sdk/jsoncpp)
target_compile_options(CoreGeek PRIVATE -Wall -Wextra -Wpedantic -pedantic-errors)
target_link_libraries(CoreGeek PRIVATE Threads::Threads)
