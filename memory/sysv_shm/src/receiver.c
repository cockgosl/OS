#include "ipc_utils.h"
#include "sys_v.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/sem.h>
#include <sys/shm.h>

static struct SharedData *openSharedMemory(key_t key, size_t *segment_size) {
    int shmid = shmget(key, sizeof(struct SharedData), 0);
    if (shmid == -1) {
        perror("shmget");
        return NULL;
    }
    struct shmid_ds info;
    if (shmctl(shmid, IPC_STAT, &info) == -1) {
        perror("shmctl IPC_STAT");
        return NULL;
    }
    if (info.shm_segsz < sizeof(struct SharedData)) {
        fprintf(stderr, "Invalid shared memory size\n");
        return NULL;
    }
    void *memory = shmat(shmid, NULL, 0);
    if (memory == (void *)-1) {
        perror("shmat");
        return NULL;
    }
    *segment_size = info.shm_segsz;
    return memory;
}

static int readMessage(const struct SharedData *data, size_t segment_size,
                       FILE *output) {
    if (data->capacity == 0 || data->capacity > segment_size - sizeof(*data) ||
        data->size > data->capacity || (data->size == 0 && !data->finished)) {
        fprintf(stderr, "Invalid shared-memory message\n");
        return -1;
    }
    if (data->size > 0 &&
        fwrite(data->buff, 1, data->size, output) != data->size) {
        perror("fwrite output");
        return -1;
    }
    return 0;
}

static int receiveFile(struct SharedData *data, size_t segment_size,
                       const char *output_path, int semid) {
    /* Do not read the shared header until the sender publishes a block. */
    if (waitSemaphore(semid, SEM_FULL) == -1) {
        return -1;
    }
    FILE *output = fopen(output_path, "wb");
    int result = output != NULL ? 0 : -1;
    if (output == NULL) {
        perror("fopen output");
    }

    while (true) {
        bool finished = data->finished;
        if (result == 0) {
            result = readMessage(data, segment_size, output);
        }
        if ((finished || result == -1) && output != NULL) {
            /* Include buffered-write and close errors in the acknowledgement.
             */
            if (fclose(output) == EOF) {
                perror("fclose output");
                result = -1;
            }
            output = NULL;
        }
        data->failed = result == -1;
        if (postSemaphore(semid, SEM_EMPTY) == -1) {
            result = -1;
            break;
        }
        if (finished || result == -1) {
            break;
        }
        if (waitSemaphore(semid, SEM_FULL) == -1) {
            result = -1;
            break;
        }
    }

    if (output != NULL && fclose(output) == EOF) {
        perror("fclose output");
        result = -1;
    }
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <output_file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    key_t shm_key = getIpcKey(IPC_KEY_FILE, SHM_PROJECT_ID);
    key_t sem_key = getIpcKey(IPC_KEY_FILE, SEM_PROJECT_ID);
    if (shm_key == (key_t)-1 || sem_key == (key_t)-1) {
        return EXIT_FAILURE;
    }
    int semid = semget(sem_key, SEM_COUNT, 0);
    if (semid == -1) {
        perror("semget");
        return EXIT_FAILURE;
    }
    size_t segment_size;
    struct SharedData *data = openSharedMemory(shm_key, &segment_size);
    if (data == NULL) {
        return EXIT_FAILURE;
    }

    int result = receiveFile(data, segment_size, argv[1], semid);
    if (detachSharedMemory(data) == -1) {
        result = -1;
    }
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
