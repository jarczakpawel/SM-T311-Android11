G716 — complete runtime removal of Linus/Kiryl reference-free page cache port
============================================================================

21 October 2026, Samsung SM-T311 / Linux 7.3.0-pjarczak / Android 11.

Decision
--------

The *experimental* reference-free page-cache mechanism is not deployed
to this 32-bit Cortex-A9 tablet. G716 deletes it from active kernel C/H
and Kconfig, not merely from the compiled profile. Normal Linux RCU,
the reference-backed page cache, Multi-Gen LRU and FUSE remain.

The 18 original Git commits (05362c1a7..016e91411) are KEPT in
immutable Git history for provenance. All 20 files touched by the
series were restored from their immediate ancestor 4432bceab.
Subsequent ARMv7 no-ref customization in mm/vma.c was reverted.
All optional FUSE passthrough-open counters accidentally guarded by
SM_T311_RCU_HOT4K_EXPERIMENT were removed, while the independent
read-only fusectl passthrough negotiation remains available.
Two independent buffered read optimizations were REAPPLIED to the
refcounted kernel, with behavior justified by their original diffs:

* Linus 899f8d53b: consolidate folio_mark_accessed in filemap_read().
* Kiryl d46226de3: skip ra->prev_pos store when unchanged (avoids
  needless cache-line writes when threads share an open file).

The 18-commit branch is NOT rebased/deleted from history, but the
present checked-out source no longer contains PG_rcu_free,
CONFIG_PAGE_RCU_FREE, special RCU-deferred allocator or debugfs
lockless-page-cache switch. No compile-time or runtime ON toggle exists.

Original 18 commits contained other modifications to splice,
memory-failure, migration, FUSE and shmem. Those were specific to
PG_rcu_free or the reference-free reader and were deliberately
reverted rather than retained speculatively. In particular, the
G674 inode-exit workaround, G676 special flush_rcu_work synchronization
and G683 ext4 mmap extra synchronize_rcu are no longer needed after
the entire special folio lifetime machinery was removed.

Build and safety gates
----------------------

Tracked, standalone CONFIG: arch/arm/configs/sm_t311_g716_clean_defconfig,
431 lines. A separate make sm_t311_g716_clean_defconfig invocation
reproduced the complete 7.3 G716 .config with ZERO symbol differences.
No dependency on old host G686 or G700 config remains.

Reproducible host compilation and BOOT pack::

  bash tools/testing/selftests/smt311/build_g716_clean_no_ref_port.sh

The script compiles FULL ARM32 vmlinux + zImage + DTBs, checks board
options (FUSE, FUSE_PASSTHROUGH, DEBUG_FS, LRU_GEN, HighMem), requires
kernelrelease 7.3.0-pjarczak, verifies that no-ref RCU Kconfig does
not exist, and invokes the G716 host-only Android boot packer.

Standalone read-only release gate::

  make -C tools/testing/selftests/smt311 check-clean-no-ref-port

PASS: 21/21 positive compile/link/source/BOOT checks, 7/7 modified
source/Kconfig/ELF controls correctly rejected. Full ARM32 link PASS.
Raw JSON: Documentation/mm/sm-t311-clean-no-ref-port-g716.json.

Final host binary:
vmlinux SHA256 0017bed38b58258ec7d8bc8987ffeeaf60fca2f984f3df71c043fdfb8a325122
zImage SHA256 029b6d3c21fc67b2892cde858e1fef134cfa07fe3ab9d3df4b39777617a3d8bf
DTB SHA256    6fb3e77075733d531abaa2f4c3fa1bf55c11a50f04cc5acc9838db5ea0f41bfb
full BOOT SHA 5d275fb7977d95e9e3515ff571efe675359e0874e1f8c5aef4b9c925d19182c1
BOOT size 8,388,608 bytes. Pinned G650 Android ramdisk SHA256:
681d59437776b09ede48dc57e16b34697d08b7161f820673b8c84df3943bd8fd.

ARM32 linked shmem_file_read_iter code sizes:
- historical physically G650-like compiled baseline G694: 976 B / 244 ARM instructions;
- G700 host no-ref OFF with old port source: 920 B / 230 insns;
- NEW G716 host fully removed port: 860 B / 215 insns.

Thus there is 116 B fewer ARM instructions in this function than
that G650-like benchmark object. NOT a throughput or power result.
G716 filemap_read = 1236 B as in G700 and comparable G650,
so no fake filemap speedup claim. Benchmark on physical tablet
after safely supervised installation only.

Provenance
----------

G716 complete BOOT is assembled using the previously verified G696
exact Android v0 packer and pinned historic G650 BOOT baseline
outside the tracked source. It verifies G650 byte-for-byte roundtrip,
preserves original ramdisk/cmdline/load addresses/DTB and rejects six
intentional corruptions. The 8MiB artifact and original G650 BOOT
baseline are not committed to normal Git; after fresh checkout the
baseline input must be made available from the existing project
artifacts, without retrieving/modifying live partitions. No backup
creation is needed. Release manifest is tracked in the project and
Android tools/G716-CLEAN-BOOT-MANIFEST.json.

Android's t311-custom-bootimg.mk selects the new G716 offline BOOT
and enforces its exact size+SHA in both normal and nodeps rules.
Android prebuilt images are Git-ignored; the tracked script
device/samsung/lt013g/tools/g716-stage-clean-boot.py verifies the
release manifest before staging.

Historical RCU-specific tools and reports remain in source control
as evidence but are RETIRED, not part of active build target.
The old G686/G700 boot generation workflows must not be used
for a new release after the removal. The new G716 build is canonical.

Physical limitations and priorities
-----------------------------------

Physical tablet is still G650 Android 11 Linux7.3.0-pjarczak.
G716 has NOT been installed; its p9 BOOT / p10 RECOVERY current SHA
and an owner-attended proven TWRP rescue path are missing. DO NOT
flash or reboot to TWRP unattended. No backups or audible tests.

S0 PASS physical G650; S1-S3 RETIRED/no-ref mechanism removed
(no production safety gate); S4 REMOVED/OFF; S5 BLOCKED physical
G716 A/B; S6 BLOCKED current BOOT/RECOVERY and verified TWRP;
S7 ACTIVE GNSS/AGPS, RIL call/SMS, USB OTG, HWC and ADB-WiFi;
S8 ACTIVE Git / code provenance.

The remaining S7 tasks are the real priorities. Never apply
old experimental G674/G676/G683 RCU fixes in isolation — their
prerequisite machinery no longer exists.
