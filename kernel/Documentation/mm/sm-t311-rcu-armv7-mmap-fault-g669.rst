SM-T311 G669: ARMv7 PTE publication and writable mmap fault coherence
======================================================================

Question
--------
G668 found a source-order interval in which filemap_read_rcu_batch()
observes i_mmap_writable == 0, then a second thread creates a shared
maywrite VMA and writes a file page before the first thread completes
the speculative ref-free RCU copy. This review follows the *real*
ARMv7 fault/PTE/cache path instead of inferring a coherence failure.

Correct ARMv7 cache/PTE path
----------------------------
For ARMv6 and later, arch/arm/include/asm/tlbflush.h defines
update_mmu_cache_range() as an empty inline. That is intentional:
cache synchronization is performed in arch/arm/mm/mmu.c:set_ptes(),
which, for valid non-special userspace PTEs, invokes
__sync_icache_dcache(pteval) BEFORE set_pte_ext() publishes the PTE.
For SM-T311 CONFIG_CPU_CACHE_VIPT=y and CONFIG_SMP=y.

arch/arm/mm/flush.c:__sync_icache_dcache() handles VIPT aliasing by
using folio_flush_mapping() and conditionally
__flush_dcache_folio() when PG_dcache_clean is not set. For
nonaliasing VIPT with a non-executable PTE, this flush can be skipped.
The underlying cache flush handles the lowmem direct mapping and,
when aliasing, flush_pfn_alias() in the file's relevant color.

arch/arm/mm/mmap.c:arch_get_unmapped_area() aligns file and shared
mapping offsets for aliasing VIPT and checks MAP_FIXED shared color
constraints. These are positive protections; they do not establish
the exact runtime cacheid/geometry of both actual Cortex-A9 cores.

mm/memory.c:do_shared_fault() invokes do_page_mkwrite() for the
filesystem before finish_fault(), while set_pte_range() publishes
the PTE via set_ptes(). ext4_file_vm_ops sets page_mkwrite to
ext4_page_mkwrite(), which takes a shared invalidate lock and
checks mapping identity in the appropriate branch. A reused write
fault in wp_page_reuse() flushes the cache before enabling write.

Limitations
-----------
Those source paths support PTE initialization and existing cache
coherency, but do not provide a lock or lifetime pin spanning
filemap_read_rcu_batch()'s entire copy. The fast read checks
mapping_writably_mapped() only once BEFORE entering the RCU section.
A new mapping or user write can still begin after that observation;
this is normal from a scheduling standpoint. A shared file read can
also overlap ordinary writes even on the traditional referenced path,
so byte-level mixed visibility by itself is NOT data corruption/UAF.

A demonstrable harmful ARMv7 cache alias event would require
real cacheid and page color evidence plus a reachable concurrent
interleaving showing wrong bytes, in a mode that does not jeopardize
the physical tablet. S2 folio lifetime and S3 HighMem/cache remain
unproven. Adding a second unpinned counter check alone cannot
remove the window, and adding expensive locking before understanding
the performance cost would be premature.

Reproducible source gate
------------------------
Run::

    python3 tools/testing/selftests/smt311/rcu_armv7_fault_g669.py

G669: 24/24 source properties PASS, six independent modified-source
variants rejected by running the exact same checks on mutated
input. Mutants remove PTE sync, D-cache flush, file color flush,
page_mkwrite, MAP_FIXED color check, or the no-ref mapping guard.
Raw machine record is
Documentation/mm/sm-t311-rcu-armv7-mmap-fault-g669.json.

This is not a CPU/ARMv7 LKMM or executable Android concurrency test,
not an end-to-end cache coherence proof, and not clearance to enable
the reference-free path. Previous G665 genuine XArray userspace C
binary remains UNRUN without real liburcu development headers.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED;
S5 BLOCKED; S6 BLOCKED; S7 PARTIAL. No production kernel changes,
new BOOT image, TWRP flash, RCU ON, or backups.
