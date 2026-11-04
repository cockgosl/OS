#include "fifo.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int parseBufferSize(const char *text, size_t *buffer_size) {
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

int prepareFifo(const char *path) {
    if (mkfifo(path, 0600) == 0) {
        return 0;
    }

    if (errno != EEXIST) {
        perror("mkfifo");
        return -1;
    }

    struct stat fifo_info;
    if (lstat(path, &fifo_info) == -1) {
        perror("lstat FIFO");
        return -1;
    }

    if (!S_ISFIFO(fifo_info.st_mode)) {
        fprintf(stderr, "Path exists but is not a FIFO: %s\n", path);
        return -1;
    }

    return 0;
}

static int openFifo(const char *path, int flags) {
    int fd;

    do {
        fd = open(path, flags);
    } while (fd == -1 && errno == EINTR);

    if (fd == -1) {
        perror("open FIFO");
    }

    return fd;
}

int openFifoForReading(const char *path) { return openFifo(path, O_RDONLY); }

int openFifoForWriting(const char *path) { return openFifo(path, O_WRONLY); }

int readAll(int fd, void *buffer, size_t size) {
    char *data = buffer;
    size_t total_read = 0;

    while (total_read < size) {
        ssize_t bytes_read = read(fd, data + total_read, size - total_read);
        if (bytes_read == -1 && errno == EINTR) {
            continue;
        }
        if (bytes_read == -1) {
            perror("read FIFO");
            return -1;
        }
        if (bytes_read == 0) {
            fprintf(stderr, "Unexpected EOF in FIFO transfer\n");
            return -1;
        }
        total_read += (size_t)bytes_read;
    }
    return 0;
}

int readBlockSize(int fd, uint64_t *size) {
    unsigned char header[8];
    if (readAll(fd, header, sizeof(header)) == -1) {
        return -1;
    }

    uint64_t value = 0;
    for (size_t i = 0; i < sizeof(header); ++i) {
        value = (value << 8) | header[i];
    }
    *size = value;
    return 0;
}

int writeBlockSize(int fd, uint64_t size) {
    unsigned char header[8];
    for (size_t i = sizeof(header); i > 0; --i) {
        header[i - 1] = (unsigned char)(size & UINT64_C(0xff));
        size >>= 8;
    }
    return writeAll(fd, header, sizeof(header));
}

int writeAll(int fd, const void *buffer, size_t size) {
    const char *data = buffer;
    size_t total_written = 0;

    while (total_written < size) {
        ssize_t bytes_written =
            write(fd, data + total_written, size - total_written);

        if (bytes_written == -1 && errno == EINTR) {
            continue;
        }

        if (bytes_written <= 0) {
            if (bytes_written == 0) {
                errno = EIO;
            }

            perror("write FIFO");
            return -1;
        }

        total_written += (size_t)bytes_written;
    }

    return 0;
}

int removeFifo(const char *path) {
    if (unlink(path) == -1 && errno != ENOENT) {
        perror("unlink FIFO");
        return -1;
    }

    return 0;
}
