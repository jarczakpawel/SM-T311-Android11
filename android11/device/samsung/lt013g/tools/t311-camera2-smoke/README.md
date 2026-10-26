# T311 Camera2 Smoke Test

Persistent Android 11 framework-level validation tool for the SM-T311 camera port.

It uses the public Camera2 API plus ImageReader YUV_420_888 output. A PASS proves a
real frame traveled through:

Camera2 -> CameraService -> HIDL HAL3 -> external/0 provider -> FIMC -> sensor.

Default test:
- rear camera 0
- front camera 1
- 6 alternating cycles
- final rear capture

The source is intentionally kept in device/samsung/lt013g/tools even when the APK
is not included in production PRODUCT_PACKAGES.


## G1143 stricter H.264 MP4 acceptance
- Older `T311_CAM2_AUDIT_VIDEO` PASS required only an MP4 over 4096 bytes.
  Real post-G1142 test 2026-10-24 yielded rear MP4 5342 bytes and front
  13933 bytes, so byte size alone is NOT proof of healthy 25/30fps H.264.
- The source now opens the real freshly recorded file with `MediaExtractor`,
  requiring exactly one AVC track with requested width/height, at least 20
  positive-size samples, at least one key sample, monotonically nondecreasing
  PTS spanning >= 1 second, and at least 8 bytes of AVC sample payload per packet, avoiding false negatives on extremely low-entropy scenes.
  Emits `T311_CAM2_AUDIT_AVC_SAMPLES` even on an incomplete recording.
- Not a strict H.264 entropy decoder: a corrupt P-slice may still pass. Do not
  claim verified H.264 until FFmpeg strict decode on original MP4 where safe.
- All generated MP4 files are removed from private app cache by `finally`.
  Without `av_audio=true` the microphone is never enabled.
- G1143 source compiled with javac Android API 30; a newly compiled APK and
  physical tests of this stricter condition are still separately REQUIRED.
  Do not count the older 11/11 result as evidence of the new sample gate.

## G1144 strict external decoder diagnostics (temporary private originals)
- Source now accepts `--ez video_only true --es video_role REAR|FRONT`
  combined with `--ez retain_private_mp4_for_qa true --ez av_audio false`
  to KEEP one very short app-private, silent MP4 after the Camera2 test.
- The path is logged as `T311_CAM2_QA_PRIVATE_VIDEO_PATH`. Pull it to the
  LOCAL Debian workspace using root ADB and run strict FFmpeg decode.
  Remove both the tablet cache file AND temporary host copy afterward.
  Default (without the opt-in flag) deletes MP4 exactly as before.
- Never enable `av_audio`, never expose/private-publish recorded video,
  never leave retained QA files or install-only APKs in Git.
- Real G1144 previous rear sample: 67 frames spanning 2.199s, 1979 bytes
  of AVC media data (roughly 30 B/frame), front: 56 frames spanning 2.2s,
  7484 bytes. These sizes alone neither prove nor disprove valid H.264.
