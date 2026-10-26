# SM-T311 G1154 — source-level release checklist (2026-10-25)

**This checklist is an enforceable project contract, not a claim that every hardware flow passed.** Hardware: Samsung Galaxy Tab 3 8.0 LTE SM-T311, Exynos4412, ARMv7, ~1.5 GB RAM, Mali400, BCM4752 GNSS, XMM6262 3G modem, Android 11 / Lineage 18.1, pinned Linux 7.3.0-pjarczak G1130 BOOT.

## Source/installed regression barriers

1. Keep exact accepted physical G1130 BOOT and TWRP. Don't flash experimental G1131 CPU policy QoS or replace GPU GNSS modem binaries speculatively. Preserve source commit history for later user-owned GitHub publish.
2. Protect user-confirmed working **GPS, OTG, phone calls, SMS**. No SIM PIN/PUK scripts or automatic modem unlock. Running GNSS bridge, RIL, Codec2 HAL and USB ADB are smoke evidence only, not complete voice/outdoor GNSS tests.
3. G1153 physical H264 playback regression: app-owned Codec2 `memfd:MessageQueue` denied to hardware `mediacodec`. Narrow source `device/samsung/lt01-common/sepolicy/vendor/lt013g_media_memfd_interop.te` must contain exactly `allow mediacodec appdomain_tmpfs:file { getattr map read write };`. API30 vendor CIL contains `(allow mediacodec appdomain_tmpfs_30_0 (file (read write getattr map)))`. Old precompiled policy SHA83f... and vendor CIL445... are **NOT** releasable. New exact compiled SHA c56ffc6db7fc2fed5db8e1778ea0d8d7ae45f8214769e518c27e657d7a0a7bb9 and CIL SHA4cb0699a05f69af08a528680077c026a4e9a1e294f95281e79833afaaa8ad71e must be present in fresh actual SYSTEM and target-files.
4. Audio: hardware audio HAL and audio-RIL bridge physically SHA-pinned; accepted 48kHz `AudioFlinger` and `/vendor/etc/audio_policy_configuration.xml` SHA df5800...; no audible automatic QA. Camera2 G1142 CIL property policy PIN, screenrecord G1099 SHA and USB ADB/OTG init pins.
5. New G1154: all 4 accepted `/vendor/etc/media_codecs.xml`, `media_codecs_c2.xml`, `media_codecs_samsung_video.xml`, `audio_policy_configuration.xml` hashes recorded directly from **physical SM-T311** and identical source-staged bytes. `linux-7.3-port/tools/g1139-verify-android-system-ext4-image.py` and `g1143-verify-target-files-zip-actual-artifacts.py` require all 4 hash matches. Ephemeral mock ZIP 1 accepted synthetic + 25 deliberately invalid variants PASS, including changed Codec2 XML and audio config. **Synthetics are not OTA certification**.
6. Silent live verification: `/system/bin/sh /data/local/tmp/G1154-silent-functional-matrix.sh` after copying `tools/g1154-silent-runtime-functional-matrix.sh` to that path. Physical result G1154: **34 PASS, 0 FAIL, 5 NEEDS_TEST**. Never initiate video/music/microphone or camera automatically; never trust a mere absent AVC without full playback attempt.
7. Android11 end-of-security-support risk: physical `ro.build.version.security_patch=2024-02-05`. The Google October 2026 Android Security Bulletin lists affected/current patches primarily for Android14+; do NOT falsely bump patch date on the unsupported Android11 branch. Check per-CVE codepath and ABI, backport only proven-compatible fixes and test individually.
8. Idle performance measured read-only: `schedutil` CPU governor (200 MHz minimum, 1400 MHz maximum), MemTotal 1,483,324 KiB, MemAvailable ~872,436 KiB, **real active ZRAM swap 786,428 KiB** (all free), low load ~0.9, AudioFlinger 48 kHz. `/dev/block/zram0` exists and swap is not hypothetical. Don't force performance governor, overclock or increase zRAM without quantified performance and thermal/codec regression runs. Don't disable GPS/modem checks or kernel reclaim.
9. Kernel mainline **7.3** dated 2026-10-18: upstream no automatic new-v-number speed fix; ensure actual Exynos4412/ARMv7 benefit and source compatibility before pulling candidate patches.
10. Possible optional bloat (print recommendation, wallpapers, calendar, contact backup) is NOT proof unsupported; do not remove package/provider dependencies blindly. Measure idle PSS and service starts, make source-only scoped changes with tests, keep telephony, USB, sensors, codec drivers and SELinux minimum required interfaces.

