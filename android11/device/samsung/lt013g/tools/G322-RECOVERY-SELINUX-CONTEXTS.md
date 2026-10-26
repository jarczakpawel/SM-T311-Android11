# SM-T311 G322 — preserving SELinux file labels in the signed recovery update

G301 live Android 11 assigns these actual inode SELinux labels, confirmed read-only by `ls -Z`:

| Destination inside TWRP SYSTEM mount | Current live label |
|---|---|
| `/system/sepolicy` | `u:object_r:sepolicy_file:s0` |
| `/system/system/vendor/etc/selinux/precompiled_sepolicy` | `u:object_r:vendor_configs_file:s0` |
| `/system/system/vendor/etc/selinux/vendor_sepolicy.cil` | `u:object_r:vendor_configs_file:s0` |
| `/system/system/vendor/bin/lt013g-gps-legacy-launcher-v195` | `u:object_r:lt013g_gps_legacy_exec:s0` |
| `/system/system/vendor/lib/hw/gps.default.so` | `u:object_r:vendor_file:s0` |
| `/system/system/vendor/etc/init/init.lt013g.gps-legacy.rc` | `u:object_r:vendor_configs_file:s0` |

The exact **physical** installed TWRP's decompressed ramdisk includes a SELinux policy v30. Its SHA256 is `3a91bab38aafbcb058c203e5fb0d206e909365177415a6ed303d043d87aad08f`; the legacy recovery policy predates the `lt013g_gps_legacy_exec` vendor label and does not define it. Directly invoking `set_metadata(...,"selabel","u:object_r:lt013g_gps_legacy_exec:s0")` under that recovery may therefore fail even while Android 11 accepts the same label.

The G322 updater deliberately leaves `security.selinux` xattrs intact. Its preflight verifies **all six paths already exist** at their G301 hashes. The source of Android 11 `bootable/recovery/updater/install.cpp::PackageExtractFileFn` opens destination with `O_WRONLY | O_CREAT | O_TRUNC` rather than unlink/recreate: writing to the pre-existing inode preserves its SELinux xattr. `set_metadata` sets owner/group/mode only; the G322 signed ZIP asserts no `selabel` assignments are present, and records actual labels in the build manifest for after-install diagnostics. No RECOVERY/BOOT policy changes required to relabel vendor files from the older recovery domain. This depends on update-binary matching the Android updater extraction behavior, and **real TWRP execution still needs verification**.

G322 uses the *same* G321 BOOT and six GNSS/SELinux binaries. Only the signed recovery packaging audit/metadata handling changed. Host script failure tests pass (valid source, damaged source, premature BOOT write blocked, idempotent retry and target corruption detection). Android 11 physical Enforcing boot has **not** been tested.
