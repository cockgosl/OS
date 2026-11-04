#ifndef MSG_QUEUE_H
#define MSG_QUEUE_H

#include <stddef.h>

#define QUEUE_KEY_FILE "ipc.key"
#define QUEUE_PROJECT_ID 'Q'
#define QUEUE_DATA_TYPE 1L
#define QUEUE_END_TYPE 2L

/* Message boundaries supply the payload length; END has no payload. */
struct QueueMessage {
    long type;
    char data[];
};

int getMaximumMessageSize(int msqid, size_t *size);

#endif