## Pending precise qualifications

| Priority | Required true end-to-end experiment | Condition to close |
|---|---|---|
| Critical | Original CDA HLS/AVC 1280x720 video played from same source | real decoded frames, no `Codec2 registerSender4`, no `queueCSDInputBuffer14`, no MediaCodec error0xe |
| Critical | Full Android11 release build | native job actually SUCCEEDED, NEW G1153 host policy and vendor CIL, G1137 G1139 G1143 real release gates PASS and 100% ZIP CRC |
| High | Long hardware H264 encoding, rear/front recording audio and screenrecord | repeated long clips with verified FFmpeg integrity, rotation correct, codec compatibility |
| High | Post-load SELinux AVC sweep | collect audits after actual media, camera, GNSS/OTG and telephony flows, add ONLY causally justified grants |
| High | 3G cellular data validation failures | network data test, distinguish APN/no-data from internet captive validation, do not compromise working calls/SMS |
| Medium | GNSS outdoors and OTG/USB role stress | user-confirmed working GPS/OTG already; stress only with safe user expectations |
| Medium | Performance and footprint changes | quantify PSS/battery/latency before/after, no vendor/GPU/Codec2 regression |
| Security | Android11 per-CVE backport and Android bulletins | source-applied, applicable patches and reproducible test evidence before any claimed patch level |

## Build / publishing
AOSP native full target-files job ID `6974d254eda6` was still RUNNING at last audit. **Do not launch a simultaneous build or touch in-flight outputs**. After stopped, run G1148 single finalizer; it excludes diagnostic GNSS/Camera2 APKs, rebuilds a coherent sparse SYSTEM ext4, tests byte-for-byte SHA, verifies source ancestor and ZIP member hashes and refuses stale policy. No full ROM flash until qualification. Commit every required source edit locally; user handles eventual GitHub push.

Documentation: `/home/debian/Projekty/samsung/linux-7.3-port/G1154_MASTER_COMPONENT_QA_RELEASE_AND_PERFORMANCE_CONTRACT_20261025.md` and handoff /CURRENT. External factual references kernel.org, source.android.com/docs/security/bulletin/2026/2026-10-01, source.android.com/docs/security/features/selinux/validate.


## G1154 security clarification: not every process is truly Enforcing
Source immutable `device/samsung/lt01-common/sepolicy/vendor/lt013g_g342_rild_compat.te` has `permissive rild;` and `lt013g_g341_enforcing_hardware.te` has `permissive lt013g_gps_legacy;`. Thus the modem/GNSS domains accept denied access (observed `permissive=1`) although the global Android device says Enforcing. Other known permissive domains in the compiled binary: `su`, `backuptool`. Production Linux QA `g1154-audit-selinux-permissive-domain-debt.py` inventories exactly these four and must print **NEEDS_SECURITY_REVIEW**, not 'secure release PASS'. Never force the already-working GPS/voice/SMS domains enforcing without proven binder and uevent device-node source changes and physical tests.

Postboot G1154 full logcat sweep last 10k records found 47 AVC: shell probes of protected interfaces (expected/should not grant), denied vendor init printk, scheduler BFQ attr and capability perfmon, storaged rev, RIL legacy CBD /dev node operations (permissive), GNSS private binder context manager (permissive). Local Git `76f67aa` drops one ineffective vendor init /proc/sys/kernel/printk write without changing USB ConfigFS logic; effective only in a future qualified ROM. Reference `tools/g1154-silent-selinux-avc-sweep.sh` (Git `324a5ce`) for read-only aggregates after actual phone/GPS/H264/camera workflows; it does not print private filenames or grant permissions. No global permissive toggle.
