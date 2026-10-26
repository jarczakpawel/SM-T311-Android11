# SM-T311 G317 combined TWRP installer

The G316 update has two inseparable parts: a Linux 6.12 BOOT with the duplicate `androidboot.selinux=permissive` kernel argument removed, and six Android 11 GNSS/SELinux files including the GPS launcher's new `namespace mnt` init service.

`build_g317_combined_twrp_ota.py` packages the six-file signed G316 source update plus its matching BOOT into a separate signed **G317 combined ZIP**. The script requires the G301 physical BOOT partition SHA256 `c0370d97d94c2dcedd5f5405ccda2dcf63afa4e2d30f9c07e1be577591e693ed`, checks six original SYSTEM SHA256 values, writes the updated SYSTEM files, verifies them, and only then writes BOOT. It verifies the new BOOT prefix SHA256. It never intentionally modifies RECOVERY, DATA or EFS, and never auto-reboots.

The installed TWRP recovery's **physical partition SHA matches its source image**. TWRP's fstab calls BOOT `/dev/block/platform/dw_mmc/by-name/BOOT` while Android 11 calls it `/dev/block/by-name/BOOT`; both aliases are supported when they resolve to the verified `/dev/block/mmcblk0p9` device. TWRP ramdisk includes BusyBox v1.22.1 and SHA256 tools. The actual installed G301 BOOT ramdisk SHA256 matches the G316 build's ramdisk SHA256 `681d59437776b09ede48dc57e16b34697d08b7161f820673b8c84df3943bd8fd`.

This is an **offline-built, uninstalled** update. CRC, signing and SHA checks are not a substitute for physical TWRP installation testing or a recoverable operator session. G317 is the combined successor of the G316 six-file-only ZIP and separate BOOT, not an additional required step to be flashed after those.

## G318 hardening

The updated signed G318 package is independent of the earlier G317 ZIP. Its BOOT write phase also validates the six **already updated** SYSTEM SHA256 values, independently of the updater-script orchestration. The actual packaged shell passes the isolated host tests covering old/partial/corrupt states, premature BOOT rejection and idempotent reinstall. This is not physical TWRP execution or power-loss atomicity.
