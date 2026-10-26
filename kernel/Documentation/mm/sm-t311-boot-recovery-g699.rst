SM-T311 G699 — Read-only BOOT/RECOVERY identity, fail closed
============================================================

Why G696 cannot yet be installed
--------------------------------
The fully formatted G696 RCU-OFF Android boot v0 image exists on
the HOST, but its physical identity has not been proved. The tablet
still boots G650. Correct host packaging and SHA256 checks do NOT
establish that the currently installed BOOT and RECOVERY partition
contents match historical image files.

G699 performed only PASSIVE adb shell diagnostics on G650:
- `id`: uid=2000(shell), SELinux `u:r:shell:s0`;
- /dev/block/mmcblk0p9 and p10 exist as root:root `brw-------`;
- `sha256sum /dev/block/mmcblk0p9`, `blockdev --getsize64` for
  both p9 and p10, and reading the sysfs partition sizes were
  denied by the running Android SELinux/DAC permissions;
- old `/dev/block/platform/dw_mmc/by-name/BOOT` and
  `.../RECOVERY` were not visible through this shell;
- physical G650 `boot_completed=1`, kernel 7.3.0-pjarczak,
  Enforcing. No reboot, root escalation or bypass attempted.

IMPORTANT: the older installed TWRP historically constructs
`/dev/block/platform/dw_mmc/by-name/*` aliases. Missing aliases
from an Android11 shell do NOT prove the TWRP filesystem is broken.
The correct test must run IN actual verified TWRP, not Android.

Independent complete host BOOT and TWRP images
----------------------------------------------
G699 found the preserved original TWRP image at:

  /home/debian/Projekty/samsung/artifacts/
    SM-T311-G52-INDEPENDENT-BOOT-RESCUE-20261013/
    RECOVERY-TWRP-F818C2-8MiB.img

Verified on Debian: exact 8,388,608 bytes, Android v0 `ANDROID!`
header, page size 2,048, valid Android boot SHA1 header ID and
zero-filled padding/trailer. SHA256:

  f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f

This is identical to the historically measured TWRP p10 SHA256,
but it was NOT read from the CURRENT physical p10 in G699.
The recovery image uses kernel address 0x10008000 and ramdisk
address 0x11000000 (correct for recovery), different from BOOT
ramdisk address 0x12000000. Do not confuse these two formats.

Host prior physical G650 BOOT image remains SHA256:

  2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3

HOST G696 RCU-OFF candidate:

  ba9aed97fe7a028fc9ff5b540c916b251a7cd064a9fbf4055b5eb3f7af254204

The latter is a full size 8MiB Android boot v0 constructed from
the same original G650 ramdisk and Exynos 3g8 DTB, not a boot test.

Permanent reproducible code and negative cases
----------------------------------------------
Project sources:

  tools/g699_boot_recovery_gate.py
  tools/g699_twrp_readonly_boot_identity.sh
  G699_BOOT_RECOVERY_HOST_PREFLIGHT.json

Host gate validates real bytes and SHA for G650 BOOT, G696
candidate and TWRP RECOVERY, Android SHA1 boot header IDs and
distinct addresses, exact preserved G650 ramdisk and Exynos DTB.
It explicitly records Android shell uid=2000 and DENIED physical
partition readback: `physical_p9_current_sha_known=False`,
`physical_p10_current_sha_known=False`; `safe_to_flash_G696=False`.
It checks the strictly read-only TWRP preflight script itself, including
failure on unknown live hashes and no device writes or reboots.
Host report: 24/24 positive, 10/10 deliberately corrupted
images/policies/manual guard/observations rejected.

The tracked kernel regression:

  python3 tools/testing/selftests/smt311/rcu_boot_recovery_identity_g699.py

re-runs host preflight and asserts 18/18 positive, 6/6 deliberately
falsified live-readbacks, hash, TWRP-state and guard changes caught.
JSON saved in `Documentation/mm/sm-t311-boot-recovery-g699.json`.

The manually executed (FUTURE, **NOT executed now**) script is
`tools/g699_twrp_readonly_boot_identity.sh`. It requires real
`/sbin/twrp`, BusyBox, root, physical p9/p10 block devices,
correct old `dw_mmc` symlink targets, EXACT 8MiB partition
sizes, EXACT full p9 G650 and p10 TWRP SHA256. It fails closed
on any mismatch. It contains NO writes/flash/mount/reboot or
backup actions and prints only the resulting PASS/FAIL.
Do not run on Android; recovery must already have been entered
safely by an owner-attended process. G699 DID NOT enter TWRP.

Remaining blockers
------------------
- Current physical BOOT and RECOVERY SHA remain unknown (read denied).
- Present TWRP accessibility/ADB and original aliases are not
  retested this morning, even though historically they worked.
- G696 is not a signed, physical hardware-accepted OTA.
- G697 module symbol and G698 GNSS/RIL HAL/source checks are static;
  no active call/SMS/GNSS fix or USB OTG test on G696.
- G692 experiment ON is fail-closed only in STAGED sources.
- Physical system is in use and user explicitly requests no backup.
  Do NOT attempt a speculative flash without a verified recovery
  path and current physical read-only identity. No G699 flash.

Unchanged staged executable kernel paths vs G650:
mm/filemap.c, mm/page_alloc.c, mm/shmem.c and mm/vma.c, plus
the optional experiment-only configuration overlay.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED;
S5 BLOCKED; S6 BLOCKED; S7 PARTIAL; S8 ACTIVE.
