# Duplex Pipe

An implementation of a duplex inter-process communication channel between a `parent` and a `child` process using two unnamed pipes (`pipe`).

## Description

Two `pipe` instances are used to enable bidirectional data transmission:

- `parent_to_child` — transfers data from the parent process to the child process;
- `child_to_parent` — transfers data from the child process to the parent process.

The `Channel` structure encapsulates file descriptors and channel operations. Interaction with the channel is performed through a function pointer table, `ChannelOps`, allowing object-oriented programming (OOP) concepts to be applied in C.
