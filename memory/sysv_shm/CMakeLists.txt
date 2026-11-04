add_library(ipc_utils STATIC src/ipc_utils.c)
target_include_directories(ipc_utils PUBLIC include)
target_link_libraries(ipc_utils PUBLIC ipc_transfer)

add_executable(shm_sender src/sender.c)
target_link_libraries(shm_sender PRIVATE ipc_utils)

add_executable(shm_receiver src/receiver.c)
target_link_libraries(shm_receiver PRIVATE ipc_utils)
