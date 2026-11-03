# Monte Carlo integration with pthreads and System V shared memory

Two programs estimate the integral

$$
I = \int_0^1 x\,dx = \frac{1}{2}
$$

using random points in the unit square. Program A distributes the points among
POSIX threads and stores the results in System V shared memory. Program B reads
the results, calculates the estimate, and removes the shared-memory segment.

## Run

Run both programs from the same working directory. Start B only after A finishes
successfully. For example, from the root of the OS repository:

```bash
./monte_carlo/build/monte_carlo_a 1000000 4 per_thread &&
    ./monte_carlo/build/monte_carlo_b
```

To use a shared counter:

```bash
./monte_carlo/build/monte_carlo_a 1000000 4 shared_mutex &&
    ./monte_carlo/build/monte_carlo_b
```

Program A accepts three arguments:

```text
monte_carlo_a <total_points> <threads> <per_thread|shared_mutex>
```

Both numbers must be positive decimal integers, and the thread count must not
exceed the number of points. Thread creation and memory allocation are also
subject to platform resource limits. Points are distributed as evenly as possible;
the first threads receive one extra point when the division has a remainder.

| Mode | Counting strategy |
| --- | --- |
| `per_thread` | Each thread counts hits locally and writes its final count to its own slot in shared memory. B sums the counts. |
| `shared_mutex` | Threads increment a shared counter, locking a mutex for each hit. B reads the total. |

The mutex synchronizes threads within A. B reads the completed results after A
exits; concurrent execution of A and B is not supported.

Each thread has its own random generator state. Seeds depend on the time, process
ID, and thread index, so repeated runs can produce different estimates.

## Output

A writes one comma-separated line to standard output:

```text
TIME,<mode>,<threads>,<total_points>,<elapsed_seconds>
```

B writes:

```text
RESULT,<mode>,<threads>,<total_points>,<hits>,<estimate>,<absolute_error>
```

A hit is a point satisfying `y <= x`. The estimate is `hits / total_points`, and
the absolute error is the distance from the exact value `0.5`.

Elapsed time is measured using `clock_gettime(CLOCK_MONOTONIC, ...)`, starting
immediately before thread creation and ending after all threads have been joined.
It includes thread creation, random sampling, counting, synchronization, and
waiting for the threads. Shared-memory setup, random-state initialization,
resource cleanup, and program B are outside the measured interval.

Errors are written to standard error. Both programs return `0` on success and
`1` on a handled error.

## Shared-memory lifecycle

The programs derive an IPC key from `monte_carlo.key` in the current working
directory. The file is created automatically and remains after the programs exit.
Keep it in place until the result has been read or the segment has been removed.
Deleting and recreating the file can change the key and prevent the programs from
finding the original segment.

Only one pending result is supported per IPC key. A refuses to overwrite an
existing segment. B removes the segment after successfully collecting the result,
so the result can be consumed once. If B finds incomplete or invalid data, it
reports an error and preserves the segment.

If a result is no longer needed or an interrupted run leaves a segment behind,
run the following command from the original working directory, after A has stopped:

```bash
./monte_carlo/build/monte_carlo_b --cleanup
```

Cleanup succeeds even when no segment exists. It discards any pending result for
that key. The key file itself is retained for subsequent runs.
