cmake_minimum_required(VERSION 3.21)

project(snailtrail
    VERSION 1.0.0
    DESCRIPTION "Every slow query leaves a trail: a MySQL slow-log analyzer and index advisor in modern C++20"
    HOMEPAGE_URL "https://github.com/WaynerMoraes12/snailtrail"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SNAILTRAIL_BUILD_CLI "Build the snailtrail command-line tool" ON)
option(SNAILTRAIL_BUILD_TESTS "Build the GoogleTest suite" ON)
option(SNAILTRAIL_BUILD_PYTHON "Build the pybind11 extension module" OFF)
option(SNAILTRAIL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(SNAILTRAIL_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(SNAILTRAIL_STATIC "Link the CLI statically" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(SnailTrailHelpers)

add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8;/Zc:preprocessor;/Zc:__cplusplus>")
add_compile_definitions($<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
snailtrail_enable_sanitizers()

add_subdirectory(core)

if(SNAILTRAIL_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(SNAILTRAIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SNAILTRAIL_BUILD_PYTHON)
    add_subdirectory(bindings/python)
endif()
