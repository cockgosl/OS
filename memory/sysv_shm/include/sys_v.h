#ifndef SYS_V_H
#define SYS_V_H

#include <stdbool.h>
#include <stddef.h>

#define IPC_KEY_FILE "ipc.key"

enum IpcProjectId { SHM_PROJECT_ID = 'M', SEM_PROJECT_ID = 'S' };

enum SemaphoreIndex { SEM_FULL = 0, SEM_EMPTY = 1, SEM_COUNT = 2 };

/* EMPTY and FULL transfer exclusive ownership of this single buffer. */
struct SharedData {
    size_t capacity;
    size_t size;
    bool finished;
    bool failed;
    char buff[];
};

#endif
