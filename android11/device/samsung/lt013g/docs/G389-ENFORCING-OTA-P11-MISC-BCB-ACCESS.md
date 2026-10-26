# G389 SM-T311 - Android11 enforcing BCB OTA p11 label

## Physical G379 (read-only)
SM-T311 stock PIT OTA p11 is mmcblk0p11; 8MiB, existing /dev/block/platform/soc/12550000.mmc/by-name/OTA points to it.
SELinux global Enforcing and the actual target node is labeled u:object_r:block_device:s0.
G388 source fixes the absent /misc entry in the actual selected /vendor/etc/fstab.lt013g, but this is not yet installed.
G379 previous reboot recovery request timed out after 20sec and shut down, historical init failure detail unavailable; a different normal reboot actually reached kernel Samsung SEC INFORM3.

## Precise AOSP policy source
AOSP system/sepolicy/public/init.te grants init dev_type:blk_file r_file_perms, and only misc_block_device:blk_file w_file_perms for raw bootloader-message writes; no general init block_device write. w_file_perms includes open+write+append+lock+map.
AOSP bootloader_message.cpp reads BCB O_RDONLY then writes p11 O_WRONLY and fsync. Android init reboot.cpp requires a successful BCB write before issuing recovery restart.
As a result, merely adding vendor FSTAB /misc without labeling OTA correctly would still risk SELinux write denial in Enforcing.

## Scoped source repair
Common Git commit 016be53: exact /dev/block/mmcblk0p11 u:object_r:misc_block_device:s0 in source vendor/file_contexts. Other sensitive nodes EFS p3, radio p13, CACHE p19, USERDATA p21 unaffected. NO general raw block-device write grants, global permissive, EFS/CP/NV changes, BCB partition raw reads or writes.
Device source test test_g389_bcb_enforcing_selinux.py uses genuine AOSP SELinux files, macro, bootloader_message and G388 fstab: ten bad negative fixtures fail as required. Device source compile/release gate stage_g389_enforcing_bcb_target_files.py checks only one new compiled vendor file_contexts mapping and byte-identical preexisting contexts and G388 BOOT/TWRP/stock RIL/audio/GPS Binder policy/ART/G387 Java host route and vendor FSTAB.
G389 complete ARM32 target-files actual build passed, including vendor_file_contexts_test and sepolicy_tests. Device code commit cee25cb.

## Deployment remains blocked
G388 TESTKEY OTA currently physically staged but DOES NOT include this p11 label and must not be mistaken for a complete recovery fix. G389 cumulative signed OTA needs own full integrity check and physical SHA staging, without reboot/flash. Existing G379 cannot gain the correction until safe hardware-key TWRP entry and controlled OTA install. After owner-controlled install verify /misc vendor fstab, p11 misc_block_device live label, then recovery restart under physical fallback and finally mobile default route, NTP/SUPL/GPS actual stable 3D fix.
No plugin reinstalls, no user phone calls/SMS, SIM PIN/PUK, raw OTA BCB writes, EFS/CP, auto modem resets or publicly uploading private OEM files. Keep exact 20 GPS-first task IDs and full private indexed conversation history.
