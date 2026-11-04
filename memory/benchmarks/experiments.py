#!/usr/bin/env python3
"""Compare complete file transfers through FIFO and System V IPC."""

import argparse
from collections import defaultdict
from contextlib import ExitStack
import csv
import ctypes
import errno
import hashlib
import math
import os
from pathlib import Path
import random
import selectors
import statistics
import subprocess
import tempfile
import time

PROGRAMS = {
    "fifo": ("fifo/fifo_sender", "fifo/fifo_receiver"),
    "shm": ("sysv_shm/shm_sender", "sysv_shm/shm_receiver"),
    "queue": ("sysv_queue/queue_sender", "sysv_queue/queue_receiver"),
}
COLUMNS = (
    "mechanism", "buffer_size", "file_size_bytes", "run", "elapsed_seconds",
    "throughput_mib_s", "msgmax_bytes", "msgmnb_bytes",
    "input_sha256", "output_sha256",
)
MIB = 1024 * 1024


def positive_int(text):
    value = int(text)
    if value < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return value


def nonnegative_int(text):
    value = int(text)
    if value < 0:
        raise argparse.ArgumentTypeError("must be a nonnegative integer")
    return value


def positive_seconds(text):
    value = float(text)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError("must be finite and positive")
    return value


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(MIB), b""):
            value.update(block)
    return value.hexdigest()


class IpcResources:
    """Track IPC objects for a unique working directory, without ABI structs."""

    def __init__(self, directory, mechanism):
        self.api = ctypes.CDLL(None, use_errno=True)
        signatures = {
            "ftok": [ctypes.c_char_p, ctypes.c_int],
            "shmget": [ctypes.c_int, ctypes.c_size_t, ctypes.c_int],
            "semget": [ctypes.c_int, ctypes.c_int, ctypes.c_int],
            "msgget": [ctypes.c_int, ctypes.c_int],
            "shmctl": [ctypes.c_int, ctypes.c_int, ctypes.c_void_p],
            "msgctl": [ctypes.c_int, ctypes.c_int, ctypes.c_void_p],
        }
        for name, arguments in signatures.items():
            function = getattr(self.api, name)
            function.argtypes = arguments
            function.restype = ctypes.c_int
        self.api.semctl.restype = ctypes.c_int
        projects = {"shm": {"shm": "M", "sem": "S"},
                    "queue": {"msg": "Q"}, "fifo": {}}[mechanism]
        keyfile = directory / "ipc.key"
        keyfile.touch()
        self.keys = {}
        self.ids = {}
        for kind, project in projects.items():
            key = self.api.ftok(os.fsencode(keyfile), ord(project))
            if key == -1:
                raise OSError(ctypes.get_errno(), "ftok failed")
            self.keys[kind] = key
            if self.find(kind) is not None:
                raise ValueError("IPC key collision; retry with a new working directory")

    def find(self, kind):
        key = self.keys[kind]
        calls = {"shm": (self.api.shmget, (key, 1, 0o600)),
                 "sem": (self.api.semget, (key, 0, 0o600)),
                 "msg": (self.api.msgget, (key, 0o600))}
        function, arguments = calls[kind]
        result = function(*arguments)
        if result == -1:
            error = ctypes.get_errno()
            if error == errno.ENOENT:
                return None
            raise OSError(error, f"{kind} lookup failed")
        return result

    def ready(self):
        ready = True
        for kind in self.keys:
            identifier = self.find(kind)
            if identifier is None:
                ready = False
            else:
                self.ids[kind] = identifier
        return ready

    def cleanup(self):
        errors = []
        for kind in self.keys:
            identifier = self.ids.get(kind)
            if identifier is None:
                identifier = self.find(kind)
            if identifier is None:
                continue
            # IPC_RMID is zero on Linux. semctl has a variadic fourth argument.
            if kind == "sem":
                result = self.api.semctl(identifier, 0, 0)
            else:
                result = getattr(self.api, kind + "ctl")(identifier, 0, None)
            if result == -1 and ctypes.get_errno() not in (errno.EIDRM, errno.EINVAL):
                errors.append(f"{kind}: {os.strerror(ctypes.get_errno())}")
        if errors:
            raise RuntimeError("IPC cleanup failed: " + "; ".join(errors))


def remaining(deadline):
    seconds = deadline - time.perf_counter()
    if seconds <= 0:
        raise TimeoutError("Transfer exceeded its timeout")
    return seconds


