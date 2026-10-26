# SM-T311 G573 — fixed Android boot system/vendor fingerprint mismatch

**HOST VERIFIED; NOT INSTALLED.** G573 is the successor to G567. It fixes the user-observed Android 11 startup "internal problem" dialog arising from unequal `ro.system.build.fingerprint` and `ro.vendor.build.fingerprint` on a non-Treble device. Instead of suppressing ActivityTaskManager's safety warning, `BUILD_NUMBER=SMT311G573` was fixed for the release build and the generated SYSTEM and VENDOR build.prop files were regenerated together. Both now equal:

`samsung/lineage_lt013g/lt013g:11/RQ3A.211001.001/SMT311G573:userdebug/test-keys`.

The release validator `device/samsung/lt013g/tools/check_release_components.py` independently **rejects** old G567 with the mismatch; G573 **passes** this gate.

Canonical G573 ZIP (G567 old ZIP remains separate):
`/home/debian/android/lineage-18.1-sm-t311/out/target/product/lt013g/obj/PACKAGING/target_files_intermediates/lineage_lt013g-target_files-SMT311G573.zip`

ZIP SHA256: `86ab028d7486f83548bfa8be538ea24ec1851323c602ab7d940f9f9003226864`.
Full SYSTEM raw ext4 SHA256: `11c81fcf45dd86dacbdd85f521ccd6eb1cb8818c7c2bbdcee69bc0f113efa779`, bytes: `2235564032`.
Native static ARM32 gated TWRP helper SHA256: `ba964378166bb4263487f85da7ae86df1f597853edde27b892f98acd789c6ff0`.

Validation: `check_release_components.py --require-linux73-policy --require-t311-usb-reconnect` PASS, G554 exact binary parity PASS, ext4 read-only e2fsck PASS. Five gzip compressed raw chunks independently roundtrip SHA PASS, full raw SHA from concatenated chunks PASS, safe installer default DRY RUN PASS; eight simulated ADB tests PASS (seven fail-closed + resume). Only scripts and images on Debian; **no tablet write**.

## Installation safety

Scripts: `device/samsung/lt013g/tools/t311-io64-chunk/releases/G573/`.
Bundle: `/home/debian/Projekty/samsung/artifacts/SM-T311-G573-FIXED-FINGERPRINT-SYSTEM-20261020/`.
The plain executable G573 `stage_and_flash.py` defaults to read-only host dry run. Physical mode is intentionally gated by a real USB serial, an explicit `--flash-token I_AUTHORIZE_G573_RAW_SYSTEM_P20`, and original TWRP3.1.1-0 present. BOOT p9 and RECOVERY p10 SHA, p20 geometry/unmounted and writable /data p21 with enough space are checked before writing. Per 64MiB transferred piece SHA, combined gzip SHA, raw part SHA, native p20 per-chunk verify, final native full SYSTEM 64-bit SHA must match exactly. `--resume-from N` verifies earlier p20 chunks before continuing. **Never boot a half-written p20**. No auto-reboot, no CP/EFS/SIM, no user-data deletion/format.

Important operational issue: the Debian host workspace tool is isolated from physical host ADB devices; do not assume a flash script can use that sandbox's `adb` binary. Physically controlled TWRP installation requires a usable physical USB ADB terminal or equivalent verified connector path, plus operator at the tablet to recover if TWRP entry fails. The old `adb reboot recovery` had an intermittent black-screen regression, so don't try it unattended. Do not mount p20 while raw-writing it.

G573 includes host-built G567 DWC2 reconnect one-shot (hardware not yet verified), native encrypted Wi-Fi debugging fixed port 5555 with fallback and trusted-WLAN setting (not yet live), and precise SELinux 18 label updates. Golden OEM RIL, GPS legacy HAL and Mesa26 binaries still match earlier accepted parity. The G566 experimental audio V191 bounded-connect modification is source-only and NOT linked into the production golden HAL. GNSS fix on a desk indoors **is not an acceptance requirement**; measure daemon and optional satellite reception/NMEA without demanding a fix.

After an actual supervised G573 install and successful boot: (1) confirm boot dialog gone and fingerprint equality, (2) verify USB replug ADB+MTP, (3) confirm native TLS5555 on trusted Wi-Fi, (4) enforce SELinux path labels and services, (5) owner-led HANGUP and repeated voice audio acceptance, (6) indoor GPS transport/satellite checks and outdoor GNSS only when appropriate, (7) cameras/sensors/storage/power under controlled tests. Kernel BOOT remains final v7.3 physical G551 unless a separate guarded G552 patch update is intentionally performed.
