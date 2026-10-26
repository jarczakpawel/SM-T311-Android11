# G1547: physically measured SM-T311 Android11 SystemUI SF / HWUI frame cadence

Scope: Samsung SM-T311 `lt013g`, physically accepted signed G1527 SystemUI, Android11 build property `SMT311G1441`, Linux `7.3.0-pjarczak` kernel G1130, Mesa26.2.5 GLES2 Lima. **Source-only diagnostic**, not a GPU/CPU/ROM patch. No new TWRP install, backup, wipe, property change, governor/clock changes or audio tests. The same 4 × silent shade swipes previously established in G1454/G1535 are reused. Diagnostic counter resets are ephemeral.

## Source-contained raw evidence (reproducible without hardware)

| Raw input | SHA256 |
|---|---|
| `tools/testdata/g1541-physical-notificationshade-sf-latency.txt` | `ce561deeaae98d86990c384660c69720b0dec9ffba64077e8175b425f1af4380` |
| `tools/testdata/g1541-physical-notificationshade-hwui-framestats.txt` | `43bdacf0f7d6f06f779f96b3b702727729a5efa4d6c96300d9a2c2faf49f707e` |
| `tools/testdata/g1541-physical-notificationshade-summary.txt` | `a2f2da42e74ec51e85ef9255c95eca0439f14a45df4cd7dfd7feb17c5c4cd44e` |

Run the independent synthetic **and physical fixture** regression on Debian:

```sh
python3 device/samsung/lt013g/tools/test_g1542_sf_cadence.py
```

A new physical capture (only with awake display, suitable when owner is not using the screen, run once; refuses to overwrite prior captures):

```sh
adb push device/samsung/lt013g/tools/g1541_silent_shade_sf_hwui_capture.sh /data/local/tmp/G1541-SILENT-SF-HWUI-CAPTURE.sh
adb shell sh /data/local/tmp/G1541-SILENT-SF-HWUI-CAPTURE.sh
# Preserve all 3 output files reported under /data/local/tmp/G1541-SM-T311-SHADE-CADENCE-*
```

The Python analyzer reads three raw files and outputs one JSON. The `g1455` Android11 FrameInfo analyzer is bundled in the same device source repository. It does **not** use Android12+ FrameTimeline.

## Actual device results

- SF layer `NotificationShade#0`, physical refresh period `17,107,264 ns` = **58.4547 Hz**. SF latency ring 39 completed presentations, 37 active inter-present intervals, 1 deliberate/idle gap over250ms excluded. Interval p50 **17.112ms**, p90 **34.214ms**, 15/37 active intervals longer than 1.5 display periods.
- Same workload HWUI total 300 frames, 235 janky (**78.33%**), whole-frame median 30ms, GPU reported p50 8ms.
- The last retained 120 NotificationShade FrameInfo records contain 114 valid records; paired **same-frame** `SyncQueued→SwapBuffers` exceeds actual 17.107ms VSYNC budget on **97/114 frames**, median **30.647ms**, p90 **45.276ms**. Substages: `SyncQueued→SyncStart` p50 11.824ms, `IssueDrawCommandsStart→SwapBuffers` p50 14.909ms, `DequeueBufferDuration` p50 5.698ms. The dequeue duration is nested and **must not be added** again to the command-to-swap stage.
- Only 39 SF presentations belong to the sampled `NotificationShade#0` layer and GFXInfo 300 frames are per-process counters across windows; **the populations are not interchangeable**. Do not calculate an end-to-end FPS from their ratio or equate individual janky HWUI app frames with 1:1 SF missed presents. No inference of an individual broken ioctl, GL fence or Mali400 driver bug follows.
- Actual working RenderThread priority is nice -10 and CPU frequency snapshot reported 1.4GHz under `schedutil`. This does not prove CPU stays at peak throughout the gesture. No FTRACE in accepted kernel and simpleperf shell perf counter access denied; avoid unsupported exact GPU-busy-attribution claims.

## Publication impact

Source diagnostics and physical testdata are Git-controlled and do not create ignored runtime ELF blobs. The previously accepted 6/6 Mesa26.2.5 source-built bit-exact guard (`mesa26-g816/source-rebuild/verify_bitexact_six.py`) remains intact, as do AOSP 863-repo manifests and the accepted kernel sources. There is no new firmware image to flash because no supported new kernel/HWUI/Mesa runtime change was made. The real bottleneck for ~80% app-reported jank requires further supported RenderThread wakeup/GL/BufferQueue analysis and controlled A/B before changing clocks, EGL swap damage, buffer counts or native fences.

Source references: https://developer.android.com/studio/profile/jank-detection ; https://source.android.com/docs/core/tests/debug/systrace ; https://chromium.googlesource.com/chromium/src/build/+/689a0a184e54c7a50a05964ae666b82e96ea549f/android/pylib/perf/surface_stats_collector.py
