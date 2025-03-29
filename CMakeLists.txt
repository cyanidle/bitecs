cmake_minimum_required(VERSION 3.16)
project(bitecs C CXX)

include(CPM.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 17)

option(BITECS_TEST "Build tests" OFF)

add_library(bitecs-core
    src/bitecs_core.c
    src/bitecs_mask.c
    src/bitecs_private.h
)
target_compile_options(bitecs-core PRIVATE -fexceptions)
target_include_directories(bitecs-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

CPMAddPackage("gh:doctest/doctest@2.4.11")

add_library(bitecs INTERFACE)
target_link_libraries(bitecs INTERFACE bitecs-core doctest::doctest)
add_library(bitecs::bitecs ALIAS bitecs)
add_library(bitecs::core ALIAS bitecs-core)

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
