cmake_minimum_required(VERSION 2.8)
project(tsh)

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_FLAGS "-Wall -Wextra -std=gnu99")

include_directories(.)

add_executable(tsh src/main.c src/utils.c src/utils.h)

set_target_properties(tsh PROPERTIES
        OUTPUT_NAME "tsh"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
