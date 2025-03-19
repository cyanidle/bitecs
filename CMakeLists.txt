cmake_minimum_required(VERSION 3.16)
project(bitecs C CXX)

include(CPM.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 17)

option(BITECS_TEST "Build tests" ON)

add_library(bitecs-core src/bitecs_core.c)
target_include_directories(bitecs-core PUBLIC include)

CPMAddPackage("gh:doctest/doctest@2.4.11")

add_library(bitecs INTERFACE)
target_link_libraries(bitecs INTERFACE bitecs-core doctest::doctest)


if (BITECS_TEST)
    enable_testing()
    add_executable(bitecs_test test.cpp)
    target_link_libraries(bitecs_test PRIVATE bitecs)
    add_test(NAME basic COMMAND $<TARGET_FILE:bitecs_test>)
endif()

if(MSVC)
    target_compile_options(bitecs-core PRIVATE /W4)
else()
    target_compile_options(bitecs-core PRIVATE -Wall -Wextra -pedantic)
endif()
