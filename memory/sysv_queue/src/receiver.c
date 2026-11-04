#include "ipc_utils.h"
#include "msg_queue.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>

static int receiveFile(int msqid, FILE *output, size_t limit) {
    struct QueueMessage *message = malloc(sizeof(*message) + limit);
    if (message == NULL) {
        perror("malloc message");
        return -1;
    }
    int result = 0;
    while (true) {
        ssize_t size;
        do {
            /* Type zero consumes the first message, preserving queue order. */
            size = msgrcv(msqid, message, limit, 0, 0);
        } while (size == -1 && errno == EINTR);
        if (size == -1) {
            perror("msgrcv");
            result = -1;
            break;
        }
        if (message->type == QUEUE_END_TYPE && size == 0) {
            break;
        }
        if (message->type != QUEUE_DATA_TYPE || size == 0) {
            fprintf(stderr, "Invalid queue message type or size\n");
            result = -1;
            break;
        }
        if (fwrite(message->data, 1, (size_t)size, output) != (size_t)size) {
            perror("fwrite output");
            result = -1;
            break;
        }
    }
    free(message);
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <output_file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    key_t key = getIpcKey(QUEUE_KEY_FILE, QUEUE_PROJECT_ID);
    if (key == (key_t)-1) {
        return EXIT_FAILURE;
    }
    int msqid = msgget(key, 0);
    if (msqid == -1) {
        perror("msgget");
        return EXIT_FAILURE;
    }

    int result = -1;
    size_t limit;
    if (getMaximumMessageSize(msqid, &limit) == -1) {
        goto done;
    }
    FILE *output = fopen(argv[1], "wb");
    if (output == NULL) {
        perror("fopen output");
        goto done;
    }
    result = receiveFile(msqid, output, limit);
    if (fclose(output) == EOF) {
        perror("fclose output");
        result = -1;
    }

done:
    if (removeMessageQueue(msqid) == -1) {
        result = -1;
    }
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