def wait_for_processes(processes, deadline):
    # pidfds avoid the polling delays introduced by Popen.wait(timeout=...).
    with ExitStack() as stack:
        selector = stack.enter_context(selectors.DefaultSelector())
        for process in processes:
            if process.returncode is not None:
                if process.returncode != 0:
                    raise RuntimeError(f"{process.args[0]} exited with {process.returncode}")
                continue
            descriptor = os.pidfd_open(process.pid)
            stack.callback(os.close, descriptor)
            selector.register(descriptor, selectors.EVENT_READ, process)
        while selector.get_map():
            events = selector.select(remaining(deadline))
            if not events:
                raise TimeoutError("Transfer exceeded its timeout")
            for key, _ in events:
                process = key.data
                code = process.wait()
                selector.unregister(key.fd)
                if code != 0:
                    raise RuntimeError(f"{process.args[0]} exited with {code}")


def transfer(mechanism, buffer_size, programs, input_path, expected_hash, timeout):
    with tempfile.TemporaryDirectory(prefix="ipc-transfer-") as temporary:
        directory = Path(temporary)
        resources = IpcResources(directory, mechanism)
        output = directory / "output.bin"
        processes = []
        with ExitStack() as logs:
            sender_log = logs.enter_context((directory / "sender.log").open("wb"))
            receiver_log = logs.enter_context((directory / "receiver.log").open("wb"))
            try:
                start = time.perf_counter()
                deadline = start + timeout
                sender = subprocess.Popen(
                    [str(programs[0]), str(input_path), str(buffer_size)],
                    cwd=directory, stdout=sender_log, stderr=subprocess.STDOUT)
                processes.append(sender)
                while True:
                    ready = ((directory / "transfer.fifo").is_fifo()
                             if mechanism == "fifo" else resources.ready())
                    if ready:
                        break
                    if sender.poll() is not None:
                        raise RuntimeError("Sender exited before IPC became ready")
                    time.sleep(min(0.001, remaining(deadline)))
                remaining(deadline)
                arguments = [str(programs[1]), str(output)]
                if mechanism == "fifo":
                    arguments.append(str(buffer_size))
                processes.append(subprocess.Popen(
                    arguments, cwd=directory, stdout=receiver_log,
                    stderr=subprocess.STDOUT))
                wait_for_processes(processes, deadline)
                elapsed = time.perf_counter() - start
                # Verification and runner cleanup are outside the timed interval.
                actual_hash = digest(output)
                if actual_hash != expected_hash:
                    raise ValueError("Input and output SHA-256 hashes differ")
                return elapsed, actual_hash
            except (OSError, RuntimeError, ValueError) as error:
                details = []
                for name in ("sender", "receiver"):
                    path = directory / f"{name}.log"
                    content = path.read_text(errors="replace").strip()
                    if content:
                        details.append(f"{name}: {content}")
                raise RuntimeError(str(error) + "\n" + "\n".join(details)) from error
            finally:
                # Stop both peers before removing any IPC object or key file.
                for process in processes:
                    if process.poll() is None:
                        process.kill()
                for process in processes:
                    process.wait()
                resources.cleanup()


def run_experiments(args):
    if not hasattr(os, "pidfd_open"):
        raise ValueError("Experiments require Python 3.9+ and Linux pidfd support")
    programs = {}
    for mechanism in args.mechanisms:
        programs[mechanism] = tuple(
            (args.build_dir / path).resolve() for path in PROGRAMS[mechanism])
        for program in programs[mechanism]:
            if not program.is_file() or not os.access(program, os.X_OK):
                raise ValueError(f"Executable not found: {program}")
    limits = [int(Path(f"/proc/sys/kernel/{name}").read_text())
              for name in ("msgmax", "msgmnb")]
    scenarios = [(mechanism, size) for mechanism in dict.fromkeys(args.mechanisms)
                 for size in sorted(set(args.buffers))]
    order = random.Random(args.seed)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(COLUMNS)
        file.flush()
        with tempfile.TemporaryDirectory(prefix="ipc-input-") as temporary:
            input_path = Path(temporary) / "input.bin"
            with input_path.open("wb") as source:
                left = args.file_size
                while left:
                    size = min(left, MIB)
                    source.write(os.urandom(size))
                    left -= size
            expected_hash = digest(input_path)
            for round_number in range(1 - args.warmups, args.repeats + 1):
                order.shuffle(scenarios)
                for mechanism, size in scenarios:
                    label = "warmup" if round_number <= 0 else f"run {round_number}"
                    print(f"{label}: {mechanism}, buffer={size}", flush=True)
                    elapsed, actual_hash = transfer(
                        mechanism, size, programs[mechanism], input_path,
                        expected_hash, args.timeout)
                    if round_number > 0:
                        writer.writerow([
                            mechanism, size, args.file_size, round_number,
                            elapsed, args.file_size / MIB / elapsed, *limits,
                            expected_hash, actual_hash])
                        file.flush()
    print(f"Saved {args.output}")


