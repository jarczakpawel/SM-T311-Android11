SM-T311 G696 — complete Android boot v0 RCU-OFF candidate, HOST ONLY
===================================================================

Physical baseline and known-good boot geometry
----------------------------------------------
Actual Android device is SM-T311 / lt013g, with physical stable
BOOT G650 and running `7.3.0-pjarczak`. The BOOT partition is
`/dev/block/platform/dw_mmc/by-name/BOOT -> /dev/block/mmcblk0p9`,
and RECOVERY is `/dev/block/mmcblk0p10`. The actual partition's
current SHA256 has NOT been reread by this stage.

The existing, previously built and historically physically accepted
G650 **HOST SOURCE** image is stored at:

  linux-7.3-port/artifacts/G650-SMT311-HOTMARK-DEFAULT-OFF-BYPASS/
    G650-HOT4K-RCU-BOOT-8MiB.img

This exact 8,388,608-byte file has pinned SHA256:

  2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3

It has Android boot v0 `ANDROID!` magic, 2048-byte pages,
kernel load addr 0x10008000, ramdisk load addr 0x12000000 and
tags addr 0x10000100. Its original ramdisk is exactly 786,423
bytes with SHA256:

  681d59437776b09ede48dc57e16b34697d08b7161f820673b8c84df3943bd8fd

The BASE image includes a final exact 86,489-byte board
`exynos4212-tab3-3g8.dtb` appended to kernel zImage, SHA256:

  6fb3e77075733d531abaa2f4c3fa1bf55c11a50f04cc5acc9838db5ea0f41bfb

The host verifier recomputes the ANDROID boot v0 SHA1 header ID
and checks that the baseline BOOT parser/repacker round-trips ALL
8MiB bytes exactly. This guards against simplistic bad boot image
repacking, but does NOT independently rehash the live p9 partition.

RCU-OFF kernel candidate
------------------------
The new kernel uses `../build/g687-release-rcu-off`, linked
ARM EABI5 GCC 16.2.0-3 and `7.3.0-pjarczak`. It has no
CONFIG_PAGE_RCU_FREE, no CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT,
and no CONFIG_DEBUG_FS. This removes the risky optional
reference-free read branch and G683 ext4 mmap GP overhead.

Host `zImage` SHA256:

  1c2dd2e521cbfec48c95c89518487b5cfd1887027e7f9d74146027d777e86c49

The board DTB is independently built from the release-OFF
kernel source using `make ... dtbs` and is BYTE-IDENTICAL
to the old G650 trailing DTB SHA above; includes `d00dfeed`
FDT magic and consistent FDT declared total size.

G696 now creates an actual full `boot.img` in the existing
project artifacts directory:

  artifacts/G696-RCU-OFF-HOST-ONLY/
    BOOT-G696-RCU-OFF-8MiB-HOST-ONLY-DO-NOT-FLASH.img

Its SHA256 is:

  ba9aed97fe7a028fc9ff5b540c916b251a7cd064a9fbf4055b5eb3f7af254204

Size exactly 8,388,608 bytes. The ramdisk bytes, page alignment,
all header bytes other than new kernel size and SHA1 ID, second
stage and DT-size fields and board DTB identity are preserved.

Reproduction
------------
The generator lives in the tracked project repository:

  python3 tools/g696_pack_release_off_boot.py
  python3 tools/g696_pack_release_off_boot.py --verify-only

It only reads existing host files and writes host artifact output,
does not use ADB, flash, recovery or create a BOOT backup. It
refuses an unpinned or changed BASE image/kernel/DTB, wrong
kernelrelease, unsupported partition geometry/size, missing
host configs, invalid SHA1 header, changed ramdisk, changed source
manifest, and overwrite of a distinct existing release candidate.

It has six deliberate corrupted input controls, covering magic,
page size, ramdisk bytes, oversize kernel, undersized kernel and
invalid header ID. Exactly six were rejected in actual G696 runs.

The kernel-tree independent consumer test is:

  python3 tools/testing/selftests/smt311/rcu_boot_host_preflight_g696.py

It checks 24 positive gates against actual G650 source bytes,
generated complete boot candidate, latest zImage, DTB, SHA256,
safety manifest and config-diff metadata. Nine tampered inputs
were rejected. The machine record is:

  Documentation/mm/sm-t311-rcu-offline-boot-g696.json

The project report `G696_RELEASE_OFF_CONFIG_DIFF.json` lists
all 29 differences between host experimental and RCU OFF
configs. Twelve checked key feature options remain `=y`,
including BCM4752 GNSS, XMM6262 modem, USB DWC2, MMC, ext4,
FUSE, CMA, ZRAM, sound/ALSA, HighMem and PREEMPT_RCU. This
does NOT certify that driver module vermagic, symbol ABIs,
XMM runtime bootstrap, cameras, GPS or power management all work
with the new image.

Critical blockers before installation
-------------------------------------
1. The running physical G650 BOOT p9 hash was NOT reread;
   the historical HOST image SHA must not be described as a new
   physical SHA verification.
2. RECOVERY/TWRP guard checks and postwrite readback were NOT run.
3. This is a raw offline Android boot image, NOT a TWRP-signed OTA
   and not a validated candidate for the live device yet.
4. New RCU-OFF removes an experiment that physical G650 had
   compiled in. Although tested device Kconfig options remain
   enabled, module and firmware ABI and modem/GNSS readiness
   need stronger checks.
5. User requested NO backups and is away. Do NOT install an
   unproven kernel or reboot a known working tablet in their
   absence, especially without verified rollback/recovery.
6. The Android device makefile `t311-custom-bootimg.mk`
   still pins historic G541 SHA and must not be silently changed
   or allowed to disguise this test-only G696 image.

Result: full format-correct host-only BOOT candidate exists, but
NOT physically deployed and NOT declared fully hardware-qualified.
No modifications to Android/kernel executable C in G696; exactly
four earlier runtime kernel source paths remain STAGED NOT INSTALLED:
mm/filemap.c, mm/page_alloc.c, mm/shmem.c, mm/vma.c, plus one
optional experimental build Kconfig overlay.

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
