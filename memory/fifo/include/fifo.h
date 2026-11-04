#ifndef FIFO_H
#define FIFO_H

#include <stddef.h>
#include <stdint.h>

#define FIFO_PATH "transfer.fifo"

int parseBufferSize(const char *text, size_t *buffer_size);
int prepareFifo(const char *path);
int openFifoForReading(const char *path);
int openFifoForWriting(const char *path);
int readAll(int fd, void *buffer, size_t size);
/* Blocks use an 8-byte big-endian length; zero marks successful completion. */
int readBlockSize(int fd, uint64_t *size);
int writeBlockSize(int fd, uint64_t size);
int writeAll(int fd, const void *buffer, size_t size);
int removeFifo(const char *path);

#endif
