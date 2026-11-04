#include "ipc_utils.h"
#include "sys_v.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sem.h>
#include <sys/shm.h>

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static int parseBufferSize(const char *text, size_t *buffer_size) {
    if (text == NULL || *text == '\0') {
        fprintf(stderr, "Buffer size must be a positive integer\n");
        return -1;
    }

    for (const char *digit = text; *digit != '\0'; ++digit) {
        if (*digit < '0' || *digit > '9') {
            fprintf(stderr, "Invalid buffer size: %s\n", text);
            return -1;
        }
    }

    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);

    if (errno == ERANGE || end == text || *end != '\0' || value == 0 ||
        value > SIZE_MAX) {
        fprintf(stderr, "Invalid buffer size: %s\n", text);
        return -1;
    }

    *buffer_size = (size_t)value;
    return 0;
}

static int prepareSharedMemory(key_t shm_key, int *shmid, size_t buffer_size,
                               struct SharedData **data) {
    if (buffer_size == 0 ||
        buffer_size > SIZE_MAX - sizeof(struct SharedData)) {
        fprintf(stderr, "Shared memory size is invalid or overflows size_t\n");
        return -1;
    }

    *shmid = shmget(shm_key, sizeof(struct SharedData) + buffer_size,
                    IPC_CREAT | IPC_EXCL | 0600);
    if (*shmid == -1) {
        perror("shmget");
        return -1;
    }

    void *memory = shmat(*shmid, NULL, 0);
    if (memory == (void *)-1) {
        perror("shmat");
        return -1;
    }

    *data = memory;
    (*data)->capacity = buffer_size;
    return 0;
}

static int prepareSemaphoreSet(key_t sem_key, int *semid) {
    *semid = semget(sem_key, SEM_COUNT, IPC_CREAT | IPC_EXCL | 0600);
    if (*semid == -1) {
        perror("semget");
        return -1;
    }

    unsigned short values[SEM_COUNT] = {[SEM_FULL] = 0, [SEM_EMPTY] = 1};

    union semun argument = {.array = values};

    if (semctl(*semid, 0, SETALL, argument) == -1) {
        perror("semctl SETALL");
        return -1;
    }

    return 0;
}

static int sendFile(struct SharedData *data, FILE *input, int semid) {
    if (waitSemaphore(semid, SEM_EMPTY) == -1) {
        return -1;
    }

    while (true) {
        size_t bytes_read = fread(data->buff, 1, data->capacity, input);
        if (ferror(input)) {
            perror("fread");
            return -1;
        }
        if (bytes_read == 0 && !feof(input)) {
            fprintf(stderr, "fread returned no data without EOF\n");
            return -1;
        }

        data->size = bytes_read;
        data->finished = feof(input) != 0;
        bool finished = data->finished;
        if (postSemaphore(semid, SEM_FULL) == -1 ||
            waitSemaphore(semid, SEM_EMPTY) == -1) {
            return -1;
        }
        if (data->failed) {
            fprintf(stderr, "Receiver reported a write error\n");
            return -1;
        }
        if (finished) {
            return 0;
        }
    }
}

static int cleanup(struct SharedData *data, int shmid, int semid) {
    int result = 0;
    if (detachSharedMemory(data) == -1) {
        result = -1;
    }
    if (removeSemaphoreSet(semid) == -1) {
        result = -1;
    }
    if (removeSharedMemory(shmid) == -1) {
        result = -1;
    }
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_file> <buffer_size>\n", argv[0]);
        return EXIT_FAILURE;
    }

    size_t buffer_size;
    if (parseBufferSize(argv[2], &buffer_size) == -1 ||
        buffer_size > SIZE_MAX - sizeof(struct SharedData)) {
        fprintf(stderr, "Invalid shared-memory buffer size\n");
        return EXIT_FAILURE;
    }
    FILE *input = fopen(argv[1], "rb");
    if (input == NULL) {
        perror("fopen input");
        return EXIT_FAILURE;
    }

    int result = -1;
    int shmid = -1;
    int semid = -1;
    struct SharedData *data = NULL;
    key_t shm_key = getIpcKey(IPC_KEY_FILE, SHM_PROJECT_ID);
    key_t sem_key = getIpcKey(IPC_KEY_FILE, SEM_PROJECT_ID);
    if (shm_key == (key_t)-1 || sem_key == (key_t)-1) {
        goto done;
    }
    if (prepareSharedMemory(shm_key, &shmid, buffer_size, &data) == -1) {
        goto done;
    }
    data->size = 0;
    data->finished = false;
    data->failed = false;
    if (prepareSemaphoreSet(sem_key, &semid) == -1) {
        goto done;
    }
    result = sendFile(data, input, semid);

done:
    if (fclose(input) == EOF) {
        perror("fclose input");
        result = -1;
    }
    if (cleanup(data, shmid, semid) == -1) {
        result = -1;
    }
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
