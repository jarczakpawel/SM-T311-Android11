SM-T311 G683: RCU eligibility publication vs writable mmap count
==================================================================

Source-identified race and restricted mitigation
-------------------------------------------------
The G680/G682 writer originally skipped its pre-publication RCU GP
when mapping_rcu_read(file->f_mapping) was false. That predicate
samples AS_RCU_READ, a separate atomic mapping flag that
filemap_mark_hot() may publish concurrently with the writer's
atomic increment of i_mmap_writable.

The reader consults AS_RCU_READ *before* its RCU read-side section
and later checks i_mmap_writable *inside* RCU. Neither test_bit nor
the independently modified atomic writable count is a complete
cross-location publication protocol by itself.

An abstract three-actor memory-model witness exists in which a
hot-page marker publishes eligibility, a reader sees eligible=1 but
still sees the old count=0, and the mmap writer sees eligible=0
despite having increased writable count. The writer would skip
its own GP and publish a VMA while the model reader is still active.
The LKMM test even has full writer memory barriers around the
abstract eligibility/count writes, though it is not a faithful
model of every ARMv7 atomic bit-op instruction.

New G683 STAGED source correction in mm/vma.c
--------------------------------------------
Within CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT, for a file-backed
VM_SHARED|VM_MAYWRITE mmap:

1. mapping_map_writable() publishes the increment to the file's
   writable-mapping count;
2. if the file's inode belongs to EXT4_SUPER_MAGIC, wait through
   synchronize_rcu() regardless of AS_RCU_READ;
3. then proceed to __mmap_region() and VMA publication.

Other filesystems cannot be tagged by this exact SM-T311
experiment: filemap_mark_hot checks EXT4_SUPER_MAGIC and requires
order-0 folios in ZONE_NORMAL. The source explicitly guards against
a NULL mapping->host. The new code adds an include for linux/magic.h
under the experiment configuration. Normal configurations do not
acquire this wait.

We retain G680's reader-side rcu_read_lock before
mapping_writably_mapped and G674/G676 deferred folio protections.
We deliberately do NOT put smp_mb in every ref-free read, as that
would add a hot-read cost and is only a small-model alternative.

Real LKMM/herd7 and source checks
---------------------------------
Run::

  python3 tools/testing/selftests/smt311/rcu_eligibility_publication_g683.py

Four actual C litmus cases with the kernel's linux-kernel.cfg:

+--------------------------------------------+------------+----------+----------+
| Publication order condition                | Outcome    | Positive | Negative |
+============================================+============+==========+==========+
| Old writer gate on AS_RCU_READ              | Sometimes  |        1 |        8 |
+--------------------------------------------+------------+----------+----------+
| Ext4 writer always waits RCU GP             | Never      |        0 |        8 |
+--------------------------------------------+------------+----------+----------+
| Writer inserts smp_mb, but still skips GP   | Sometimes  |        1 |        8 |
+--------------------------------------------+------------+----------+----------+
| Additional reader barrier, not implemented | Never      |        0 |        7 |
+--------------------------------------------+------------+----------+----------+

The test validates 14/14 C-source predicates and rejects eight
deliberately damaged real-source variants. It stores model SHA256,
normalized herd7 stdout SHA256 and exact observations in
Documentation/mm/sm-t311-rcu-eligibility-publication-g683.json.

The actual ARM32 cross-compiler built mm/vma.o as ARM EABI5,
with SHA256
44d37f7e6fcbfc24dd065e271ac43e953aaedce140b9ae2774a7921d73d6d2bf.

Crucial limits / cost of fix
----------------------------
These are real herd7 results, but abstract:
eligible and writable are scalar model variables, marker operations
are simplified and writer memory barriers are illustrative. The
model does not represent full atomic-bit coherence, VMAs, allocator
PFN ABA, HighMem/D-cache or all readers and writers. An LKMM allowed
execution is NOT a confirmed physical UAF.

Waiting for synchronize_rcu() on EVERY ext4 shared-maywrite mmap
while CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is enabled can be costly
even with the runtime static key default OFF. The call remains
inside mmap_region with mmap_lock write-held. No lockdep completion
proof, benchmark or acceptable p99 mmap latency is available.
This is a conservative staged mitigation, NOT approval to flash.

Old G680/G681/G682 tests that previously pinned the
AS_RCU_READ-only writer predicate have been updated to fail closed
against the new ext4 condition while preserving historical LKMM
results as historical comparisons.

The number of STAGED executable source paths stays THREE, all
NOT INSTALLED relative to physical G650 a2fca4558:

- mm/filemap.c (G674 + G680);
- mm/page_alloc.c (G676);
- mm/vma.c (G680 + G683).

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.


G684 ARM32 configuration correction
-----------------------------------
The old g674-inode-aba-safety build directory did NOT enable
CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT (its CONFIG_DEBUG_FS was off);
therefore its mm/vma.o ARM32 build never compiled the guarded
G680/G683 writer branch. Corrected in G684 by compiling real
experiment-enabled mm/vma.o, mm/filemap.o and mm/page_alloc.o
using g633-armv7-rcu-zone-guard .config with
CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y / PAGE_RCU_FREE=y / DEBUG_FS=y.
See Documentation/mm/sm-t311-rcu-toggle-mmap-g684.rst and JSON.
This overrides the earlier object hashes as test evidence, without
altering old Git history or physical BOOT.
