# G1562: physical NotificationShade versus StatusBar cost within the same SystemUI process

Physical Samsung SM-T311 lt013g, Android11 Lineage 18.1 installed G1527 SystemUI, accepted G1130 kernel Linux7.3.0-pjarczak and Mesa26.2.5 Lima; **source-only diagnostic** on the same two physical 4-swipe FrameInfo captures G1541 and G1557. No new BOOT/SYSTEM/TWRP flash, reboot, backup, wipe, audible test or GitHub push. No CPU/GPU clocks, RenderThread priority, buffer count, GNSS, RIL, camera, Codec2 or USB altered.

The previously verified physical GFXInfo data includes independently retained Android11 FrameInfo samples for both `NotificationShade` and `StatusBar` windows in the same SystemUI PID. Script `tools/g1562_analyze_shade_vs_statusbar_same_process.py` combines the two already Git-tracked SHA-verified physical inputs; no new device trace needed. Run `python3 device/samsung/lt013g/tools/test_g1562_shade_statusbar_two_physical_runs.py` to verify raw SHA, frame count, p50 values and compare both independent captures. No input normalization or timestamps are invented.

| Physical FrameInfo p50 | G1541 Shade (114 valid) | G1541 StatusBar (45 valid) | G1557 Shade (115 valid) | G1557 StatusBar (46 valid) |
|---|---:|---:|---:|---:|
| `SyncQueued→SyncStart` | 11.824ms | 0.568ms | 11.919ms | 0.548ms |
| `IssueDrawCommandsStart→SwapBuffers` | 14.909ms | 3.387ms | 14.855ms | 3.414ms |
| `DequeueBufferDuration` (nested) | 5.698ms | 0.290ms | 5.323ms | 0.292ms |
| `QueueBufferDuration` (nested) | 0.697ms | 0.455ms | 0.678ms | 0.456ms |
| `IntendedVsync→FrameCompleted` | 45.455ms | 6.174ms | 45.729ms | 6.278ms |

The Shade has much larger median queue wait and GL/buffer work *within the same process*. These are **different windows with different sizes and contents, independent retained frame rings and non-aligned time spans**. Ratios ~21× RenderThread wait, ~4.4× GL work and ~19× dequeue are *descriptive*, NOT a causal GPU clock or expected speedup from any patch. StatusBar being faster does not prove a GPU driver bug is impossible; neither does the Shade being slower prove any particular overdraw, SurfaceFlinger stall or Linux scheduler error.

Source review of physically compiled `NotificationStackScrollLayout` shows that while shade animates, `onPreDrawDuringAnimation()` invokes `mShelf.updateAppearance()` and `updateClippingToTopRoundedCorner()`. `NotificationShelf.updateAppearance()` traverses children, adjusts clipping and icon states. Those operations can be necessary to maintain visuals (notification positions, animations and rounded corners), so **do not disable or cache them blindly**. Existing G1527 no-op clipping damage guard is installed and tested but not proven to improve FPS; target-specific source optimizations require matched A/B, preserved visuals and protected-hardware hashes before any new TWRP OTA.

Sources: https://developer.android.com/studio/profile/jank-detection ; https://android.googlesource.com/platform/frameworks/base/+/android11-release/packages/SystemUI/src/com/android/systemui/statusbar/notification/stack/NotificationStackScrollLayout.java
