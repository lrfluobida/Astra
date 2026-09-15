cmake_minimum_required(VERSION 3.10)
project(CoreGeek LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ASTRA_SOURCES
    src/actions.cpp
    src/combat.cpp
    src/defense.cpp
    src/http_server.cpp
    src/json_io.cpp
    src/main.cpp
    src/navigation.cpp
    src/protocol.cpp
    src/session.cpp
    src/strategy.cpp
    src/task_solver.cpp)

set(OFFICIAL_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty")
set(LOCAL_JSONCPP "${CMAKE_CURRENT_SOURCE_DIR}/.tmp/jsoncpp-packages/root/usr")
if(EXISTS "${OFFICIAL_THIRD_PARTY}/include/json/json.h")
    set(JSONCPP_INCLUDE_DIR "${OFFICIAL_THIRD_PARTY}/include")
    find_library(JSONCPP_LIBRARY jsoncpp PATHS "${OFFICIAL_THIRD_PARTY}/lib" NO_DEFAULT_PATH)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin")
    set(ASTRA_INSTALL_RPATH "$ORIGIN/../ThirdParty/lib")
elseif(EXISTS "${LOCAL_JSONCPP}/include/jsoncpp/json/json.h")
    set(JSONCPP_INCLUDE_DIR "${LOCAL_JSONCPP}/include/jsoncpp")
    find_library(JSONCPP_LIBRARY jsoncpp PATHS "${LOCAL_JSONCPP}/lib/x86_64-linux-gnu" NO_DEFAULT_PATH)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(ASTRA_INSTALL_RPATH "${LOCAL_JSONCPP}/lib/x86_64-linux-gnu")
else()
    find_path(JSONCPP_INCLUDE_DIR json/json.h PATH_SUFFIXES jsoncpp)
    find_library(JSONCPP_LIBRARY jsoncpp)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

if(NOT JSONCPP_INCLUDE_DIR OR NOT JSONCPP_LIBRARY)
    message(FATAL_ERROR "jsoncpp was not found; place the official SDK at ../ThirdParty")
endif()

find_package(Threads REQUIRED)
add_executable(CoreGeek ${ASTRA_SOURCES})
target_include_directories(CoreGeek PRIVATE src "${JSONCPP_INCLUDE_DIR}")
target_compile_options(CoreGeek PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(CoreGeek PRIVATE Threads::Threads "${JSONCPP_LIBRARY}")
set_target_properties(CoreGeek PROPERTIES
    BUILD_RPATH "${ASTRA_INSTALL_RPATH}"
    INSTALL_RPATH "${ASTRA_INSTALL_RPATH}")
