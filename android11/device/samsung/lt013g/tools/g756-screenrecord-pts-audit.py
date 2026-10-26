#!/usr/bin/env python3
"""Non-mutating video-only screenrecord timestamp/codec regression analysis.

Android screenrecord uses a mirrored virtual display; static screens may
produce no encoded frames until SurfaceFlinger has fresh content to compose.
Measure both the capture wall interval and MP4 sample timestamps: average
frame rate alone does not prove dropped frames or constant-rate capability.
"""
import argparse
import json
import subprocess
import statistics
from pathlib import Path

def ffprobe(path):
    r = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=codec_name,profile,width,height,nb_frames,avg_frame_rate:"
                          "format=duration",
         "-of", "json", str(path)],
        check=True, capture_output=True, text=True)
    return json.loads(r.stdout)

def sample_timestamps(path):
    r = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0",
         "-show_entries", "packet=pts_time", "-of", "csv=p=0", str(path)],
        check=True, capture_output=True, text=True)
    return sorted(float(value.strip()) for value in r.stdout.splitlines()
                  if value.strip() and value.strip() != "N/A")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video", type=Path)
    ap.add_argument("--wall-seconds", type=float,
                    help="Monotonic seconds between actual start/stop service requests")
    ap.add_argument("--min-duration-ratio", type=float, default=0.92)
    ap.add_argument("--max-duration-ratio", type=float, default=1.15)
    ap.add_argument("--expected-width", type=int)
    ap.add_argument("--expected-height", type=int)
    ap.add_argument("--strict", action="store_true",
                    help="Return exit 2 for a measured wall-clock duration shortfall")
    args = ap.parse_args()
    if not args.video.is_file():
        ap.error("Video not found")
    probe = ffprobe(args.video)
    stream = probe["streams"][0]
    pts = sample_timestamps(args.video)
    if not pts:
        ap.error("No presentation timestamps: cannot audit")
    duration = float(probe["format"]["duration"])
    intervals = [b - a for a, b in zip(pts, pts[1:])]
    result = {
        "codec": stream.get("codec_name"),
        "profile": stream.get("profile"),
        "width": stream.get("width"),
        "height": stream.get("height"),
        "duration_seconds": round(duration, 4),
        "packets": len(pts),
        "timestamp_span_seconds": round(pts[-1]-pts[0], 4),
        "average_frames_per_second": round(len(pts)/duration, 3),
        "median_frame_interval_ms": (round(statistics.median(intervals)*1000, 2)
                                     if intervals else None),
        "max_gap_seconds": (round(max(intervals), 3) if intervals else None),
        "gaps_over_200ms": sum(gap > 0.2 for gap in intervals),
        "wall_elapsed_seconds": args.wall_seconds,
    }
    if args.expected_width is not None or args.expected_height is not None:
        result["resolution_match"] = (
            args.expected_width == result["width"]
            and args.expected_height == result["height"])
    if args.wall_seconds is not None:
        if args.wall_seconds <= 0:
            ap.error("--wall-seconds must be positive")
        ratio = duration / args.wall_seconds
        result["duration_to_wall_ratio"] = round(ratio, 3)
        result["wall_duration_qa"] = (
            "WARN_SHORT_DURATION" if ratio < args.min_duration_ratio else
            "WARN_LONG_DURATION" if ratio > args.max_duration_ratio else
            "PASS")
    print(json.dumps(result, indent=2))
    if args.strict and (result.get("wall_duration_qa") in
                        ("WARN_SHORT_DURATION", "WARN_LONG_DURATION")
                        or result.get("resolution_match") is False):
        raise SystemExit(2)

if __name__ == "__main__":
    main()
