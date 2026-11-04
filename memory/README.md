# File transfer with Linux IPC

Three pairs of C11 programs transfer a file between processes using a FIFO,
System V shared memory, or a System V message queue.

## Build

## Manual execution

Run each sender and receiver from the same working directory. Use different paths
for the input and output files: opening the output truncates an existing file.
All buffer sizes are positive decimal integers in bytes.

| Mechanism | Sender | Receiver |
| --- | --- | --- |
| FIFO | `build/fifo/fifo_sender input.bin 65536` | `build/fifo/fifo_receiver output.bin 65536` |
| Shared memory | `build/sysv_shm/shm_sender input.bin 65536` | `build/sysv_shm/shm_receiver output.bin` |
| Message queue | `build/sysv_queue/queue_sender input.bin 65536` | `build/sysv_queue/queue_receiver output.bin` |

The paths in this table are relative to `memory/`. FIFO supports either startup
order; its sender and receiver block until both ends are open. Their buffer sizes
may differ. For System V IPC, start the sender first and launch the receiver after
the IPC objects exist. The experiment runner handles this readiness step.

Run only one transfer per mechanism in a working directory. Programs return `0`
on success and `1` on a handled error, with diagnostics on standard error. Check
both processes and compare files to verify a complete transfer.

### Protocols

- **FIFO:** each data block has an 8-byte big-endian length. A zero-length block
  marks successful completion. EOF before this marker is an error.
- **Shared memory:** a single shared buffer has two semaphores, `EMPTY` and `FULL`,
  which transfer ownership between sender and receiver. The receiver closes the
  output before acknowledging the final block, including buffered-write errors.
- **Message queue:** type `1` carries data; an empty type `2` message marks
  completion. The sender reads directly into a message buffer, capped by both
  `kernel.msgmax` and the queue's `msg_qbytes`. Successfully queuing all messages
  does not confirm that the receiver has written the file.

### Resource lifecycle

FIFO uses `transfer.fifo`, which the receiver removes after the transfer. System V
IPC keys derive from `ipc.key` using project IDs `M` (shared memory), `S`
(semaphores), and `Q` (message queue). Keep the key file until cleanup completes.
The shared-memory sender removes its segment and semaphores. The queue receiver
removes the queue; a failed queue sender also attempts removal.

A peer that is killed can leave the other process waiting and can leave System V
objects behind. Use `ipcs` to inspect such resources and `ipcrm` to remove the
specific objects after stopping both peers. Removing `ipc.key` does not remove
kernel IPC objects. The runner handles timeouts and cleanup for its own transfers;
forced termination of the runner can prevent cleanup.

## Experiments

The runner requires Python 3.9 or later and Linux 5.3 or later with `pidfd_open`
support. It uses the Python standard library; plotting additionally requires
`matplotlib` 3.5 or later. Process descriptors allow waiting for exit without
adding the polling delays of timed `Popen.wait` calls.

From `memory/`, after building:

```bash
python3 benchmarks/experiments.py run \
    --build-dir build \
    --file-size 67108864 \
    --buffers 4096 65536 1048576 \
    --repeats 5 \
    --output results/comparison.csv

python3 benchmarks/experiments.py plot results/comparison.csv
```

The PNG contains median elapsed time, individual elapsed-time samples, and
throughput calculated from each median. Plotting reuses the CSV and replaces the
PNG. Plot one fixed file size at a time; generate separate CSV files for different
file sizes. Existing CSV files are never overwritten.

Optional arguments include `--mechanisms fifo shm queue`, `--warmups 1`,
`--timeout 120`, and `--seed 0`. The timeout covers the entire transfer, including
startup. Each measurement gets a temporary working directory. The input is
created once and reused for all scenarios. Each round shuffles mechanism/buffer
pairs; warmup rounds are not recorded. After stopping both peers, the runner
removes remaining IPC objects before deleting their key files. On an error, it
stops and retains completed CSV rows.

### Measurement boundaries

The timer starts immediately before launching the sender and stops after both
processes exit successfully. It includes process startup, IPC setup, readiness
polling, file reads and writes, synchronization, and cleanup performed by the C
programs. Readiness is checked in Python at intervals of up to 1 ms, without
invoking `ipcs` or `ps`. Input generation, SHA-256 verification, CSV writing, and
the runner's final cleanup are outside the timed interval.

This measures complete transfers, including runner startup coordination, rather
than isolated IPC operations. Small files can be dominated by startup costs.
The initial input checksum and warmups populate the filesystem cache. Writes use
buffered I/O without `fsync`, so completion does not guarantee that data has reached
physical storage. Results depend on the machine, cache state, scheduling, and
available IPC buffering; they do not establish a universal ranking of mechanisms.

CSV columns:

```text
mechanism,buffer_size,file_size_bytes,run,elapsed_seconds,throughput_mib_s,msgmax_bytes,msgmnb_bytes,input_sha256,output_sha256
```

`buffer_size` is the requested application buffer, not the size returned by a
FIFO `read` and not the FIFO's kernel capacity. For queues the actual data message
is capped by the current message/queue limits. `msgmax_bytes` and `msgmnb_bytes`
record global queue settings at the start of the experiment; they do not describe
FIFO or shared-memory capacity. The runner does not change system limits or
enlarge FIFO/queue capacities. Throughput is file size in MiB divided by elapsed
time in seconds. Both SHA-256 fields must match for a measurement to be saved.
