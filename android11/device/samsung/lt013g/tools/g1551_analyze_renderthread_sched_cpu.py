#!/usr/bin/env python3
"""G1551: compare SM-T311 task scheduler CPU runtime with four-swipe wall time.

Read-only diagnostic: wall interval includes shell proc reads and input overhead.
No inference of GPU fence wait or direct CPU saturation is justified.
"""
import argparse
import csv
import hashlib
import json
import statistics
from pathlib import Path

HEADER = (
    "gesture", "wall_s_start", "wall_s_end",
    "rt_cpu_ms_start", "rt_cpu_ms_end",
    "rt_voluntary_start", "rt_voluntary_end",
    "rt_involuntary_start", "rt_involuntary_end",
    "ui_cpu_ms_start", "ui_cpu_ms_end"
)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def analyze(path):
    with path.open(newline="") as f:
        reader = csv.DictReader(f, delimiter="\t")
        if tuple(reader.fieldnames or ()) != HEADER:
            raise ValueError("G1551_WRONG_SCHED_TAB_COLUMNS")
        rows = list(reader)
    if len(rows) != 8:
        raise ValueError("G1551_EXPECT_EIGHT_PHYSICAL_GESTURES")
    columns = {
        "window_wall_ms": ("wall_s_start", "wall_s_end", 1000),
        "renderthread_cpu_ms": ("rt_cpu_ms_start", "rt_cpu_ms_end", 1),
        "ui_thread_cpu_ms": ("ui_cpu_ms_start", "ui_cpu_ms_end", 1),
        "rt_voluntary_switches": ("rt_voluntary_start", "rt_voluntary_end", 1),
        "rt_involuntary_switches": ("rt_involuntary_start", "rt_involuntary_end", 1)
    }
    values = {key: [] for key in columns}
    for idx, row in enumerate(rows):
        gesture = str(idx + 1) + "-" + ("open" if idx % 2 == 0 else "close")
        if row["gesture"] != gesture:
            raise ValueError("G1551_SWIPES_NOT_IN_EXPECTED_ORDER")
        for key, (start, end, scale) in columns.items():
            try:
                a, b = float(row[start]), float(row[end])
            except (TypeError, ValueError) as err:
                raise ValueError("G1551_NON_NUMERIC_SCHED_VALUE") from err
            val = (b - a) * scale
            if val < 0:
                raise ValueError("G1551_COUNTER_DECREASED")
            if key in ("renderthread_cpu_ms", "ui_thread_cpu_ms") and val > (float(row["wall_s_end"]) - float(row["wall_s_start"])) * 1000 * 1.01:
                raise ValueError("G1551_SINGLE_THREAD_MORE_CPU_THAN_WALL")
            values[key].append(val)
    totals = {key: sum(value) for key, value in values.items()}
    return {
        "schema": "sm-t311-g1551-physical-renderthread-sched-cpu-util-readonly-v1",
        "sha256": sha(path),
        "gesture_count": len(rows),
        "totals": {key: round(value, 3) for key, value in totals.items()},
        "per_gesture_median": {key: round(statistics.median(value), 3) for key, value in values.items()},
        "renderthread_cpu_share_of_measured_wall_pct": round(100 * totals["renderthread_cpu_ms"] / totals["window_wall_ms"], 3),
        "ui_cpu_share_of_measured_wall_pct": round(100 * totals["ui_thread_cpu_ms"] / totals["window_wall_ms"], 3),
        "observation": "RenderThread nice=-10; task CPU runtime and voluntary/involuntary context switches from /proc/pid/task/tid/sched",
        "critical_caveat": "Measured wall includes input command overhead, proc reads and idle. CPU share is NOT GPU blocking fraction. Context switches do not identify GPU fence, specific syscall, or CPU competition. No FPS gain is claimed."
    }

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = json.dumps(analyze(args.capture), sort_keys=True, indent=2) + "\n"
    if args.output:
        if args.output.exists() or args.output.is_symlink():
            parser.error("G1551_REFUSE_OVERWRITE_PRIOR_CAPTURE_REPORT")
        args.output.write_text(report)
    print(report)

if __name__ == "__main__":
    main()
