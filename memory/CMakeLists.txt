cmake_minimum_required(VERSION 3.16)

project(ipc_transfer LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(ipc_transfer INTERFACE)
target_compile_definitions(ipc_transfer INTERFACE _POSIX_C_SOURCE=200809L)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ipc_transfer INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(fifo)
add_subdirectory(sysv_shm)
add_subdirectory(sysv_queue)
