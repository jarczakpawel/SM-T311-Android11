# G1551 — SM-T311 Android11: physical RenderThread CPU vs gesture wall-time

Scope: source-controlled read-only diagnostics from the actual SM-T311 lt013g, Android11 Lineage18.1 physically installed G1527 SystemUI, `7.3.0-pjarczak` original G1130 kernel. **No runtime source patch, BOOT/SYSTEM/TWRP flash, property/governor/clock change, backup, wipe, reboot, audio test, GitHub push or binary modification.**

Previous physically captured G1549 evidence: SurfaceFlinger display 17.107264ms refresh, 114 Android11 valid NotificationShade frames, 97/114 same-frame `SyncQueued->SwapBuffers` intervals exceed one physical refresh period, p50 30.647ms; GPU reported p50 ~8ms. Does not identify GPU driver waits or kernel stall.

## New physical scheduler counters: already-available kernel debug `sched`

The accepted Linux7.3 kernel does **not** expose `/proc/<pid>/task/<tid>/schedstat` and forbids unprivileged kernel `/proc/<pid>/task/<tid>/stack`, but does allow ADB-shell read of **`/proc/<pid>/task/<tid>/sched`**, including `se.sum_exec_runtime` (ms of CPU execution), `nr_voluntary_switches`, `nr_involuntary_switches`. RenderThread already has **nice -10**. Physical data acquisition uses `tools/g1550_silent_renderthread_sched_cpu_profile.sh`, validates boot/device/awake screen, discovers the live RenderThread TID by its thread name, reads UI and RenderThread scheduler counters before/after each of four open/close cycles, and stores a TAB-delimited 8-row log. Inputs are touch swipes only with no sound, no writes outside uniquely named `/data/local/tmp` diagnostic output. It does not itself change scheduling.

Actual 8-gesture physical captured file: `tools/testdata/g1550-physical-renderthread-cpu-wall-4swipes.tsv`, SHA256 `5528d9f2084fd61817f065db98ee7709bd0aaaa86c3ad96c7618a92cd4ec35c3`. Run portable regression:

```sh
python3 device/samsung/lt013g/tools/test_g1551_renderthread_sched_cpu.py
python3 device/samsung/lt013g/tools/g1551_analyze_renderthread_sched_cpu.py \
  device/samsung/lt013g/tools/testdata/g1550-physical-renderthread-cpu-wall-4swipes.tsv
```

### Physical results

| Metric | Total over 8 gestures | Per-gesture median |
|---|---:|---:|
| Wall around `input touchscreen swipe` command and counter reads | 14290 ms | 1785 ms |
| RenderThread actual CPU executed | **1920.681 ms** | 262.135 ms |
| SystemUI UI main thread actual CPU executed | 1080.849 ms | 143.206 ms |
| RenderThread voluntary context switches | 1240 | 154.5 |
| RenderThread involuntary context switches | 1075 | 140 |

CPU execution / measured wall: RenderThread **13.441%**, main UI **7.564%** (separate threads; these fractions are not exclusive). **Warning:** Wall includes `/proc` reads, shell process startup/ADB `input` touch injection overhead and periods in which there are no new frames to render. The test cannot separate actual GPU fence blocking from normal frame pacing/idle, nor reconstruct instantaneous CPU frequency or thermal activity. High context-switch counts alone do not prove a scheduler or GPU bug. Do NOT claim RenderThread spends the remaining 86.56% blocked on a particular subsystem. The result supports developing finer tracing/instrumentation before forcing render-ahead depth, GPU frequency, EGL buffer counts, `schedutil` tuning, FTRACE changes or kernel rebuilding.

This source + real physical fixture was added to the lt013g Android device Git tree specifically so later GitHub users can inspect and reproduce the analysis without local host files. Original kernel, working RIL, GNSS, MFC/Codec2, USB, audio, camera, EGL/Lima Mesa 26.2.5 and TWRP preserved.
