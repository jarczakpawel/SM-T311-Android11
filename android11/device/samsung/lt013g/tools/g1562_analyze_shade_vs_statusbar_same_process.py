#!/usr/bin/env python3
"""G1562: exact in-process Android11 StatusBar vs NotificationShade control.

Two independently recorded physical SM-T311 traces from the same installed
G1527 SystemUI. This compares independent distributions, NOT paired timestamps
or equal workloads. Never interpret a ratio as the speedup of any source patch.
"""
import argparse
import hashlib
import json
import runpy
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
EXPECTED_RAW = {
    "first": "43bdacf0f7d6f06f779f96b3b702727729a5efa4d6c96300d9a2c2faf49f707e",
    "repeat": "cef1310b2fe0a10b4ba389623ace561fde6661de772ac6e6bc42796ec4f0ca5d"
}
SOURCES = {
    "first": "g1541-physical-notificationshade-hwui-framestats.txt",
    "repeat": "g1557-repeat-physical-notificationshade-HWUI-FRAMESTATS.txt"
}
STAGES = ("renderthread_queue_wait", "commands_to_swap", "DequeueBufferDuration",
          "QueueBufferDuration", "intended_vsync_to_completion_wall")

def analyze(base=SCRIPT_DIR / "testdata"):
    script = SCRIPT_DIR / "g1455-analyze-android11-gfxinfo-framestats.py"
    if not script.is_file():
        raise ValueError("G1562_REFUSE_MISSING_ANDROID11_VERSIONED_FRAMEINFO_PARSER")
    parser = runpy.run_path(str(script), run_name="g1562_frameinfo_import")["analyze"]
    results = {}
    for name, relative in SOURCES.items():
        path = base / relative
        raw = path.read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        if digest != EXPECTED_RAW[name]:
            raise ValueError("G1562_REFUSE_CHANGED_RAW_PHYSICAL_INPUT_" + name)
        windows = parser(raw.decode("utf-8"))
        if set(("StatusBar", "NotificationShade")) - set(windows):
            raise ValueError("G1562_REFUSE_MISSING_COMPARED_WINDOWS")
        table = {}
        for window in ("StatusBar", "NotificationShade"):
            info = windows[window]
            if info["flags_zero"] < 20:
                raise ValueError("G1562_REFUSE_INSUFFICIENT_WINDOW_FRAMES")
            table[window] = {
                "retained_frames": info["retained_frames"],
                "valid_frames": info["flags_zero"],
                "stages": {k: info["stages_ms"][k] for k in STAGES}
            }
        ratios = {
            stage: round(table["NotificationShade"]["stages"][stage]["p50"] /
                         table["StatusBar"]["stages"][stage]["p50"], 3)
            for stage in STAGES
        }
        results[name] = {"raw_sha256": digest, "windows": table,
                         "shade_to_statusbar_median_ratio": ratios}
    return {
        "schema": "sm-t311-g1562-physical-aosp11-two-run-within-systemui-window-comparison-v1",
        "runs": results,
        "caveat": "Different window content, animation and geometry in same SystemUI process. Retained per-window frame windows are not timestamp-matched. Median ratios identify focus area, NOT causal effect, proved GPU speed or FPS gain. The Shader/BufferQueue/RenderThread root cause remains unproven. No modification of runtime or device."
    }

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    answer = json.dumps(analyze(), indent=2, sort_keys=True) + "\n"
    if args.output:
        if args.output.exists() or args.output.is_symlink():
            p.error("G1562_REFUSE_OVERWRITE")
        args.output.write_text(answer)
    print(answer)

if __name__ == "__main__":
    main()
