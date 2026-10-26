#!/usr/bin/env python3
"""SM-T311 Android 11: parse passive gfxinfo framestats, never alter device.

Usage: python3 g651_android11_framestats_readonly.py captured_raw.txt
Writes JSON to stdout; no ADB, UI wake, reset, or side effects. A sample may
contain multiple SystemUI ViewRoots and is NOT a supervised interaction test.
"""
import json,math,sys
from pathlib import Path
if len(sys.argv)!=2:
    raise SystemExit(__doc__)
text=Path(sys.argv[1]).read_text()
cols=("Flags IntendedVsync Vsync OldestInputEvent NewestInputEvent "
      "HandleInputStart AnimationStart PerformTraversalsStart DrawStart "
      "SyncQueued SyncStart IssueDrawCommandsStart SwapBuffers FrameCompleted "
      "DequeueBufferDuration QueueBufferDuration GpuCompleted").split()
assert len(cols)==17
observed={}
for line in text.splitlines():
    x=line.strip().split(",")
    if len(x)<17 or not x[0].isdigit():
        continue
    try:r=dict(zip(cols,(int(v) for v in x[:17])))
    except ValueError:continue
    if r["Flags"]!=0 or r["IntendedVsync"]<=0 or r["FrameCompleted"]<=r["IntendedVsync"]:
        continue
    observed[(r["IntendedVsync"],r["FrameCompleted"])]=r
rows=list(observed.values())
stages={n:[] for n in ["frame_ms","input_to_draw_ms","draw_to_syncqueue_ms",
                       "sync_to_swap_ms","dequeue_ms","queue_ms"]}
for r in rows:
    for n,a,b in [("frame_ms","IntendedVsync","FrameCompleted"),
                  ("input_to_draw_ms","HandleInputStart","DrawStart"),
                  ("draw_to_syncqueue_ms","DrawStart","SyncQueued"),
                  ("sync_to_swap_ms","SyncStart","SwapBuffers")]:
        if r[a]>0 and r[b]>=r[a]:
            stages[n].append((r[b]-r[a])/1e6)
    for n,v in [("dequeue_ms","DequeueBufferDuration"),("queue_ms","QueueBufferDuration")]:
        if r[v]>=0:stages[n].append(r[v]/1e6)
def pct(a,q):
    if not a:return None
    v=sorted(a)
    return round(v[max(0,min(len(v)-1,math.ceil(len(v)*q)-1))],3)
report={"device":"SM-T311 Android 11","frame_records":len(rows),
        "metrics":{n:{"n":len(v),"p50":pct(v,.50),"p90":pct(v,.90),
                      "p95":pct(v,.95),"p99":pct(v,.99)} for n,v in stages.items()},
        "warnings":["Passive multiple-ViewRoot history; not controlled swipe/scroll or live FPS.",
                    "Different stage p95s cannot be summed.",
                    "sync_to_swap is not pure GPU time; no HWC source of jank proven.",
                    "Zero/missing GpuCompleted cannot justify GPU comparison.",
                    "No Android, property, kernel or display modifications."]}
print(json.dumps(report,ensure_ascii=False,indent=2))
