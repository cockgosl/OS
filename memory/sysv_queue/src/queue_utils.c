#include "msg_queue.h"

#include <stdint.h>
#include <stdio.h>
#include <sys/msg.h>

int getMaximumMessageSize(int msqid, size_t *size) {
    FILE *file = fopen("/proc/sys/kernel/msgmax", "r");
    if (file == NULL) {
        perror("fopen msgmax");
        return -1;
    }
    size_t kernel_limit = 0;
    int result = 0;
    if (fscanf(file, "%zu", &kernel_limit) != 1) {
        fprintf(stderr, "Cannot read kernel.msgmax\n");
        result = -1;
    }
    if (fclose(file) == EOF) {
        perror("fclose msgmax");
        result = -1;
    }
    if (result == -1) {
        return -1;
    }

    struct msqid_ds info;
    if (msgctl(msqid, IPC_STAT, &info) == -1) {
        perror("msgctl IPC_STAT");
        return -1;
    }
    size_t limit = kernel_limit;
    if (info.msg_qbytes < limit) {
        limit = (size_t)info.msg_qbytes;
    }
    if (limit == 0 || limit > SIZE_MAX - sizeof(struct QueueMessage)) {
        fprintf(stderr, "Invalid System V message limit\n");
        return -1;
    }
    *size = limit;
    return 0;
}
