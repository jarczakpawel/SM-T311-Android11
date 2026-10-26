# G388 SM-T311 Android11 recovery BCB: missing /misc in actual runtime vendor fstab

Date 2026-10-17. GPS remains FIRST. G387 full cumulative firmware with XMM6262 PDP /32 default next-hop correction is signed and STAGED only, so recovering TWRP access is blocking.

## Real factory and physical evidence
- Stock Samsung PIT OTA p11 physically exists as /dev/block/platform/soc/12550000.mmc/by-name/OTA -> /dev/block/mmcblk0p11, 16,384 sectors (8 MiB). Original stock recovery fstab exposes OTA as /misc, and current ramdisk/fstab.lt013g already had the same OTA /misc mapping.
- Real G379 Android11 /vendor/etc/fstab.lt013g exists but has ZERO entries for /misc. Actual source device/samsung/lt01-common/rootdir/fstab.lt013g.vendor had only EFS, CACHE and USERDATA.
- In AOSP Android11 system/core/fs_mgr/fs_mgr_fstab.cpp GetFstabPath chooses /odm/etc then /vendor/etc then / for hardware lt013g. ReadDefaultFstab normal boot selects /vendor/etc/fstab.lt013g rather than the correct but unused RAMDISK fstab.
- bootable/recovery/bootloader_message/bootloader_message.cpp get_misc_blk_device uses ReadDefaultFstab and searches mount_point /misc. Without it, returns failed to find /misc partition. system/core/init/reboot.cpp HandlePowerctlMessage reboot,recovery calls write_bootloader_message on this path; **returns BEFORE kernel reboot if BCB write fails**. AOSP PowerManagerService lowLevelReboot returns after a 20-second timeout, so ShutdownThread's historical Reboot failed, will attempt shutdown instead log can be explained by this userspace failure. This is direct configuration proof, not conclusive historical kernel trace.
- G379 pstore console-ramoops-0 captures a different successful Linux kernel Samsung SEC restart path cmd=shell, INFORM2=12345678 INFORM3=12345670. Previously attempted reboot recovery physically powered the tablet off (NO flash). Do not repeat blindly.
- system/core/fs_mgr/fs_mgr.cpp fs_mgr_mount_all deliberately skips raw type emmc entries. Mapping /misc emmc causes NO auto-mount, formatting, EFS/CP writes, radio reset or reboot.

## Scoped source correction
- device/samsung/lt01-common source commit 26ce6e0 appends exactly one factory OTA p11 /misc emmc defaults defaults entry to **the vendor fstab Android actually chooses**. Existing EFS/CACHE/USERDATA mappings byte unchanged.
- device/samsung/lt013g/tools/test_g388_recovery_bcb_fstab.py tests AOSP selection, real BCB code, factory OTA, skipped raw eMMC and 10/10 negative/fail-closed broken fstab mutants.
- stage_g388_recovery_bcb_target_files.py requires new vendor fstab exact-match, prior G387 BOOT/TWRP/RIL/audio/GNSS Binder CIL/compiled telephony ART unchanged and diagnostic apps absent. Re-runs G387 exact Java extracted 10 cases and 4 negative mutations plus previous release gates.
- Source patch and test compilation do not install Android G388. This is a recovery-installation catch-22: to get this corrected /misc on the tablet, owner may still need to enter currently installed TWRP manually (Volume Up + Home + Power). Avoid blind Android restart, /dev/block/mmcblk0p11 writes and unsafe partial SYSTEM swaps.

## Physical after new coherent ROM installation (PENDING)
- Recheck active vendor FSTAB has exactly OTA /misc, then reboot to recovery under controlled fallback. Verify physical network route via /32 gateway, validation, NTP/SUPL, GNSS time and eventually outdoor GPS3D position fix.
- Preserve G379 physical Android, the signed staged G387 until a strictly compatible cumulative G388 is signed and physically staged/readback hash-verified. Do not claim physical boot/recovery/internet/GPS fixed yet.


## Completed cumulative full Android11 release (signed/staged only)

- Cumulative exact G388 with G387 source route and new vendor /misc built with m -j3 target-files-package EXIT0, using unchanged isolated MKE2FS_CONFIG to satisfy AOSP1.45.4. Target-files staged SHA256 36f5d620a6c572b5d07cc320764db2ac150e67a46f4a9265523a04364c4a2928 (1,149,694,339 bytes), strict G382/G387 immutable BOOT, RECOVERY, factory RIL, libaudiohal, GPS Binder SELinux and telephony ART/OAT/VDEX gate PASS.
- Original BCB absent G387 vs new G388 vendor/etc/fstab.lt013g has exactly one OTA p11 /misc raw eMMC entry, old EFS/CACHE/DATA rows byte-preserved. Source 10/10 bad-config fixtures rejected; older exact extracted 10+4 Java route tests PASS.
- Full non-A/B developer TESTKEY signed generator EXIT0 and guarded OTA re-signer EXIT0. Final candidate SM-T311-G388-GUARDED-DATA-ROUTE-RECOVERY-BCB-SIGNED.zip is 484,320,573 bytes, SHA256 a6ded4e74b58792a906c2473b1081707b72e0fe603d4921e529d1e4e4fe30cc6. BOOT SHA cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd, TWRP unchanged f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f, OEM libril sha719750b0f544238c0986ecbbc8c7d4b59b822f00c7ba1c883809b1b54fcee491. No physical write/wipe/flash.
- Independent system block-by-block verification 274149 new + 6828 zero = exactly 1,122,914,304 bytes matched G388 full target system image, no recovery write. Signed ZIP guarded pre/post BOOT, no /data/CP/EFS.
- Physically pushed G388 OTA to /data/local/tmp/SM-T311-G388-GUARDED-DATA-ROUTE-RECOVERY-BCB-SIGNED.zip, exact SHA256 on real tablet PASS, chmod0600 root:root; only OLD superseded G387 on-device signed ZIP removed after BOTH hash checks. All older signed ROM packages still on Debian. G379 installed boot1 Enforcing Plus SIM LOADED gps service running idle power0; G388 NOT INSTALLED.
