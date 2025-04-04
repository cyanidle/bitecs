cmake_minimum_required(VERSION 3.16)
project(bitecs C CXX)

if (NOT CONAN_BUILDING)
    set(CONAN_INSTALL_ARGS "--build=missing;-o;bench=True" CACHE STRING "Override default conan opts")
    include(cmake/conan2.cmake)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 17)

option(BITECS_TEST "Build tests" OFF)
option(BITECS_BENCH "Build bench" OFF)

add_library(bitecs-core src/bitecs_core.c)

if (CMAKE_COMPILER_IS_GNUCC)
    target_compile_options(bitecs-core PRIVATE
        -fexceptions
    )
endif()

target_include_directories(bitecs-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

add_library(bitecs INTERFACE)
target_link_libraries(bitecs INTERFACE bitecs-core)
add_library(bitecs::bitecs ALIAS bitecs)
add_library(bitecs::core ALIAS bitecs-core)

if (BITECS_TEST)
    message(STATUS "Building tests")
    enable_testing()

    find_package(GTest REQUIRED)

    add_executable(bitecs_test tests/test.cpp)
    target_link_libraries(bitecs_test PRIVATE bitecs GTest::gtest_main)
    add_test(NAME bitecs_test COMMAND $<TARGET_FILE:bitecs_test>)

endif()

if (BITECS_BENCH)
    message(STATUS "Building benchmarks")
    find_package(benchmark REQUIRED)
    find_package(gsl-lite REQUIRED)
    find_package(flecs REQUIRED)
    find_package(gaia REQUIRED)
    find_package(fastprng REQUIRED)
    find_package(EnTT REQUIRED)
    find_package(entityx REQUIRED)

    file(GLOB benches CONFIGURE_DEPENDS benchmarks/*.cpp)
    add_executable(bitecs_bench ${benches})
    target_link_libraries(bitecs_bench PRIVATE
        bitecs
        benchmark::benchmark_main
        gsl::gsl-lite
        EnTT::EnTT
        entityx::entityx
        gaia::gaia
        flecs::flecs_static
        fastprng::fastprng
    )
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