def plot_results(args):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    samples = defaultdict(list)
    sizes = set()
    with args.csv.open(newline="", encoding="utf-8") as file:
        reader = csv.DictReader(file)
        required = {"mechanism", "buffer_size", "file_size_bytes", "elapsed_seconds"}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError("CSV is missing required measurement columns")
        for row in reader:
            mechanism = row["mechanism"]
            buffer_size = int(row["buffer_size"])
            size = int(row["file_size_bytes"])
            elapsed = float(row["elapsed_seconds"])
            if (mechanism not in PROGRAMS or buffer_size <= 0 or size <= 0
                    or not math.isfinite(elapsed) or elapsed <= 0):
                raise ValueError(f"Invalid measurement on CSV line {reader.line_num}")
            samples[mechanism, buffer_size].append(elapsed)
            sizes.add(size)
    if not samples or len(sizes) != 1:
        raise ValueError("Plot one nonempty series with a fixed file size")
    output = args.csv.with_suffix(".png")
    if output.resolve() == args.csv.resolve():
        raise ValueError("The CSV path must differ from the PNG output path")
    size = next(iter(sizes))
    figure, axes = plt.subplots(1, 2, figsize=(12, 4.5))
    try:
        for mechanism in PROGRAMS:
            buffers = sorted(buffer for name, buffer in samples if name == mechanism)
            if not buffers:
                continue
            medians = [statistics.median(samples[mechanism, value]) for value in buffers]
            line, = axes[0].plot(buffers, medians, marker="o", label=mechanism)
            axes[1].plot(buffers, [size / MIB / value for value in medians],
                         marker="o", label=mechanism)
            for buffer in buffers:
                values = samples[mechanism, buffer]
                axes[0].scatter([buffer] * len(values), values, alpha=0.3,
                                color=line.get_color())
        buffers = sorted({buffer for _, buffer in samples})
        for axis in axes:
            axis.set_xscale("log", base=2)
            axis.set_xticks(buffers, labels=[str(value) for value in buffers])
            axis.set_xlabel("Application buffer size (bytes)")
            axis.grid(True, which="both", alpha=0.3)
            axis.legend()
        axes[0].set_ylabel("Median elapsed time (seconds)")
        axes[1].set_ylabel("Throughput from median time (MiB/s)")
        figure.suptitle(f"Complete file transfer: {size / MIB:g} MiB")
        figure.tight_layout()
        figure.savefig(output, dpi=160)
    finally:
        plt.close(figure)
    print(f"Saved {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("run", help="measure complete file transfers")
    run.add_argument("--build-dir", type=Path, default=Path("build"))
    run.add_argument("--file-size", type=positive_int, default=64 * MIB)
    run.add_argument("--buffers", type=positive_int, nargs="+",
                     default=[4096, 65536, 1048576])
    run.add_argument("--mechanisms", nargs="+", choices=PROGRAMS, default=list(PROGRAMS))
    run.add_argument("--repeats", type=positive_int, default=5)
    run.add_argument("--warmups", type=nonnegative_int, default=1)
    run.add_argument("--timeout", type=positive_seconds, default=120,
                     help="overall timeout per transfer in seconds (default: 120)")
    run.add_argument("--seed", type=int, default=0)
    run.add_argument("--output", type=Path, default=Path("results/comparison.csv"))
    run.set_defaults(action=run_experiments)
    plot = commands.add_parser("plot", help="plot median time and throughput")
    plot.add_argument("csv", type=Path)
    plot.set_defaults(action=plot_results)
    args = parser.parse_args()
    try:
        args.action(args)
    except (OSError, RuntimeError, ValueError, ImportError) as error:
        parser.exit(1, f"Error: {error}\n")
    except KeyboardInterrupt:
        parser.exit(130, "Interrupted. Completed CSV rows have been retained.\n")


if __name__ == "__main__":
    main()
