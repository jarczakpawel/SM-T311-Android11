#!/usr/bin/env python3
"""Passive SM-T311 Android11 gfxinfo freshness; no ADB or modifications."""
import argparse
import json
import math
from pathlib import Path

COL=("Flags IntendedVsync Vsync OldestInputEvent NewestInputEvent HandleInputStart "
"AnimationStart PerformTraversalsStart DrawStart SyncQueued SyncStart "
"IssueDrawCommandsStart SwapBuffers FrameCompleted DequeueBufferDuration "
"QueueBufferDuration GpuCompleted").split()

def pct(x,q):
    return round(sorted(x)[math.ceil(len(x)*q)-1],3) if x else None

def parse(raw):
    frames={}
    for line in raw.splitlines():
        parts=line.strip().split(",")
        if len(parts)<len(COL) or not parts[0].isdigit():continue
        try:r=dict(zip(COL,(int(x) for x in parts[:len(COL)])))
        except ValueError:continue
        if r["Flags"]!=0 or r["IntendedVsync"]<=0 or r["FrameCompleted"]<=r["IntendedVsync"]:continue
        frames[(r["IntendedVsync"],r["FrameCompleted"])]=r
    return list(frames.values())

def stage_stats(frames):
    columns={
      "frame_ms":lambda f:(f["FrameCompleted"]-f["IntendedVsync"])/1e6,
      "input_to_draw_ms":lambda f:(f["DrawStart"]-f["HandleInputStart"])/1e6
        if f["HandleInputStart"]>0 and f["DrawStart"]>=f["HandleInputStart"] else None,
      "sync_to_swap_ms":lambda f:(f["SwapBuffers"]-f["SyncStart"])/1e6
        if f["SyncStart"]>0 and f["SwapBuffers"]>=f["SyncStart"] else None,
      "dequeue_ms":lambda f:f["DequeueBufferDuration"]/1e6 if f["DequeueBufferDuration"]>=0 else None,
      "queue_ms":lambda f:f["QueueBufferDuration"]/1e6 if f["QueueBufferDuration"]>=0 else None
    }
    out={}
    for k,fn in columns.items():
        v=[fn(f) for f in frames]
        v=[x for x in v if x is not None]
        out[k]={"count":len(v),"p50":pct(v,.50),"p95":pct(v,.95),"p99":pct(v,.99)}
    return out

def analyze(raw,uptime_seconds,windows=(30,60,120,300,600),minimum=30):
    f=parse(raw)
    if not f:raise ValueError("no valid framedata")
    newest=max(a["IntendedVsync"] for a in f)/1e9
    if uptime_seconds-newest < -3:raise ValueError("inconsistent clocks")
    result={"board":"Samsung SM-T311 Android11",
        "records":len(f),"snapshot_uptime_s":uptime_seconds,
        "newest_frame_age_s":round(uptime_seconds-newest,3),
        "windows":{},
        "warnings":[
          "Read-only mixed ViewRoot history, NOT matched scroll or live FPS",
          "Clock MONOTONIC may differ from /proc/uptime BOOTTIME after suspend",
          "Window p95 is INCONCLUSIVE with under 30 frames",
          "Frame stage percentiles are not additive",
          "No Android/SystemUI state mutated, no RCU enabled"]}
    for window in windows:
        a=[x for x in f if 0<=uptime_seconds-x["IntendedVsync"]/1e9<=window]
        result["windows"][str(window)]={"count":len(a),
           "reliability":"INCONCLUSIVE_LOW_N" if len(a)<minimum else "DESCRIPTIVE_PASSIVE_ONLY",
           "stages":stage_stats(a)}
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("raw_file")
    p.add_argument("--uptime-seconds",type=float,required=True)
    p.add_argument("--min-sample",type=int,default=30)
    a=p.parse_args()
    if a.min_sample<1:p.error("invalid sample threshold")
    print(json.dumps(analyze(Path(a.raw_file).read_text(),a.uptime_seconds,
                            minimum=a.min_sample),indent=2,ensure_ascii=False))
if __name__=="__main__":main()
