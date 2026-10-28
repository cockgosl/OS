#ifndef DUP_H
#define DUP_H

#include <stdbool.h>
#include <sys/types.h>

#define BUF_SZ 4096

typedef struct Channel Channel;
typedef struct ChannelOps ChannelOps;

struct ChannelOps {
    int (*send)(Channel *self, void *buffer, size_t size);
    int (*receive)(Channel *self, void *buffer, size_t size);
    int (*close_unused)(Channel *self);
    int (*close_end)(Channel *self);
};

struct Channel {
    int parent_to_child[2];
    int child_to_parent[2];

    bool is_parent;
    ChannelOps ops;
};

int emulation();
int chann_send(Channel *self, void *buffer, size_t size);
int chann_receive(Channel *self, void *buffer, size_t size);
int close_unused(Channel *self);
int close_end(Channel *self);
int channel_init(Channel *channel);

#endif
