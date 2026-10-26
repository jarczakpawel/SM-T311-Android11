# G403 full signed cumulative ROM physically staged, NOT installed (2026-10-17)

Real tablet remains G379 Android11 Linux6.12 boot1 SELinux Enforcing Plus SIM LOADED UMTS; native Broadcom gpsd running, GPS power0 idle. New signed G403 developer TESTKEY complete Android11 OTA is PHYSICALLY STAGED, not installed or booted.

New G403 confirmed actual G401 native C defect: old pending CP SUPL CLOSE worker sent one command only; a transient -EIO left pending_close true and worker gone until another real event. Exact original G401 C source compiled and synthetic one-error failure reproduced. G403 source fix v398_local_close_worker retries max 3 CLOSED sends with max two 100ms backoffs under existing serialized CP link mutex. Clear pending on first success, preserve pending if all fail, skip obsolete/local-owner work; no infinite loops, no new process or modem reset, no APN/SIM/CP/EFS changes. Eight actual C regression scenarios PASS and G330-G333, G396 noIP callbacks10+10, G397 highFD ASan, G400 old CP orphan9, G401 worker fallback8 PASS. Source Git 3f54f22.

Full ARM32 target-files package built PASS. Coherent protected G382 legacy Binder/audio, G387 modem PDP /32 Java route, G388 original vendor /misc, G389 enforcing p11 SELinux, G395 UTC/netns, G396 noIP SUPL, G397 safe poll, G400 CP orphan and G401 fallback fixes intact. New GPS ELF32 ARM SHA256 8cba224bb2f77a4d829b6ff7a4f1da1461357b0cad3eba9d704ae7e56e88db5b. Full cumulative targetfiles size 1,149,695,333B SHA256 bcf43207a6a4c6db71fcf0ae099ecc38c0ff9dd539d144e695843d0848247a74, only four contents changed versus approved G401: modern gps.default.so and IMAGES/system.img, cache.img, userdata.img. Stock BOOT, TWRP, RIL, audio, compiled ART and SELinux, vendor /misc FSTAB identical; 9 source-gate unsafe mutations rejected.

AOSP signed non-A/B developer TESTKEY full OTA G403 size 484,326,152B SHA256 b6df8dcab3af2c2afd41271f607f90ed511ce6df342fff172cfa6794b355a1b, host artifacts/SM-T311-G403-BOUNDED-CP-CLOSE-20261017/SM-T311-G403-GUARDED-GPS-SUPL-CP-RETRY-SIGNED.zip. G401/G403 signed update-binary, updater-script, pre/post factory BOOT guard, system.transfer.list, BOOT.img byte-identical; only compressed SYSTEM payload changed. Independent exact signed SYSTEM stream 274149 new blocks *4096 =1,122,914,304B compared byte for byte with G403 target image PASS, 6828 zero blocks, p20 physical partition545792 blocks, recovery untouched. Factory BOOT SHA cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd, physically original TWRP SHA f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f.

G403-only original old real TWRP3.1.1 read-only safety gate tools/g403_twrp_readonly_preflight.sh 3029B SHA256 6459d29d3d6a6435e89fd47f361a690be2a459183db25727bd7a777c5cad73fb. Actual physically hashed old TWRP ramdisk original BusyBox, p9 BOOT prefix, old dw_mmc p9/p20 by-name aliases, unmounted SYSTEM and exact signed G403 ZIP SHA pinned. Offline one good + 8 fail-closed corrupted/missing condition cases PASS; script NOT RUN in physical TWRP.

G403 only after-install READ-ONLY audit source tools/g403_postinstall_readonly_acceptance.sh 4213B SHA256 42496b796bf4f23568b2bbd3d7d3ea1c4eccab2aa8d5ee5d4579b4be9d3278b2, pins exact installed G403 GPS ELF, selected /misc and p11 label, Android11 boot/global Enforcing and passive cellular IPv4/32 gateway host route, symbolic PASS/FAIL/PENDING without IP/APN/SIM/GPS coords. Synthetic healthy one positive+7 hard rejects+3 conditional owner-pending PASS. Old G402-G401 auditor is NOT valid for newer G403 GPS ELF.

REAL physical tablet ADB root0600 exact whole SHA PASS for:
- /data/local/tmp/SM-T311-G403-GUARDED-GPS-SUPL-CP-RETRY-SIGNED.zip, size484326152
- /data/local/tmp/SM-T311-G403-TWRP-READONLY-PREFLIGHT.sh, size3029
- /data/local/tmp/SM-T311-G403-POSTINSTALL-READONLY.sh, size4213
After all G403 ZIP/guard/postinstall hashes and previous G401 signed ZIP hash confirmed, removed ONLY superseded G401 signed ZIP from tablet, older signed host G401/G400/G397 retained. FULL historical PRIVATE G389 conversations+sources archive remains tablet root0600 size195942769B unchanged, newer PRIVATE G390 archive814 entries HOST ONLY after connector safety block, no bypass or working ADB plugin reinstall. Tablet /data ~92% used with about 1,064,648KiB free. NO reboot/flash, no live modem/GPS settings changed.

