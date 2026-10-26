# G1558 — SM-T311 two physical independent matched RenderThread/BufferQueue captures

On the actual Samsung SM-T311 Android 11, **physically accepted G1527 SystemUI** and G1130 kernel Linux7.3.0-pjarczak, Mesa 26.2.5 Lima, we performed a second identical silent four-cycle shade open/close test to check whether G1555's observations are stable. This is **source-only measurement and reproducer**, no new graphics/Android build, OTA/TWRP, GPU/CPU clocks, backup, wipe or audible test. No GitHub push.

Commands: `tools/g1541_silent_shade_sf_hwui_capture.sh` originally; `tools/g1557_repeat_silent_sf_hwui_capture.sh` same four gestures, new unique filenames. Both scripts require device build and awake display and emit only diagnostic files under `/data/local/tmp`. Both independent raw captures are committed under `tools/testdata`. Run `python3 device/samsung/lt013g/tools/test_g1555_bufferqueue_frame_substages.py` to reproduce the classification directly from immutable physical FrameInfo files.

## Real second-run exact raw artifacts

- `tools/testdata/g1557-repeat-physical-notificationshade-HWUI-FRAMESTATS.txt` SHA256 **cef1310b2fe0a10b4ba389623ace561fde6661de772ac6e6bc42796ec4f0ca5d**.
- `tools/testdata/g1557-repeat-physical-notificationshade-SF-LATENCY.txt` SHA256 **0797fa28a7f785eb118e91c4f760640928b7b29b21a5351b85dd40e968e18334**.
- `tools/testdata/g1557-repeat-physical-notificationshade-SUMMARY.txt` SHA256 **521a2a2fa2edc5d0c8daae240aa550cbe438b091e9adf107ff069744d426929b**.

The first physical run G1541 raw hashes and analysis are documented in `G1555-ANDROID11-PHYSICAL-RENDERTHREAD-DEQUEUE-QUEUE-SAME-FRAME-CLASSIFICATION.md`.

| Physical measurement | G1541 original | G1557 repeated |
|---|---:|---:|
| Display refresh period | 17.107264 ms | 17.107264 ms |
| Total GFXInfo frames / janky | 300 / 235 (78.33%) | 308 / 248 (80.52%) |
| Valid retained **same-frame** NotificationShade FrameInfo | 114 | 115 |
| RenderThread `SyncQueued→SwapBuffers` >17.107ms | 97 | 99 |
| RenderThread `SyncQueued→SyncStart` >8ms | 74 | 74 |
| `DequeueBufferDuration` >8ms | 31 | 30 |
| `DequeueBufferDuration` >17.107ms | 18 | 17 |
| `IssueDrawCommandsStart→SwapBuffers` >17.107ms | 39 | 34 |
| `QueueBufferDuration` >8ms | **0** | **0** |
| Long issue+swap + dequeue >8ms on **same frame** | 30 | 27 |
| Same-frame RT span median | 30.647ms | 29.361ms |
| Queue wait median | 11.824ms | 11.919ms |
| Dequeue duration median | 5.698ms | 5.323ms |
| Queue duration median | 0.697ms | 0.678ms |
| SF presented interval median | 17.112ms | 17.113ms |
| SF presented interval p90 | 34.214ms | 34.214ms |

Both runs are independent captures on the **same** physical installed ROM. They show reproducible high RenderThread scheduling/queue delay plus variable buffer-dequeue latency, and consistently short `queueBuffer` timing. The pipeline needs targeted investigation; **these observations do not prove** GPU clock issues, a faulty Mesa driver, an exact native fence stall or a safe value of Android's extra-buffer/render-ahead capacity. SurfaceFlinger present records and app GFXInfo frames are different populations; neither 39 vs300 nor 41 vs308 is a valid FPS denominator. Do not count idle 1s gesture pauses as missed displayed frames.

Keep working kernel, GNSS, RIL/modem, camera/Codec2, audio, USB and the six SHA-exact source-built Mesa26.2.5 libraries intact. A production C++ fix can be justified only by source evidence and a controlled physical A/B with enough warm samples and complete 24-hardware-file/BOOT/RECOVERY QA before any new TWRP flash. This iteration makes no FPS performance claim.
