# G367 — Reproducible SystemUI ART integration into Android 11 target-files

**Status:** OFFLINE INTEGRATION PASS. **NOT FLASH READY.** All commands operate on Debian artifacts; no block-device, modem, PIN/PUK, SMS, or phone actions.

## Inputs locked to verified running SM-T311

- G366 built target-files SHA256 `22ebba02be81fb65edec48b0f68a3c82be89b8908d3cedd93aa1db22f1f40bb9`; this base is left intact.
- G365 booted on real tablet SystemUI APK `afc6f70e6be16a3f840dd47cfccb76ca9e649e2048a78baefcb595b15d433f95`.
- Isolated Android ART ARM32 ODEX `c438feb53cc3bd0482f293d14fe7dee1b0659bd52391f70179cba24265a9b50f` and VDEX `b7a082b9f901af3d7459d4ff56c371e1a30a8530904faa6a354128ff8effe102`, both generated G365 using Android's original dex2oat invocation and physically installed successfully.
- G333 BOOT: copied **only read** from original signed OTA for comparison, not injected in G367 target-files.

## Reproduce G367 offline

```bash
# In Android source checkout:
base=/home/debian/Projekty/samsung/artifacts/SM-T311-G365-SYSTEMUI-PAIR-20261016
out=/home/debian/Projekty/samsung/artifacts/SM-T311-G367-SYSTEMUI-RELEASE-INTEGRATION-20261016
g366=out/target/product/lt013g/obj/PACKAGING/target_files_intermediates/lineage_lt013g-target_files-eng.debian.zip

python3 device/samsung/lt013g/tools/g367_stage_systemui.py \
  --source "$g366" \
  --output "$out/G367-STAGED-NO-SYSTEM-IMAGE-target_files.zip" \
  --apk out/target/product/lt013g/system/system_ext/priv-app/SystemUI/SystemUI.apk \
  --odex "$base/SystemUI.odex" --vdex "$base/SystemUI.vdex"

# The stage deliberately drops IMAGES/system.img and system.map, because the
# previous system.img does NOT include the newly added ODEX/VDEX.
export MKE2FS_CONFIG="$PWD/device/samsung/lt013g/tools/mke2fs-android11-compat.conf"
export PATH="$PWD/out/host/linux-x86/bin:$PATH"
out/host/linux-x86/bin/add_img_to_target_files -a \
  "$out/G367-STAGED-NO-SYSTEM-IMAGE-target_files.zip"
mv "$out/G367-STAGED-NO-SYSTEM-IMAGE-target_files.zip" \
   "$out/G367-OFFLINE-NOT-FLASHABLE-target_files.zip"

python3 device/samsung/lt013g/tools/g367_verify_system_image.py \
  --zip "$out/G367-OFFLINE-NOT-FLASHABLE-target_files.zip" \
  --workdir "$out" --report "$out/g367-ext4-verification.json" \
  --simg2img out/host/linux-x86/bin/simg2img \
  --debugfs out/soong/host/linux-x86/bin/debugfs

python3 device/samsung/lt013g/tools/audit_target_files_vs_live.py \
  --zip "$out/G367-OFFLINE-NOT-FLASHABLE-target_files.zip" \
  --live /home/debian/Projekty/samsung/artifacts/SM-T311-G366-RELEASE-GATE-20261016/live_sha256_inventory.txt \
  --boot-expected 9487424414439edf4dd00bf4169bcd684572cc4abe6690af9589632205666436 \
  --policy-analyzer out/host/linux-x86/bin/sepolicy-analyze \
  --report "$out/g367-stage-audit.json"
# Strict audit EXIT 2/BLOCK is EXPECTED due remaining verified differences.

python3 device/samsung/lt013g/tools/g367_compare_boot.py \
  --g333 /home/debian/Projekty/samsung/artifacts/SM-T311-G333-ATOMIC-ENFORCING-OTA-20261016/SM-T311-G333-G301-TO-G333-BOOT-GNSS-ENFORCING-TWRP-SIGNED.zip \
  --g367 "$out/G367-OFFLINE-NOT-FLASHABLE-target_files.zip" \
  --report "$out/g367-boot-compare.json"
```

## Verified results

G367 ZIP SHA256 `afbe42d5ede917b346b0c813b80c2e559d58ac45c6b65175babb7b5d6af1e96c`. The Android image generator created a 1,119,474,076-byte Android sparse ext4 SYSTEM image (raw 2,235,564,032 bytes). Verified the *actual ext4* filesystem using `simg2img` and `debugfs`: APK, ODEX, VDEX have exact expected SHA256, owner 0:0, mode 0644, ext4 SELinux xattr `u:object_r:system_file:s0`. Temporary sparse/raw copies are removed by the verifier.

Strict release auditor: **44 matches, 1 mismatch, 0 missing**, matching SystemUI DEX CRC32 `12db1873`. Single SHA mismatch is `/vendor/bin/hw/rild` (all non-metadata code/data/relocations byte-identical, differences include build ID and compressed debug data). No rild binary replacement was done.

BOOT comparison: real proven G333 kernel size 6,049,465 bytes, SHA `d0071e73...`, versus current rebuilt G367 kernel size 6,402,153 bytes, SHA `e6fab59f...`. The 786,423-byte gzip ramdisk is **identical** SHA `681d5943...`. G333 cmdline has `buildvariant=userdebug`, new G367 `buildvariant=eng`. Kernel change is substantive, requiring separate code/functional evaluation, not silent "hash normalization" or G333 rollback.

SELinux currently has **4 permissive domains**: `su`, `rild`, `lt013g_gps_legacy`, `backuptool` even though global `getenforce` reads `Enforcing`.

**Do not flash this target-files ZIP.** Complete release readiness requires resolving kernel validation, full SELinux hardening (without modem/GNSS regressions), and disposition of rild metadata variation, then final signed OTA generation and physical regression acceptance. The user-owned Orange SIM remains PERM_DISABLED; do not send SIM secrets or radio resets. New Plus SIM tests require owner presence and explicit direction.
