SM-T311 G680: late writable mmap and no-ref reader cache alias
=================================================================

Concrete race in the G650/G679 fastpath
---------------------------------------
In mm/filemap.c:filemap_read_rcu_batch(), the original code checked
mapping_writably_mapped(mapping) BEFORE rcu_read_lock(). A different
thread can enter mm/vma.c:mmap_region() and increment
i_mmap_writable and publish a shared writable VMA while the reader
copies through a kernel-side kmap_local_page. On ARMv7, filemap_read's
normal referenced path calls flush_dcache_folio for this case, but
the ref-free path cannot safely flush the folio.

This is a genuine time-of-check versus time-of-use *source path*.
It does NOT establish actual data corruption or an ARMv7 physical
fault. A second unchecked mapping_writably_mapped test or smp_mb
would also NOT prevent a map being published immediately afterwards.

G680 staged fix
---------------
1. mm/filemap.c: move rcu_read_lock() BEFORE the first and only
   mapping_writably_mapped() check in filemap_read_rcu_batch. Return
   to the referenced slowpath with matching rcu_read_unlock() if
   any shared writable mapping is present. Pagefault-disable and
   the remainder of the RCU read-copy batch stay in their current
   order. The generic non-SM-T311 code sees a very minor lock
   boundary change but no new mmap wait.
2. mm/vma.c:mmap_region() under
   CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT: once mapping_map_writable()
   has successfully increased the writable mapping counter, if
   mapping_rcu_read() indicates the inode previously had a tagged
   RCU folio, wait for synchronize_rcu() before __mmap_region()
   makes that VMA accessible. This marker is persistent for the
   inode's lifetime. Non-experimental kernels do not get this wait.

Why both sides are required
---------------------------
The reader must enter RCU before observing the writable counter.
The writer must publish the positive counter BEFORE starting a grace
period, then wait for that grace to complete before VMA publication.

A reader that observed zero on the counter belongs to an earlier
RCU read-side section and must finish before the new shared writable
mapping can become accessible. A reader that arrives after the count
became positive goes through the referenced path and may flush D-cache
as the regular filemap_read already does.

This protects this particular appearance-of-new-writable-VMA race;
it does not exclude all cache alias hazards, changed page contents
from concurrent pwrite, private writable aliasing, remap/mprotect
operations, or all readers/writers outside these two entrypoints.
Nor is a numeric PFN/pointer-generation token introduced.

Actual herd7/LKMM validation
----------------------------
Four C litmus tests were executed using actual herd7 7.58 and
tools/memory-model/linux-kernel.cfg:

+--------------------------------------+------------------+----------+----------+
| Model                                | Outcome          | Positive | Negative |
+======================================+==================+==========+==========+
| Reader RCU + writable count + GP     | Never            | 0        | 3        |
+--------------------------------------+------------------+----------+----------+
| No writer GP                         | Sometimes        | 1        | 3        |
+--------------------------------------+------------------+----------+----------+
| Writer smp_mb instead of GP          | Sometimes        | 1        | 3        |
+--------------------------------------+------------------+----------+----------+
| Reader count check before RCU lock   | Sometimes        | 1        | 3        |
+--------------------------------------+------------------+----------+----------+

All 15 source guards PASS and 7 mutated *real C source inputs*
are rejected by tools/testing/selftests/smt311/rcu_mmap_publish_g680.py.
Raw normalized output hashes and results are retained in
Documentation/mm/sm-t311-rcu-mmap-publish-g680.json.

This tests a minimal ordering handshake, not full mmap/VMA internals
or actual Exynos4212 cache/TLB hardware. The original bug was not
physically reproduced on the tablet.

ARM32 independent compilation completed:

  mm/filemap.o:
    f7a9794dacf55f7ed9fb0bc65924ec00bc579c2dee63cc87ba8f9c734baf16a8
  mm/vma.o:
    80113873f26758d8e8523ad197807c87029652cc669fedf263edc1c0234f88fb

Built with real SM-T311 defconfig and arm-linux-gnueabihf-gcc.
These are standalone ARM EABI5 relocatable objects, NOT a complete
flashable BOOT image.

Risk, tradeoffs, deployment
---------------------------
The new synchronize_rcu() may introduce latency on a new shared
writable mmap for an inode that has previously been tagged. It is
called while mmap_lock is held. RCU read sections are bounded to
32KiB but the actual tail latency has NOT been measured. Risk of
deadlock with unusual mmap/error paths must be evaluated before
RCU ON, and any publication paths bypassing mmap_region should be
audited. No normal fastpath flush is attempted without a folio ref.

As of G680, three executable files are STAGED NOT INSTALLED vs
physical G650 a2fca4558:

* mm/filemap.c — G674 inode-exit tag retention + G680 check under RCU
* mm/page_alloc.c — G676 full callback completion wait
* mm/vma.c — G680 writable-mmap publication grace

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE. No
nighttime audio/camera/phone tests, no backups and no flash. no-ref
RCU ON is NOT approved.


G683 supersession: the AS_RCU_READ-only writer gate above was
identified as insufficient under a concurrent marker/writer
publication outcome (G683 real LKMM). The final current STAGED
mm/vma.c code waits GP for every new EXT4 shared writable mapping
under the SM-T311 experiment config, even if AS_RCU_READ was observed
clear. See sm-t311-rcu-eligibility-publication-g683.rst.


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
