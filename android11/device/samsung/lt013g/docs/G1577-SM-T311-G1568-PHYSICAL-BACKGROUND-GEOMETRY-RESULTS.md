# G1577: SM-T311 G1568 Android11 notification-background invalidation guard — physical TWRP pass, **NO PROVEN FPS GAIN**

Physical Samsung SM-T311 lt013g Android11 Lineage18.1 (build property `SMT311G1441`, unchanged by the three-file SystemUI-only OTA), kernel `7.3.0-pjarczak` original accepted G1130 BOOT, Mesa 26.2.5 GLES2 Lima, TWRP3.1.1-0.

## Source change and audit

AOSP11 `NotificationBackgroundView.java` previously always called `invalidate()` in four setters of `actualHeight`, `clipTopAmount`, `clipBottomAmount` and `backgroundTop`, even when the geometry field was equal to the requested integer. The new Android framework G1566 source guard checks equality **only for SM-T311 device overlay**, otherwise preserves original AOSP11 behavior. It does not change the final geometry, notification shelf animation, GL/HWC, EGL buffers, kernel clocks, scheduler, GNSS, modem, camera, Codec2, audio or USB. Test `tools/test_g1566_notification_background_idempotence.py` PASS all **128** combinations of field × old × new × enabled. Device overlay bool `config_skipUnchangedNotificationBackgroundGeometryDamage=true`, generic bool false.

Signed G1570 TESTKEY TWRP full-system OTA was built and **actually flashed** by TWRP with installer RC0 and `operation_end status=0`. Host full QA G1569 compared 3487 SYSTEM members with G1527 physical baseline: exactly three changed SystemUI files, ext4 pass and 24 pinned hardware components exact, BOOT and recovery exact unchanged. No backup, no wipe, no user data or TWRP recovery write.

## Repeatable physical four-swipes measured after flash

Identical source-pinned silent G1575 first and G1576 warmed four-cycle open/close swipe scripts; their actual raw `dumpsys gfxinfo com.android.systemui framestats` and `SurfaceFlinger --latency NotificationShade#0` snapshots are committed in `tools/testdata/g1575-background-geometry-postflash-*` and `g1576-background-geometry-postflash-*`. Strict offline regression: `python3 device/samsung/lt013g/tools/test_g1577_two_physical_postflash_geometry_runs.py`; it checks all 6 raw SHA hashes, Android11 per-frame parser, and true physical timing counts.

| Physical SystemUI workload | Pre G1568 G1541 | Pre G1568 G1557 | Post G1575 first | Post G1576 warm |
|---|---:|---:|---:|---:|
| Janky frames / total | 235/300 (78.33%) | 248/308 (80.52%) | **263/309 (85.11%)** | **261/317 (82.33%)** |
| Frame p50 | 30 ms | 30 ms | 32 ms | 30 ms |
| Frame p90 | 53 ms | 53 ms | 65 ms | 53 ms |
| GPU p50 | 8 ms | 8 ms | 8 ms | 8 ms |
| Valid retained NotificationShade frames | 114 | 115 | 115 | 114 |
| RT `SyncQueued→SwapBuffers` over 17.107ms | 97 | 99 | 101 | 98 |
| RenderThread pending `SyncQueued→SyncStart` >8ms | 74 | 74 | 79 | 79 |
| `DequeueBufferDuration` >8ms | 31 | 30 | 30 | 27 |
| `QueueBufferDuration` >8ms | 0 | 0 | 0 | 0 |

Post-OTA physical frame-by-frame medians: first RT queue wait 11.882ms, draw issue to swap14.978ms, dequeue5.507ms, full paired RT30.341ms; warm RT wait12.556ms, GL14.909ms, dequeue5.491ms, full paired RT30.839ms. Pre-OTA queue wait medians11.824/11.919ms, paired RT30.647/29.361ms. **Neither the first nor warmed post-G1568 measurement demonstrates an FPS/jank improvement**, and the post results are somewhat worse. Cold/warm state, OS background activity and only 4 scripted cycles limit causal inference; no formal single-variable UI visual equivalence test was captured. The guard is source-semantically justified but its performance effect is unproven. Do **not** call this change a speed boost or misattribute remaining idle time to GPU driver waits.

SF physical refresh `17,107,264ns` /58.4547Hz remains unchanged. SF per-layer present records and GFXInfo process-wide frame counters are different populations; don't equate counts.

Raw SHA first: HWUI `293ab0ca0d7491226e7f3de6c090c26455abd02216dc0766f3998a614a514fd7`, SF `c5351c2533f9ab7ec890fac21cde19973ecfe76e9c55033f8b17e64e728e6629`, summary `f263c93655a8994ee8c3f0da8816bb34bd10c161016593923bc412ec5a15494c`.
Raw SHA warm: HWUI `7fcb47bccd973b9abda63c001f0a00e6dd9d53a513a430ed1c6a08f3b3733776`, SF `2a628cdda89069c41c351fd91127f6747b6e2389ddc931ab092f29b6ec8067db`, summary `1fa7a7665df0a0b57004feb2e6b483d097f34caa1d33f5d7ee5be1983481c915`.

The physical G1568 OTA and three installed SystemUI SHAs are preserved in the QA audit sources/receipts, with the accepted G1130 BOOT and original TWRP unchanged. Source patches and all current 867 Git revisions must be pinned before publishing. No GitHub push; the user plans to do that himself.
