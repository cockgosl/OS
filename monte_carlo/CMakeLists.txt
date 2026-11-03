cmake_minimum_required(VERSION 3.16)

project(monte_carlo_ipc LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

add_library(monte_carlo_common STATIC src/monte_carlo_common.c)
target_include_directories(monte_carlo_common PUBLIC include)
target_compile_definitions(monte_carlo_common PUBLIC _POSIX_C_SOURCE=200809L)
# The public header contains pthread types.
target_link_libraries(monte_carlo_common PUBLIC Threads::Threads)

add_executable(monte_carlo_a src/monte_carlo_a.c)
target_link_libraries(monte_carlo_a PRIVATE monte_carlo_common)

add_executable(monte_carlo_b src/monte_carlo_b.c)
target_link_libraries(monte_carlo_b PRIVATE monte_carlo_common)

foreach(target IN ITEMS monte_carlo_common monte_carlo_a monte_carlo_b)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
