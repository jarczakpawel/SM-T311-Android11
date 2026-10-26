#!/usr/bin/env python3
"""G1542: Android11 SurfaceFlinger present cadence + HWUI independent reporter.

SurfaceFlinger --latency <layer> returns a refresh-period line then three
timestamps per layer frame. The second column's successive differences show
presentation cadence. Do not equate this with app RenderThread service time
or with the total number of dropped application frames.
"""
import argparse
import hashlib
import json
import runpy
import statistics
from pathlib import Path

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def percentile(values, frac):
    if not values:
        return None
    ordered = sorted(values)
    return round(ordered[min(int(len(ordered) * frac), len(ordered) - 1)], 3)

def surface_stats(raw):
    lines = [line.strip() for line in raw.splitlines() if line.strip()]
    if not lines:
        raise ValueError("G1542_SF_EMPTY")
    try:
        period_ns = int(lines[0])
    except ValueError as exc:
        raise ValueError("G1542_BAD_REFRESH_PERIOD") from exc
    if not 10_000_000 < period_ns < 50_000_000:
        raise ValueError("G1542_IMPLAUSIBLE_REFRESH_PERIOD")
    triples = []
    for line in lines[1:]:
        fields = line.split()
        if len(fields) != 3:
            raise ValueError("G1542_NOT_THREE_COLUMNS")
        try:
            a, b, c = map(int, fields)
        except ValueError as exc:
            raise ValueError("G1542_INVALID_TIMESTAMP") from exc
        if b in (0, (1 << 63) - 1):
            continue
        if min(a, b, c) < 0:
            raise ValueError("G1542_NEGATIVE_TIMESTAMP")
        triples.append((a, b, c))
    if len(triples) < 10:
        raise ValueError("G1542_INSUFFICIENT_COMPLETE_FRAMES")
    # Timestamp rings can wrap; do not blend the old/new ends into one interval.
    triples.sort(key=lambda row: row[1])
    if any(b[1] <= a[1] for a, b in zip(triples, triples[1:])):
        raise ValueError("G1542_DUPLICATE_PRESENT_TIMESTAMP")
    delta = [(b[1] - a[1]) / 1e6 for a, b in zip(triples, triples[1:])]
    period_ms = period_ns / 1e6
    # On the physically scripted 4 x open/close test, one-second pauses between
    # gestures are deliberate; do not pretend they represent dropped frames.
    active = [v for v in delta if 0.5 * period_ms <= v <= 250.0]
    idle = [v for v in delta if v > 250.0]
    weird = [v for v in delta if v < 0.5 * period_ms]
    if len(active) < 8:
        raise ValueError("G1542_TOO_FEW_ACTIVE_PRESENT_INTERVALS")
    missed_vsynchronizations = sum(max(0, round(v / period_ms) - 1) for v in active)
    return {
        "refresh_ns": period_ns,
        "refresh_hz": round(1e9 / period_ns, 5),
        "frame_records_complete": len(triples),
        "active_presentation_intervals": len(active),
        "deliberate_or_idle_gaps_over_250ms": len(idle),
        "short_intervals_under_half_vsync": len(weird),
        "active_interval_p50_ms": percentile(active, 0.50),
        "active_interval_p90_ms": percentile(active, 0.90),
        "active_interval_max_ms": round(max(active), 3),
        "active_intervals_exceeding_1_5_vsync": sum(v > 1.5 * period_ms for v in active),
        "active_intervals_exceeding_2_5_vsync": sum(v > 2.5 * period_ms for v in active),
        "estimated_vsync_slots_between_presentations_not_displayed": missed_vsynchronizations,
        "caveat": "SF presentation cadence only. Deliberate swipe pauses over 250ms excluded. Not the same metric as GFXInfo app janky frames or actual GPU busy time.",
    }

def paired_renderthread_window(records, refresh_ns):
    # Same-frame endpoints: do NOT sum stage percentiles from different frames.
    valid = [(row['SwapBuffers'] - row['SyncQueued']) / 1e6
             for row in records if row.get('Flags') == 0
             and row['SyncQueued'] > 0 and row['SwapBuffers'] > row['SyncQueued']]
    if not valid:
        raise ValueError('G1544_NO_VALID_RENDERTHREAD_PAIRS')
    budget_ms = refresh_ns / 1e6
    return {
        'paired_valid_frames': len(valid),
        'p50_ms': percentile(valid, 0.5),
        'p90_ms': percentile(valid, 0.9),
        'over_1_display_refresh_budget': sum(ms > budget_ms for ms in valid),
        'budget_ms': round(budget_ms, 6),
        'warning': 'Same-frame SyncQueued->SwapBuffers includes renderthread queue waiting and drawing up to swap entry; it does not measure total GPU busy or guarantee a missed presentation for each sample.'
    }

def report(sf, hwui, summary):
    stats = surface_stats(sf.read_text())
    text = summary.read_text()
    if "device=lt013g" not in text or "build=SMT311G1441" not in text or "kernel=7.3.0-pjarczak" not in text:
        raise ValueError("G1542_NOT_EXPECTED_PHYSICAL_ROM")
    if "Total frames rendered:" not in text or "Janky frames:" not in text:
        raise ValueError("G1542_MISSING_HWUI_SUMMARY")
    contents = hwui.read_text()
    if "Window: NotificationShade" not in contents or "---PROFILEDATA---" not in contents:
        raise ValueError("G1542_MISSING_HWUI_NOTIFICATION_SHADE")
    # Use the already source-pinned Android11 FrameInfo analyzer, not a guessed
    # FrameTimeline API (unavailable before Android 12).
    analyzer = Path(__file__).with_name('g1455-analyze-android11-gfxinfo-framestats.py')
    if not analyzer.is_file():
        raise ValueError('G1542_REQUIRES_SAME_REPO_G1455_HWUI_ANALYZER')
    scripts = runpy.run_path(str(analyzer), run_name='g1542_import')
    window_stats = scripts['analyze'](contents)
    frame_records = scripts['extract_windows'](contents)['NotificationShade']
    paired = paired_renderthread_window(frame_records, stats['refresh_ns'])
    data = {
        "schema": "sm-t311-g1542-android11-surfaceflinger-present-cadence-v1",
        "surface": stats,
        "hwui_notification_shade": window_stats['NotificationShade'],
        "hwui_same_frame_syncqueued_to_swap": paired,
        "physical_summary": text,
        "raw_sha256": {
            "sf_latency": sha256(sf),
            "hwui_framestats": sha256(hwui),
            "physical_summary": sha256(summary),
        },
        "measurement_limitations": "No FTRACE, no claimed ioctl-level driver attribution. Retained SurfaceFlinger ring may contain fewer records than GFXInfo cumulative counters. This test is display presentation + app HWUI evidence, not a controlled driver A/B.",
    }
    return data

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("sf_latency", type=Path)
    parser.add_argument("hwui_framestats", type=Path)
    parser.add_argument("physical_summary", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    data = report(args.sf_latency, args.hwui_framestats, args.physical_summary)
    result = json.dumps(data, sort_keys=True, indent=2) + "\n"
    if args.output:
        if args.output.exists() or args.output.is_symlink():
            raise SystemExit("G1542_REFUSE_OVERWRITE_EXISTING_JSON")
        args.output.write_text(result)
    print(result)

if __name__ == "__main__":
    main()
