#include "fifo.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int receiveFile(int fifo_fd, FILE *output, size_t buffer_size) {
    char *buffer = malloc(buffer_size);
    if (buffer == NULL) {
        perror("malloc output buffer");
        return -1;
    }

    int result = 0;

    while (true) {
        ssize_t bytes_read;

        do {
            bytes_read = read(fifo_fd, buffer, buffer_size);
        } while (bytes_read == -1 && errno == EINTR);

        if (bytes_read == -1) {
            perror("read FIFO");
            result = -1;
            break;
        }

        if (bytes_read == 0) {
            break;
        }

        size_t bytes_to_write = (size_t)bytes_read;

        if (fwrite(buffer, 1, bytes_to_write, output) != bytes_to_write) {
            perror("fwrite output");
            result = -1;
            break;
        }
    }

    free(buffer);
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <output_file> <buffer_size>\n", argv[0]);
        return 1;
    }

    size_t buffer_size;
    if (parseBufferSize(argv[2], &buffer_size) == -1) {
        return 1;
    }

    FILE *output = fopen(argv[1], "wb");
    if (output == NULL) {
        perror("fopen output");
        return 1;
    }

    int result = -1;
    int fifo_fd = -1;
    if (prepareFifo(FIFO_PATH) == -1) {
        goto cleanup;
    }

    fifo_fd = openFifoForReading(FIFO_PATH);
    if (fifo_fd == -1) {
        goto cleanup;
    }

    result = receiveFile(fifo_fd, output, buffer_size);

cleanup:
    if (fifo_fd >= 0 && close(fifo_fd) == -1) {
        perror("close FIFO");
        result = -1;
    }

    if (fclose(output) == EOF) {
        perror("fclose output");
        result = -1;
    }

    if (fifo_fd >= 0 && removeFifo(FIFO_PATH) == -1) {
        result = -1;
    }

    return result == 0 ? 0 : 1;
}
