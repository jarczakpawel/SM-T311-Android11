# SM-T311 G379 - physical postflight, 2026-10-16

## Installed

- TWRP 3.1.1-0 installed G379 guarded signed full OTA, updater exit 0.
- New BOOT prefix (6,838,272 bytes): `cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd`.
- Android 11 finished boot (`sys.boot_completed=1`), global SELinux `Enforcing`.
- Initially `audioserver` repeatedly crashed in `libaudiohal.so`, and `system_server` blocked waiting for `media.audio_policy`.
- Live VINTF manifest addition: `/vendor/etc/vintf/manifest/android.hardware.audio@6.0.xml`, SHA-256 `a012aae9a74f4f5dfb24afa7fa30f4e5d526d0b4f6de027186b9cc38e97d8ced`. Afterwards `audioserver`, audio HIDL 6.0 and `media.audio_policy` remained registered; Android completed boot.
- Plus SIM `LOADED`, UMTS, `mDataConnectionState=2`; vendor RIL SHA-256 `719750b0f544238c0986ecbbc8c7d4b59b822f00c7ba1c883809b1b54fcee491` unchanged. No PIN/PUK or call/SMS test.
- GNSS HIDL service, isolated legacy launcher, bridge and `gpsd` processes running. Physical jail's `/proc/896/mountinfo` showed Binder mount source `/lt013g-gps` at `/dev/binderfs/binder`; Android main binderfs remains mounted separately. This establishes separate node mapping, not a verified satellite fix.

## Integrated in sources for the next build

- `BoardConfig.mk` adds the already-tracked, known-good audio 6.0 VINTF fragment specifically for `lt013g`.
- `frameworks/av/media/libaudiohal/FactoryHalHidl.cpp` corrects a null-sentinel iteration check (`*version != nullptr`). The corrected binary is not installed yet.
- `tools/verify_ota_targetfiles.py` now refuses an OTA when either audio or audio.effect 6.0 default HIDL service is missing from the merged target-files vendor VINTF manifest.

## Still pending

- Coherent rebuild/integration of the corrected `libaudiohal.so` into the next full ROM; do not label the source-only fix as installed.
- Actual outdoor GNSS fix and strict SELinux vendor domain review.
- Hardware-dependent calls, SMS/MMS, Bluetooth, IR and deep-sleep validation.
- GitHub publishing: source repos currently have no configured remote; commits are local only.
