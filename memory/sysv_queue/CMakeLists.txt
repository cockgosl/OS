add_library(queue_utils STATIC src/queue_utils.c)
target_include_directories(queue_utils PUBLIC include)
target_link_libraries(queue_utils PUBLIC ipc_utils)

add_executable(queue_sender src/sender.c)
target_link_libraries(queue_sender PRIVATE queue_utils)

add_executable(queue_receiver src/receiver.c)
target_link_libraries(queue_receiver PRIVATE queue_utils)
