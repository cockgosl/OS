#!/usr/bin/env python3
"""Run Monte Carlo experiments and plot their results."""

import argparse
import csv
import math
import os
from pathlib import Path
import random
import statistics
import subprocess
import sys
import tempfile
from collections import defaultdict

MODES = ("per_thread", "shared_mutex")
COLUMNS = (
    "mode", "threads", "points", "run", "elapsed_seconds",
    "hits", "estimate", "absolute_error",
)


def positive_int(text):
    value = int(text)
    if value <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return value


def positive_seconds(text):
    value = float(text)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError("must be a finite positive number")
    return value


def execute(program, arguments, directory, timeout):
    return subprocess.run(
        [str(program), *map(str, arguments)],
        cwd=directory,
        capture_output=True,
        text=True,
        check=True,
        timeout=timeout,
    ).stdout


def parse_record(output, expected, size):
    records = list(csv.reader(output.splitlines()))
    if len(records) != 1 or len(records[0]) != size:
        raise ValueError(f"Unexpected program output: {output!r}")
    record = records[0]
    if record[:4] != expected:
        raise ValueError(f"Unexpected program output: {output!r}")
    return record[4:]


def run_experiments(args):
    threads = sorted(set(args.threads))
    if args.points > 2**64 - 1 or threads[-1] > args.points:
        raise ValueError("Points must fit uint64_t; threads must not exceed points")
    program_a = args.build_dir.resolve() / "monte_carlo_a"
    program_b = args.build_dir.resolve() / "monte_carlo_b"
    for program in (program_a, program_b):
        if not program.is_file() or not os.access(program, os.X_OK):
            raise ValueError(f"Executable not found: {program}; build the project first")

    scenarios = [(mode, count) for mode in MODES for count in threads]
    order = random.Random(args.seed)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation protects results from an accidental repeat invocation.
    with args.output.open("x", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(COLUMNS)
        with tempfile.TemporaryDirectory(prefix="monte-carlo-") as directory:
            try:
                for repeat in range(1, args.repeats + 1):
                    order.shuffle(scenarios)
                    for mode, count in scenarios:
                        print(f"Round {repeat}/{args.repeats}: {mode}, {count} threads",
                              flush=True)
                        metadata = [mode, str(count), str(args.points)]
                        timing = execute(program_a, [args.points, count, mode],
                                         directory, args.timeout)
                        result = execute(program_b, [], directory, args.timeout)
                        elapsed, = parse_record(timing, ["TIME", *metadata], 5)
                        hits, estimate, error = parse_record(
                            result, ["RESULT", *metadata], 7)
                        values = [float(elapsed), float(estimate), float(error)]
                        if (not all(math.isfinite(value) for value in values)
                                or values[0] <= 0
                                or not 0 <= int(hits) <= args.points
                                or not 0 <= values[1] <= 1 or values[2] < 0):
                            raise ValueError("Invalid measurement returned by A or B")
                        writer.writerow([mode, count, args.points, repeat,
                                         elapsed, hits, estimate, error])
                        file.flush()
            finally:
                # Keep the key file until the last segment has been removed.
                execute(program_b, ["--cleanup"], directory, max(args.timeout, 5))
    print(f"Saved {args.output}")


def plot_results(args):
    # Measurement collection only requires the Python standard library.
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    samples = defaultdict(list)
    point_counts = set()
    with args.csv.open(newline="", encoding="utf-8") as file:
        reader = csv.DictReader(file)
        required = {"mode", "threads", "points", "elapsed_seconds"}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError("CSV must contain mode, threads, points and elapsed_seconds")
        for row in reader:
            mode = row["mode"]
            count = int(row["threads"])
            points = int(row["points"])
            elapsed = float(row["elapsed_seconds"])
            if (mode not in MODES or count < 1 or points < count
                    or not math.isfinite(elapsed) or elapsed <= 0):
                raise ValueError(f"Invalid measurement on CSV line {reader.line_num}")
            samples[mode, count].append(elapsed)
            point_counts.add(points)
    if not samples or len(point_counts) != 1:
        raise ValueError("CSV must contain measurements with one fixed point count")

    medians = {key: statistics.median(values) for key, values in samples.items()}
    present_modes = [mode for mode in MODES if any(key[0] == mode for key in samples)]
    for mode in present_modes:
        if (mode, 1) not in medians:
            raise ValueError(f"Speedup requires a one-thread measurement for {mode}")

    figure, axes = plt.subplots(1, 2, figsize=(12, 4.5))
    try:
        for mode in present_modes:
            counts = sorted(count for name, count in medians if name == mode)
            times = [medians[mode, count] for count in counts]
            axes[0].plot(counts, times, marker="o", label=mode)
            axes[1].plot(counts, [medians[mode, 1] / value for value in times],
                         marker="o", label=mode)
        counts = sorted({count for _, count in samples})
        for axis in axes:
            axis.set_xscale("log", base=2)
            axis.set_xticks(counts, labels=[str(count) for count in counts])
            axis.set_xlabel("Threads")
            axis.grid(True, which="both", alpha=0.3)
            axis.legend()
        axes[0].set_ylabel("Median elapsed time (seconds)")
        axes[1].set_ylabel("Speedup: T(1) / T(n)")
        figure.suptitle(f"Monte Carlo: {next(iter(point_counts)):,} points")
        figure.tight_layout()
        output = args.csv.with_suffix(".png")
        figure.savefig(output, dpi=160)
    finally:
        plt.close(figure)
    print(f"Saved {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("run", help="collect measurements in CSV format")
    run.add_argument("--build-dir", type=Path, default=Path("build"))
    run.add_argument("--points", type=positive_int, default=2_000_000)
    run.add_argument("--threads", type=positive_int, nargs="+", default=[1, 2, 4, 8])
    run.add_argument("--repeats", type=positive_int, default=3)
    run.add_argument("--timeout", type=positive_seconds, default=60,
                     help="timeout per process in seconds (default: 60)")
    run.add_argument("--seed", type=int, default=0,
                     help="seed for scenario order, not for point generation")
    run.add_argument("--output", type=Path, default=Path("results/results.csv"))
    run.set_defaults(action=run_experiments)
    plot = commands.add_parser("plot", help="plot median times and speedup")
    plot.add_argument("csv", type=Path)
    plot.set_defaults(action=plot_results)
    args = parser.parse_args()
    try:
        args.action(args)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        detail = getattr(error, "stderr", None)
        if isinstance(detail, bytes):
            detail = detail.decode(errors="replace")
        parser.exit(1, f"Error: {error}\n" + (detail or ""))
    except KeyboardInterrupt:
        parser.exit(130, "Interrupted. Completed CSV rows have been retained.\n")


if __name__ == "__main__":
    main()
