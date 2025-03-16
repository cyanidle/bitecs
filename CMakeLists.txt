cmake_minimum_required(VERSION 3.16)
project(bitecs C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 17)

add_library(bitecs-core src/bitecs_core.c)
target_include_directories(bitecs-core PUBLIC include)

add_library(bitecs INTERFACE)
target_link_libraries(bitecs INTERFACE bitecs-core)

add_executable(test test.cpp)
target_link_libraries(test PRIVATE bitecs)
