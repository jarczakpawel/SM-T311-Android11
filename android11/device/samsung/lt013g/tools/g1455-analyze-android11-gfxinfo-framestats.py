#!/usr/bin/env python3
import argparse
import csv
import hashlib
import json
import re
import statistics
from pathlib import Path

FIELDS = (
    ("vsync_to_input", "Vsync", "HandleInputStart"),
    ("input_to_draw", "HandleInputStart", "DrawStart"),
    ("draw_to_sync_queue", "DrawStart", "SyncQueued"),
    ("renderthread_queue_wait", "SyncQueued", "SyncStart"),
    ("renderthread_sync", "SyncStart", "IssueDrawCommandsStart"),
    ("commands_to_swap", "IssueDrawCommandsStart", "SwapBuffers"),
    ("swap_to_complete", "SwapBuffers", "FrameCompleted"),
    ("intended_vsync_to_completion_wall", "IntendedVsync", "FrameCompleted"),
)

def percentile(values, pct):
    ordered = sorted(values)
    if not ordered:
        return None
    return round(ordered[min(len(ordered) - 1, int(len(ordered) * pct))], 3)

def extract_windows(content):
    chunks = content.split("---PROFILEDATA---")
    windows = {}
    for previous, current in zip(chunks, chunks[1:]):
        head = re.findall(r"^Window:\s*(\S.*?)\s*$", previous, re.M)
        if not head:
            continue
        lines = current.strip().splitlines()
        if not lines or not lines[0].startswith("Flags,IntendedVsync,"):
            continue
        columns = next(csv.reader([lines[0]]))
        columns = [col for col in columns if col]
        frames = []
        for line in lines[1:]:
            if not line.startswith(("0,", "1,", "2,", "3,", "4,", "8,")):
                break
            values = next(csv.reader([line]))
            values = [value for value in values if value != ""]
            if len(values) != len(columns):
                raise ValueError("invalid framestats column count")
            record = dict(zip(columns, map(int, values)))
            frames.append(record)
        name = head[-1]
        if name in windows:
            raise ValueError("duplicate framestats window")
        windows[name] = frames
    return windows

def analyze(content):
    windows = extract_windows(content)
    if "NotificationShade" not in windows:
        raise ValueError("NotificationShade frame data missing")
    result = {}
    for window_name, frames in windows.items():
        valid = [f for f in frames if f["Flags"] == 0]
        entry = {
            "retained_frames": len(frames),
            "flags_zero": len(valid),
            "layout_or_other_flagged": len(frames) - len(valid),
            "stages_ms": {},
        }
        for label, start, end in FIELDS:
            durations = [(f[end] - f[start]) / 1000000 for f in valid
                         if f[start] > 0 and f[end] >= f[start]]
            entry["stages_ms"][label] = {
                "count": len(durations),
                "p50": percentile(durations, 0.5),
                "p90": percentile(durations, 0.9),
                "over_17_107ms": sum(v > 17.107 for v in durations),
            }
        for name in ("DequeueBufferDuration", "QueueBufferDuration"):
            durations = [f[name] / 1000000 for f in valid if f[name] >= 0]
            entry["stages_ms"][name] = {
                "count": len(durations),
                "p50": percentile(durations, 0.5),
                "p90": percentile(durations, 0.9),
                "over_17_107ms": sum(v > 17.107 for v in durations),
            }
        result[window_name] = entry
    return result

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file", type=Path)
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()
    raw = args.file.read_bytes()
    data = {
        "schema": "sm-t311-g1455-android11-frameinfo-stage-v1",
        "build": "SMT311G1441",
        "input_sha256": hashlib.sha256(raw).hexdigest(),
        "input_path": str(args.file),
        "window_stats": analyze(raw.decode()),
        "measurement_scope": "latest retained FrameInfo windows after G1454 physical swipes",
        "caveat": "GFXInfo ring can contain fewer frames than cumulative counts; flags nonzero excluded. Dequeue durations are nested inside rendering, never add them to other stages. No kernel FTRACE.",
    }
    text = json.dumps(data, sort_keys=True, indent=2) + "\n"
    if args.output:
        if args.output.exists():
            raise SystemExit("REFUSE_OVERWRITE_EXISTING_RECEIPT")
        args.output.write_text(text)
    print(text)

if __name__ == "__main__":
    main()
