cmake_minimum_required(VERSION 3.16)
project(bitecs C CXX)

if (NOT CONAN_BUILDING)
    set(CONAN_INSTALL_ARGS "--build=missing;-o;bench=True" CACHE STRING "Override default conan opts")
    include(cmake/conan2.cmake)
endif()

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

add_library(bitecs INTERFACE)
target_link_libraries(bitecs INTERFACE bitecs-core)
add_library(bitecs::bitecs ALIAS bitecs)
add_library(bitecs::core ALIAS bitecs-core)

if (BITECS_TEST)
    find_package(GTest REQUIRED)
    find_package(benchmark REQUIRED)
    enable_testing()

    add_executable(bitecs_test test.cpp)
    target_link_libraries(bitecs_test PRIVATE bitecs GTest::gtest_main)
    add_test(NAME bitecs_test COMMAND $<TARGET_FILE:bitecs_test>)

    add_executable(bitecs_bench test.cpp)
    target_link_libraries(bitecs_bench PRIVATE bitecs benchmark::benchmark_main)
    add_test(NAME bitecs_bench COMMAND $<TARGET_FILE:bitecs_bench>)
endif()

if(MSVC)
    target_compile_options(bitecs-core PRIVATE /W4)
else()
    target_compile_options(bitecs-core PRIVATE -Wall -Wextra -pedantic)
endif()


install(TARGETS bitecs-core bitecs)
install(DIRECTORY include/bitecs DESTINATION include)
install(FILES LICENSE DESTINATION .)