OWNER ONLY NEXT: use hardware VolUp+Home+Power to boot actual old TWRP 3.1.1; never blind adb reboot recovery (G379 previously turned off). Inside actual TWRP with /data accessible run ONLY: /sbin/sh /data/local/tmp/SM-T311-G403-TWRP-READONLY-PREFLIGHT-V4.sh. Stop if any check fails, do not copy 484MB into recovery tmpfs RAM. Then SEPARATE owner approval for FULL G403 developer TESTKEY install. After actual Android11 boots run /system/bin/sh /data/local/tmp/SM-T311-G403-POSTINSTALL-READONLY.sh --read-only, and independently confirm real cellular Network VALIDATED/DNS/NTP/SUPL/native UTC and stable outdoor GPS/GLONASS3D without personal coordinate recording. Original20 immutable GPS-first task IDs preserved, proprietary OEM licensing/GitHub LAST.


## G404 preflight mount-identity hardening (same G403 ROM)
- G403 original 3029B gate is retained unmodified for historical hash parity, but its mount check inspected only /system and /system_root paths.
- G403 V2 3353B SHA256 0e38ceb0082371392c3c7b80243ee080438db8261d35cb2b499cb74d216a96bd additionally uses /sys/class/block/mmcblk0p20/dev (expected physical 179:20) and /proc/self/mountinfo major:minor to reject SYSTEM mounted at ANY path, even a different alias. If identity files are unavailable, FAIL-CLOSED; no mount, reboot or write.
- V2 was physically staged on Android at /data/local/tmp/SM-T311-G403-TWRP-READONLY-PREFLIGHT-V2.sh (root:root 0600) and host/device SHA256 matched. Host sh -n and three NEW targeted mount-identity cases PASS. V2 has NOT been run in real original TWRP; real /sys mountinfo availability in TWRP remains PENDING. Its failure must STOP the installation, not trigger a downgrade to V1.
- Signed G403 OTA ZIP and G403 postinstall script remain exact, unchanged and NOT installed. First manual TWRP by hardware keys, then execute only matching V2 for G403 and stop on any failure, then independent owner permission before full ZIP install.


## G405 TWRP fail-closed mountinfo parser V3 (G403 ROM unchanged)
- V2 detected SYSTEM at alternative mountpoints, but its `if awk ...; then BLOCK; fi` treated a failed /proc/self/mountinfo read/parser as if SYSTEM were unmounted.
- V3 pins the real SM-T311 SYSTEM device 179:20 and requires valid, nonempty /proc/self/mountinfo, distinguishing CLEAR/MOUNTED/INVALID. Any command, parser or identity error BLOCKS. The original signed G403 updater/BOOT/SYSTEM payload is unchanged.
- V3 3625B SHA256 048c4a3c3e1ec6909de8199cc5e3c62199a7a024c961bf42a1b8d66d5746b6ea, staged root:root0600 at /data/local/tmp/SM-T311-G403-TWRP-READONLY-PREFLIGHT-V3.sh and host/device SHA equal. New 8 focused BusyBox shell cases PASS; no recovery execution yet.
- V1 and V2 archived in original form. Do NOT downgrade from V3 upon FAILED safety gate; STOP instead. Only OWNER enters old TWRP via hardware buttons and runs V3; separate approval is required for full signed G403 install.


## G406 G403 TWRP preflight V4 strict mountinfo syntax
- V3 could classify malformed >=10-field mountinfo lines without the mandatory ` - ` separator as CLEAR (demonstrated with actual BusyBox awk).
- V4 validates mount ID and parent ID, block major:minor, absolute root and mountpoint paths, and exactly one separator at NF-3, while retaining optional mount propagation fields. Rejects empty, corrupt, or unexpected mount records. The block 179:20 must not be mounted anywhere.
- Original G403 BOOT/SYSTEM aliases, BOOT prefix, entire signed G403 ZIP SHA checks are preserved. No ROM recompile, no new OTA.
- V4 source/device size 4003 bytes SHA256 d7f1c8fd0e0c58d39d26797324a4f5a1e98ea06900bd91810d51b06b43c8dd2d, staged root:root 0600 at /data/local/tmp/SM-T311-G403-TWRP-READONLY-PREFLIGHT-V4.sh, host/device SHA match. New structural BusyBox awk cases 8/8 PASS and sh -n PASS; no actual TWRP execution.
- V1-V3 remain archived, not fallback routes. On any V4 FAIL stop before attempting install. Owner boots TWRP with buttons and approves full G403 installation separately.
