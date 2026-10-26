# G1555 — SM-T311: real Android11 RenderThread / BufferQueue frame decomposition

On the physically accepted SM-T311 Android11 G1527 SystemUI (Linux7.3.0-pjarczak kernel G1130, Mesa26.2.5 Lima) we analyzed the exact, already recorded **same 114 valid NotificationShade FrameInfo samples** from G1541, without changing any runtime binary, kernel, EGL, GPU clocks, TWRP, Android properties, or user data. This is **diagnostic source code**, not a new FPS fix or a flashable ROM. No backup, wipe, reboot, sound, GitHub push.

## Source and reproducibility

`device/samsung/lt013g/tools/g1555_analyze_hardware_bufferqueue_frame_substages.py` reads source-committed physical `tools/testdata/g1541-physical-notificationshade-hwui-framestats.txt`, SHA256 `43bdacf0f7d6f06f779f96b3b702727729a5efa4d6c96300d9a2c2faf49f707e`, via the existing Git-tracked **Android11** FrameInfo parser `g1455-analyze-android11-gfxinfo-framestats.py`. The regression `test_g1555_bufferqueue_frame_substages.py` asserts all exact counts, SHA and stage medians; it is entirely source-based and runs offline:

```sh
python3 device/samsung/lt013g/tools/test_g1555_bufferqueue_frame_substages.py
python3 device/samsung/lt013g/tools/g1555_analyze_hardware_bufferqueue_frame_substages.py \
  device/samsung/lt013g/tools/testdata/g1541-physical-notificationshade-hwui-framestats.txt
```

Physical display period: **17.107264ms / 58.4547Hz**. Android11 `FrameInfo.h` stores `SyncQueued`, `SyncStart`, `IssueDrawCommandsStart`, `SwapBuffers`, `DequeueBufferDuration`, `QueueBufferDuration`. In the checked source `CanvasContext::draw` records `IssueDrawCommandsStart` **before** `getFrame()`, rendering and `swapBuffers`. It samples `ANativeWindow_getLastDequeueDuration` and `ANativeWindow_getLastQueueDuration` after submitting each frame, zeroing dequeue duration if dequeue began before frame sync. For all valid physical samples **dequeue duration fits within IssueDrawCommandsStart→SwapBuffers**. We subtract dequeue within each individual frame, never subtract stage percentiles across unrelated frames.

## Physically observed 114 frame classification

| Same-frame measurement | Count | p50 | p90 |
|---|---:|---:|---:|
| `SyncQueued→SwapBuffers` > physical 17.107ms | **97/114** | 30.647ms | 45.276ms |
| `SyncQueued→SyncStart` wait >8ms | **74/114** | 11.824ms | 23.178ms |
| `IssueDrawCommandsStart→SwapBuffers` >17.107ms | **39/114** | 14.909ms | 31.493ms |
| `DequeueBufferDuration` >8ms | **31/114** | 5.698ms | 21.236ms |
| `DequeueBufferDuration` >17.107ms | **18/114** | — | — |
| `QueueBufferDuration` >8ms | **0/114** | 0.697ms | 1.044ms |
| GL issue stage long + dequeue >8ms, **same frame** | **30/114** | — | — |
| GL issue stage long + dequeue <=8ms, **same frame** | **9/114** | — | — |
| Paired RT stage long, wait >8ms, dequeue <=8ms | **54/114** | — | — |
| Same-frame `IssueDraw→Swap` minus dequeue duration >17.107ms | **0/114** | 9.654ms | 12.340ms |

These counts show **two separate contributors**: scheduling of the RenderThread after the UI queues work, and occasional long buffer dequeue inside the draw/swap stage. Normal `queueBuffer` duration is short in these samples. The 31 long dequeue intervals are **not the entire 97 slow RenderThread frames**, so a dequeue-only driver patch cannot be assumed to fix every janky frame. Nothing here proves an Android/Mesa bug, precise GPU fence blocking, CPU thermal throttling or composition miss for each app frame. The nested dequeue duration **must never** be added to the encompassing GL issue-to-swap window. No new FPS improvement is claimed.

No usable accepted kernel FTRACE (and unprivileged task stacks/perf denied); do not invent driver call stacks. Avoid random EGL swap buffer count, render ahead depth, governors or clocks on a device with working Mesa26.2.5, GPS, modem, camera, USB and Codec2. Future changes must be tested against exactly the source-controlled G1541 data baseline, with A/B physical measurements before any further signed full-system TWRP flash.

References: https://android.googlesource.com/platform/frameworks/base/+/android11-release/libs/hwui/FrameInfo.h and https://developer.android.com/topic/performance/rendering/profile-gpu
