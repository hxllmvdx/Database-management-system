cmake_minimum_required(VERSION 3.20)

project(
    CourseDB
    VERSION 0.1.0
    DESCRIPTION "Educational DBMS with B*-tree storage engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(COURSEDB_BUILD_TESTS "Build tests" ON)
option(COURSEDB_BUILD_APPS "Build applications" ON)
option(COURSEDB_BUILD_EXAMPLES "Build examples" OFF)
option(COURSEDB_ENABLE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE COURSEDB_TEST_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/tests/*_tests.cpp
)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_library(coursedb_project_options INTERFACE)

target_compile_options(coursedb_project_options INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Woverloaded-virtual
    >

    $<$<CXX_COMPILER_ID:MSVC>:
        /W4
        /permissive-
    >
)

if(COURSEDB_ENABLE_WARNINGS_AS_ERRORS)
    target_compile_options(coursedb_project_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>
    )
endif()

include(FetchContent)

if(COURSEDB_BUILD_TESTS AND COURSEDB_TEST_SOURCES)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)
endif()

add_subdirectory(src)

if(COURSEDB_BUILD_APPS)
    add_subdirectory(apps)
endif()

if(COURSEDB_BUILD_TESTS AND COURSEDB_TEST_SOURCES)
    enable_testing()
    add_subdirectory(tests)
endif()

if(COURSEDB_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
