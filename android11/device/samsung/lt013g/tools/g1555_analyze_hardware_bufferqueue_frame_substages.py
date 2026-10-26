#!/usr/bin/env python3
"""G1555: exact same-frame stage decomposition on real Android 11 FrameInfo.

Does not sum unrelated percentiles or confuse GPU completion timestamps with
dequeue/queue durations. Analysis only; no writes to Android or system images.
"""
import argparse
import hashlib
import json
import runpy
import statistics
from pathlib import Path

FRAMEINFO = Path(__file__).with_name("g1455-analyze-android11-gfxinfo-framestats.py")
PERIOD_NS = 17_107_264
SLOW_NS = 8_000_000

def percentile(values, fraction):
    ordered = sorted(values)
    return round(ordered[min(len(ordered) - 1, int(len(ordered) * fraction))], 3)

def analyze(input_path):
    if not FRAMEINFO.is_file():
        raise ValueError("G1555_MISSING_VERSION_MATCHED_ANDROID11_FRAMEINFO")
    parse = runpy.run_path(str(FRAMEINFO), run_name="g1555_frameinfo_import")["extract_windows"]
    raw = input_path.read_bytes()
    records = parse(raw.decode("utf-8"))["NotificationShade"]
    accepted = []
    invalid = 0
    names = ("SyncQueued", "SyncStart", "IssueDrawCommandsStart", "SwapBuffers", "FrameCompleted")
    for record in records:
        if record.get("Flags") != 0:
            continue
        if any(record.get(name, 0) <= 0 for name in names):
            invalid += 1
            continue
        if any(record[b] < record[a] for a, b in zip(names, names[1:])):
            invalid += 1
            continue
        deq = record["DequeueBufferDuration"]
        enq = record["QueueBufferDuration"]
        drawswap = record["SwapBuffers"] - record["IssueDrawCommandsStart"]
        if deq < 0 or enq < 0 or deq > drawswap:
            invalid += 1
            continue
        accepted.append({
            "wait_ns": record["SyncStart"] - record["SyncQueued"],
            "sync_ns": record["IssueDrawCommandsStart"] - record["SyncStart"],
            "drawswap_ns": drawswap,
            "dequeue_ns": deq,
            "queue_ns": enq,
            # Actual physical source confirms dequeue starts after IssueDraw,
            # in getFrame / Mesa EGL buffer acquisition. A non-overlap check
            # above prevents invented negative residual durations.
            "non_dequeue_drawswap_ns": drawswap - deq,
            "paired_rt_ns": record["SwapBuffers"] - record["SyncQueued"]
        })
    if len(accepted) < 12:
        raise ValueError("G1555_TOO_FEW_VALID_SAME_FRAME_SAMPLES")
    def count(test):
        return sum(1 for f in accepted if test(f))
    result = {
        "schema": "sm-t311-g1555-android11-same-frame-hwui-dequeue-queue-rt-wait-v1",
        "raw_sha256": hashlib.sha256(raw).hexdigest(),
        "retained_window_frames": len(records),
        "valid_same_frame_records": len(accepted),
        "excluded_invalid_or_nonmonotonic": invalid,
        "display_budget_ms": PERIOD_NS / 1e6,
        "counts": {
            "same_frame_rt_over_vsync": count(lambda f: f["paired_rt_ns"] > PERIOD_NS),
            "renderthread_wait_over_8ms": count(lambda f: f["wait_ns"] > SLOW_NS),
            "gl_issue_to_swap_over_vsync": count(lambda f: f["drawswap_ns"] > PERIOD_NS),
            "dequeue_over_8ms": count(lambda f: f["dequeue_ns"] > SLOW_NS),
            "dequeue_over_vsync": count(lambda f: f["dequeue_ns"] > PERIOD_NS),
            "queue_over_8ms": count(lambda f: f["queue_ns"] > SLOW_NS),
            "long_gl_and_long_dequeue": count(lambda f: f["drawswap_ns"] > PERIOD_NS and f["dequeue_ns"] > SLOW_NS),
            "long_gl_without_long_dequeue": count(lambda f: f["drawswap_ns"] > PERIOD_NS and f["dequeue_ns"] <= SLOW_NS),
            "slow_rt_and_wait8_but_dequeue_short": count(lambda f: f["paired_rt_ns"] > PERIOD_NS and f["wait_ns"] > SLOW_NS and f["dequeue_ns"] <= SLOW_NS),
            "non_dequeue_drawswap_over_vsync": count(lambda f: f["non_dequeue_drawswap_ns"] > PERIOD_NS),
        },
        "p50_ms": {key: percentile([f[key] / 1e6 for f in accepted], 0.5) for key in accepted[0]},
        "p90_ms": {key: percentile([f[key] / 1e6 for f in accepted], 0.9) for key in accepted[0]},
        "physical_code_gating": "CanvasContext::draw marks IssueDrawCommandsStart before pipeline getFrame and records DequeueBufferDuration from ANativeWindow lastDequeue if it starts after SyncStart. All accepted physical duration values are <= corresponding IssueDraw->Swap timestamp span.",
        "caveats": "Correlations over one 114-frame GFXInfo ring cannot identify a specific blocked sys_call, physical GL fence, GPU bottleneck or scheduler root cause. QueueBufferDuration is recorded by Android11 CanvasContext from ANativeWindow, not identical to whole app GPU or SF composition. GFXInfo stage endpoints are timestamps, and percentiles of different stages are NOT additive. No runtime FPS improvement claimed."
    }
    return result

def main():
    arg = argparse.ArgumentParser()
    arg.add_argument("framestats", type=Path)
    arg.add_argument("--output", type=Path)
    options = arg.parse_args()
    data = analyze(options.framestats)
    content = json.dumps(data, sort_keys=True, indent=2) + "\n"
    if options.output:
        if options.output.exists() or options.output.is_symlink():
            arg.error("G1555_NO_OVERWRITE_PRIOR_PHYSICAL_REPORT")
        options.output.write_text(content)
    print(content)

if __name__ == "__main__":
    main()
