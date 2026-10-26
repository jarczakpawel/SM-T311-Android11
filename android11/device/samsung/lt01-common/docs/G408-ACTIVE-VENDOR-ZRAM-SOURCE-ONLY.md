# G408 - active Android11 vendor fstab ZRAM restore, SOURCE ONLY (2026-10-17)

## REAL READ-ONLY G379 FINDING - P1-15
- SM-T311 physically booted Android11, SELinux Enforcing, GPS service untouched.
- /proc/meminfo: MemTotal=1485356kB, MemAvailable=906308kB, SwapTotal=0kB, SwapFree=0kB, CMA=131072kB; /sys/block/zram0/disksize=0 and mm_stat all zeros, /proc/swaps empty. Linux6.12 MGLRU enabled=0x0001; no OOM (oom_kill=0), current PSI avg10/60/300=0.
- Kernel zram device /dev/block/zram0 exists, kernel defaults [lz4] (tested by READ-ONLY device control). Android mkswap/swapon executables exist. NO writing to zram, no swap activation was attempted.
- Physically running Android G379 has /vendor/etc/fstab.lt013g with EFS/CACHE/DATA only, no swap; /vendor/etc/init/hw/init.lt013g.rc with mount_all but no swapon_all. Original old root ramdisk files /fstab.lt013g and /init.lt013g.rc do NOT exist on live Android. Source tree old ramdisk/fstab has zram50% but is NOT active. Real second-stage vendor fstab is selected.
- G403 full signed OTA remains staged, but NOT installed; source G403 vendor fstab also has no zram. Its /misc p11 correction is preserved.

## CORRECTION - exact active files, not old ramdisk
1. rootdir/fstab.lt013g.vendor: add single /dev/block/zram0 none swap defaults zramsize=805306368,swapprio=32758,wait (768MiB). Keep original EFS/CACHE/USERDATA and selected /misc p11 byte-unchanged. Do not use eMMC swap file/partition.
2. rootdir/init.lt013g.vendor.rc: on fs after mount_all actual vendor fstab, run swapon_all /vendor/etc/fstab.lt013g; on boot write swappiness100 and page-cluster0 (existing source old ramdisk intent, not active at runtime).
3. Avoid max_comp_streams because the 6.12 kernel determines concurrency; default lz4 already selected, no compressor sysfs write needed.
4. AOSP Android11 system/core/init do_swapon_all reads the supplied fstab and calls fs_mgr_swapon_all; fs_mgr recognizes swap, zramsize= and swapprio=, initializes /dev/block/zram0 with /system/bin/mkswap and swapon syscall.
5. Source-only test tools/test_g408_active_vendor_zram.py PASS for exact source and 11 safety mutant rejections; old HEAD G403 fstab/init each FAIL expected. No full ROM build/flash and no unverified performance/boot claims. Ownership of source unchanged.

## DEPLOYMENT SAFETY
- This commit is G408 source-only. It DOES NOT repair live G379 or previously signed G403. Do not call G403 "ZRAM fixed".
- Before real delivery, perform FULL coherent Android11 ARM32 target-files build, signing, independent signed SYSTEM parity, protect BOOT/TWRP/RIL/audio/SELinux and new vendor fstab/init only; no partial /vendor write on mounted Android. Build new specifically pinned recovery readonly gate and postinstall read-only parity audit, stage with SHA verified. Installer consent still separately required and actual TWRP hardware boot still owner-dependent.
- Running G379 has no observed immediate memory-pressure/OOM; zram improvement expectation remains unvalidated until actual subsequent firmware boots. Do not infer benchmark speedup from its absence alone.

## G408 SELinux follow-up - physically confirmed after initial source change
- Live G379 `ls -Z /dev/block/zram0` gives `u:object_r:ram_device:s0`; actual sysfs disksize is `sysfs_zram`. Stock platform Android 11 `init.te` and `toolbox.te` only allowed `swap_block_device`, not `ram_device` for `mkswap`/`swapon_all`; the previous vendor fstab/init fix alone was incomplete for SELinux Enforcing.
- Minimal device platform-private policy `sepolicy/private/lt013g_g408_zram.te` (Git eff5cef) grants `init` and `toolbox` exactly `ram_device:blk_file rw_file_perms`. No global `block_device`, no labeling changes, no permissive mode. Source negative mutant tests 6/6 reject missing/unsafe policy.
- Android 11 compiles the board's `BOARD_PLAT_PRIVATE_SEPOLICY_DIR` into `SYSTEM/system_ext/etc/selinux/system_ext_sepolicy.cil`: byte-compared new compiled CIL versus archived G403 has exactly two new allow lines, zero removed; `plat_sepolicy.cil` remains byte-identical. The compiled preloaded policy and system_ext mapping hashes change as expected. Strict candidate G408 QA in lt013g Git e524579/4902d25 enforces this precise source-to-target-files delta.
- Old wrong-variant ENG build715e4798fc1c and incomplete userdebug build b89ce820d8d2 stopped; corrected complete G408 userdebug build job 6890ef895771 includes both vendor setup and required SELinux. No signed/physically installed G408 yet. The existing G403 signed OTA does not contain active ZRAM or this policy change.

## Vendor init swappiness restriction - corrected before final G408 release (2026-10-17)
- Physically read-only observed Android G379 /proc/sys/vm/swappiness=60, its SELinux label u:object_r:proc:s0; /proc/sys/vm/page-cluster=3, label u:object_r:proc_page_cluster:s0.
- AOSP system/core/init/builtins.cpp dispatch map has swapon_all {false,...} executing as init, but write {true,...} from the device active vendor rc executes under vendor_init subcontext. Standard Android11 vendor_init.te explicitly permits page-cluster write but NOT generic proc sysctl write.
- To avoid blind SELinux access expansion, G408 vendor init now sets ONLY page-cluster=0; leaves safe stock swappiness=60. Later physical memory pressure/UX measurement required before tailored tuning. ZRAM 768 MiB LZ4 and exactly two init/toolbox ram_device SELinux permissions remain unchanged.
- Prior attempt build job6890ef895771 halted at 33% before packaging while this was corrected; no ROM installed. New consistent userdebug build required.
