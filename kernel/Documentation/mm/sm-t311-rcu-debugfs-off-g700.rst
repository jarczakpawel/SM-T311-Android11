SM-T311 G700 — production-candidate RCU OFF retaining DEBUG_FS
================================================================

Motivation
----------
The G687/G696 standard RCU-OFF profile disabled `CONFIG_DEBUG_FS`
along with the reference-free page-cache experiment. G697 proved that
this also removed 128 globally defined diagnostic names, including
DWC2/ALSA and debugfs functions. This is avoidable on a device
requiring modem, GNSS and USB debugging.

G700 independently full-built a new Linux 7.3 ARMv7 candidate
configuration with DEBUG_FS **retained**, but the two actual
reference-free pagecache options **disabled**:

  CONFIG_DEBUG_FS=y
  # CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is not set
  # CONFIG_PAGE_RCU_FREE is not set

The source fragment is tracked in
`arch/arm/configs/sm_t311_rcu_off_keep_debugfs_g700.fragment`.
The standalone reproducible builder is
`tools/testing/selftests/smt311/build_g700_debugfs_rcu_off.sh`.
Starting from the physically G650-matched experiment profile
(`../build/g686-reproducible-exp-on/.config`) G700 changes
EXACTLY TWO Kconfig feature selections, while G687 changed
29, including DEBUG_FS.

Note: the experimental G650 source, with G692 staged setter
preventing ON, is not the deployed original G650 BOOT. A matching
G650 Kconfig does not prove a binary is identical to physical G650.
Both G686/G700 host builds use the same ARM EABI5 GCC 16.2.0-3,
and differ from the physical G650 /proc/config.gz in three
Rust LLVM toolchain version metadata fields, identified in G695.
This is host-profile comparison, NOT a real device performance test.

G700 physical-grade HOST artifacts
----------------------------------
Actual `make zImage dtbs vmlinux` fully linked PASS with
`7.3.0-pjarczak`, four core source paths staged; separate
output dir `../build/g700-debugfs-rcu-off`:

  vmlinux SHA256
    027fa9b9cce7faf3b67b35a2bc256f0f34f078b47b75be0e3e5d42caa353715f
  zImage SHA256
    a69e2a9972d9caa24d8428b0749d05b91978084da4b5d4b5caa7d43e41746790
  3g8 board DTB SHA256
    6fb3e77075733d531abaa2f4c3fa1bf55c11a50f04cc5acc9838db5ea0f41bfb

The DTB is byte-identical to the existing G650 board DTB.
The G700 project generator
`tools/g700_pack_debugfs_rcu_off.py`
imports the same strict G696 Android boot v0 packer, validates
that the original 8MiB G650 image re-encodes byte-for-byte,
preserves its EXACT 786423-byte ramdisk, Android boot v0 header
parameters and kernel load address, and repacks the new zImage
and unchanged board DTB into a complete 8MiB HOST BOOT:

  artifacts/G700-RCU-OFF-DEBUGFS-HOST-ONLY/
    BOOT-G700-RCU-OFF-DEBUGFS-HOST-ONLY-DO-NOT-FLASH.img

  SHA256
    aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f

Machine provenance and explicit `flash_authorized=false` are
preserved in `G700-DEBUGFS-OFF-EXPERIMENT-BOOT.json`.

Symbols and expensive mmap GP
-----------------------------
Real `nm -g --defined-only` results from three actual
fully linked host ARM32 vmlinux outputs:

  Profile                    Globally defined names
  ------------------------------------------------
  G686 experimental (ON forbidden by G692)     26742
  G687 ordinary RCU-OFF / DEBUG_FS=n           26614
  G700 RCU-OFF / DEBUG_FS=y                    26742

G700 retains ALL 26742 globally defined names from the matched
G686 experimental profile. G687 lost 128 names. G700 retains
`debugfs_create_file_full`, `dwc2_debugfs_init`,
`snd_soc_debugfs_root`, and all named platform family symbols.
It nevertheless has `CONFIG_PAGE_RCU_FREE` and
`CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT` DISABLED.
Keeping DEBUG_FS does NOT turn on experimental no-ref RCU.
Symbol NAME equality is not ELF layout/struct ABI equivalence;
compare real physical functionality separately.

Actual `mm/vma.o:mmap_region` in G700 has 356 bytes / 89
ARM words with NO `synchronize_rcu` relocation that was added
in experimental G683 source, while the shmem read iterator
compiles to 920 bytes / 230 ARM words as the RCU-OFF variant.
Thus the unneeded conservative GP from the experimental
enable-able reader is compiled OUT, with GNSS/USB/debugfs
diagnostic infrastructure retained.

Tests and operational safety
----------------------------
Run on Debian:
  python3 tools/g700_pack_debugfs_rcu_off.py
  python3 tools/testing/selftests/smt311/rcu_debugfs_off_binary_g700.py

The verifier checks 21 actual source/ELF/config/DTB/complete
8MiB image predicates and rejects 9 simulated tampered inputs.
Its machine record is
`Documentation/mm/sm-t311-rcu-debugfs-off-g700.json`.

**This is a host-only BOOT and has not been installed or
physically booted**. Real physical BOOT p9 / RECOVERY p10 hashes
remain unreadable to Android shell uid2000, as G699 documented.
TWRP manual readonly identity check has NOT been run.
No physical RIL/SMS/GNSS/USB/SELinux performance acceptance is
claimed for G700, no user-attended recovery preflight has been
executed, and no signed installer or rollback package exists
for G700. CONFIG_DEBUG_FS remains accessible only under the
appropriate DAC/SELinux controls but increases debugging
surface; any eventual production decision must consider that.

G700 adds no executable kernel C/Android changes; previous
four modified source paths are still STAGED NOT INSTALLED:
mm/filemap.c, mm/page_alloc.c, mm/shmem.c, mm/vma.c.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED;
S5 BLOCKED; S6 BLOCKED; S7 PARTIAL; S8 ACTIVE.
