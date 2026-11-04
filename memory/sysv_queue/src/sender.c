#include "ipc_utils.h"
#include "msg_queue.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>

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

static int sendMessage(int msqid, const struct QueueMessage *message,
                       size_t size) {
    int result;
    do {
        /* msgsz excludes the leading long message type. */
        result = msgsnd(msqid, message, size, 0);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
        perror("msgsnd");
    }
    return result;
}

static int sendFile(int msqid, FILE *input, size_t buffer_size) {
    size_t limit;
    if (getMaximumMessageSize(msqid, &limit) == -1) {
        return -1;
    }
    size_t chunk_size = buffer_size < limit ? buffer_size : limit;
    struct QueueMessage *message = malloc(sizeof(*message) + chunk_size);
    if (message == NULL) {
        perror("malloc message");
        return -1;
    }
    message->type = QUEUE_DATA_TYPE;
    int result = 0;
    while (true) {
        size_t size = fread(message->data, 1, chunk_size, input);
        if (ferror(input)) {
            perror("fread");
            result = -1;
            break;
        }
        if (size > 0 && sendMessage(msqid, message, size) == -1) {
            result = -1;
            break;
        }
        if (feof(input)) {
            break;
        }
        if (size == 0) {
            fprintf(stderr, "fread returned no data without EOF\n");
            result = -1;
            break;
        }
    }
    free(message);
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_file> <buffer_size>\n", argv[0]);
        return EXIT_FAILURE;
    }
    size_t buffer_size;
    if (parseBufferSize(argv[2], &buffer_size) == -1) {
        return EXIT_FAILURE;
    }
    FILE *input = fopen(argv[1], "rb");
    if (input == NULL) {
        perror("fopen input");
        return EXIT_FAILURE;
    }

    int result = -1;
    int msqid = -1;
    key_t key = getIpcKey(QUEUE_KEY_FILE, QUEUE_PROJECT_ID);
    if (key == (key_t)-1) {
        goto done;
    }
    msqid = msgget(key, IPC_CREAT | IPC_EXCL | 0600);
    if (msqid == -1) {
        perror("msgget");
        goto done;
    }
    result = sendFile(msqid, input, buffer_size);

done:
    if (fclose(input) == EOF) {
        perror("fclose input");
        result = -1;
    }
    if (result == 0) {
        /* Publish completion only after the input has closed successfully. */
        struct QueueMessage end = {.type = QUEUE_END_TYPE};
        result = sendMessage(msqid, &end, 0);
    }
    /* On success the receiver owns removal, after consuming all messages. */
    if (result == -1 && removeMessageQueue(msqid) == -1) {
        return EXIT_FAILURE;
    }
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
