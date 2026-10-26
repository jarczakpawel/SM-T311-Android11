# G1456 - SM-T311 Android 11 G1441, physical SystemUI frame-stage diagnosis

2026-10-25. Device: SM-T311, Android 11 SMT311G1441, Linux 7.3.0-pjarczak, Mesa 26.2.5 Lima Mali-400, SkiaGL, DRM/minigbm HWC, SELinux Enforcing. Nothing in the boot image, kernel, GPU drivers, modem, GNSS, recording, camera, audio or Android runtime has been changed by this audit.

## Source of measurements

Android `dumpsys gfxinfo` cumulative data from G1450 (764 frames / 620 janky, 81.15%) was not a controlled workload and cannot establish individual stage costs. The G1453 attempt to use `cmd statusbar collapse/expand-notifications` produced **zero** SystemUI frames: those commands are not an adequate physical animation workload on this exact build. DO NOT interpret G1453's 0/0 as fluid or fast.

G1454 switched to actual screen input swipes while the display was already awake, without touching volume settings or emitting sounds. It performed four physical transitions up/down and restored the original expanded NotificationShade. Its fresh `gfxinfo` counts were **322 frames, 258 janky (80.12%), median 27ms, p90 48ms**, GPU median 8ms and p90 11ms, high input latency 191, Slow UI thread 46, Slow issue draw 47.

An immediate follow-up `dumpsys gfxinfo com.android.systemui framestats` retained the last 120 NotificationShade frame records, 114 with Flags=0 and 6 nonzero. Per-window cumulative NotificationShade count was 267 frames / 244 janky; separate StatusBar count 61 frames / 17 janky. The tool's ring buffer does not preserve all 322 frames or allow exactly matching aggregate jank with 114 retained unflagged timestamps.

Raw host diagnostic artifact (not an installed Android component):
`/home/debian/Projekty/samsung/artifacts/SM-T311-G1455-G1454-PHYSICAL-SYSTEMUI-FRAMESTATS-20261025.txt`
SHA256 `43af3e708caaddb7706485edb9cf3af5a55cc64f1a42ef1ed06cfb8232e94b68`.

Executable source parser `tools/g1455-analyze-android11-gfxinfo-framestats.py` with `tools/test_g1455_android11_gfxinfo.py` is committed **inside the Android device repo**. Three regression tests PASS. JSON output after one full analysis is recorded in external QA tree `linux-7.3-port/SMT311_G1455_G1454_SYSTEMUI_FRAME_STAGE_MEASUREMENT_20261025.json`.

## 114 valid NotificationShade frames (milliseconds)

| AOSP Android11 FrameInfo stage | p50 | p90 |
|---|---:|---:|
| Vsync -> HandleInputStart | 2.307 | 11.139 |
| HandleInputStart -> DrawStart | 2.850 | 5.417 |
| DrawStart -> SyncQueued | 0.571 | 1.175 |
| SyncQueued -> SyncStart (wait for RenderThread) | **8.818** | **18.508** |
| SyncStart -> IssueDrawCommandsStart | 0.53 | 1.05 |
| IssueDrawCommandsStart -> SwapBuffers (draw commands) | **14.819** | **26.185** |
| SwapBuffers -> FrameCompleted | 1.294 | 1.778 |
| DequeueBufferDuration (nested measurement, not additive) | **6.461** | **18.056** |
| QueueBufferDuration (nested, not additive) | 0.696 | 0.975 |
| IntendedVsync -> FrameCompleted (wall-clock) | 42.08 | 59.52 |

The display's actual native rate is 58.454701Hz (~17.107ms period). This is not a 60.000Hz panel. The stages must not be naively summed with buffer durations, and `SyncQueued -> SyncStart` is an inter-frame wait.

## Interpretation and next change gate

AOSP `frameworks/base/libs/hwui/FrameInfo.h`, `CanvasContext.cpp`, `SkiaOpenGLPipeline.cpp` and `JankTracker.cpp` confirm stage semantics. The UI thread is **not** the primary observed cost: 2.85ms median input-to-draw. The expensive stages are RenderThread backlog and command generation/GL path, with blocking `dequeueBuffer`. BufferQueue producer may stall waiting for a free surface and return of acquire/release fences. This points toward back-pressure in the HWUI/SurfaceFlinger buffer pipeline, **NOT** proof of an EGL, Mali GPU, DRM kernel driver or HWC defect.

Live DRM HWC had zero unexpected ValidateDisplay errors and zero PresentDisplay errors. Wallpaper is a 1920x1920 image using CLIENT composition with cropping; no physical evidence yet that scaling the wallpaper would materially improve shade animations, and changing it risks wallpaper crops and texture content. Do not blindly force HWC/GPU composition, alter globally the BufferQueue count, triple-buffer latency, fences, Skia rendering mode, CPU clock, GPU devfreq or MGLRU/LMKD.

Diagnostic constraint: accepted Linux 7.3 defconfig has `# CONFIG_FTRACE is not set`; `/sys/kernel/tracing` is absent; older optimization intentionally left `persist.traced.enable=0`, `traced=stopped`. Therefore generic Perfetto/systrace sched/ftrace analysis is unavailable without a change in kernel+services. Do **not** flash a new kernel only to collect traces when this is a stable car navigation tablet.

G1459 simpleperf privilege capability probe under unprivileged Android shell: `simpleperf stat -p <SystemUI PID> --duration 2 -e cpu-cycles,instructions` returned `failed to open perf event file for event_type cpu-cycles: Permission denied`. No perf samples were collected, no root/SELinux change attempted. Do not claim CPU hotspot profiling from this failed probe.

**Next actual implementation candidate must be justified by targeted controlled A/B**, retaining screen appearance, gesture response, sound, GNSS, modem, camera, USB and recording. The available 322-frame reproducible baseline is useful; it does not authorize speculative high-risk runtime changes.

## Canonical upstream documentation

- https://android.googlesource.com/platform/frameworks/base/+/android11-release/libs/hwui/FrameInfo.h
- https://source.android.com/docs/core/graphics/
- https://source.android.com/docs/core/tests/debug/systrace
- https://developer.android.com/topic/performance/rendering/profile-gpu
