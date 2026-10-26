SM-T311 G655: hot-folio reference-free RCU reader/writer review
================================================================

Samsung SM-T311 only: Exynos4212 Cortex-A9 dual-core ARMv7, HighMem,
Android11, Linux7.3.0-pjarczak. On-tablet BOOT still G650. This is a
static-source audit, not a proof of no UAF/ABA and NOT an RCU ON approval.

Safety review matrix (current code tree)
----------------------------------------

1. Reader: mm/filemap.c filemap_read_rcu_lookup under RCU lock:
   xas_load -> flag PG_rcu_free test -> smp_rmb -> 32-bit SMP
   seqcount-protected i_size_read -> smp_rmb -> xas_reload -> smp_rmb
   -> order0/ZONE_NORMAL/mapping -> copy under disabled user faults.
   The inode is referenced by the file; speculative folio is NOT
   refcounted. XArray node lifetime is not enough to prove folio life.

2. Marking publisher: mm/filemap.c filemap_mark_hot marks referenced
   ext4 HOT order0 ZONE_NORMAL folio only if default-OFF key enabled
   and resource guard allows new deferred frees. The set bit is a real
   architecture atomic bit operation, not an illustrative smp_wmb.
   Existing normal folio_mark_accessed() must remain in RCU OFF path.

3. Removal: page_cache_delete(single+batch) and
   replace_page_cache_folio under mapping XArray locks. Inode-exiting
   special path clears PG_rcu_free before final free. Proof needed that
   a filemap reader never starts after inode teardown and none still
   relies on deferred release when tag is cleared.

4. Migration: __folio_migrate_mapping under xas lock uses
   folio_ref_freeze(), replaces XArray slot and leaves the old page's
   existing mark until final free. Page movement between zones and
   mapping revalidation remain concurrency obligations.

5. Split/collapse: mm/huge_memory.c __folio_split copies
   __PG_RCU_FREE to new child folios and has smp_wmb before making
   pages noncompound. Khugepaged collapse replaces mapping page
   entries. Board NOREF reader restricts to order0, but marked
   old order0 pages could be reparented by other MM activity.

6. Hwpoison: mm/memory-failure.c explicitly performs
   synchronize_rcu() on marked folios for special free and unmap
   actions; separately mf_free_rcu_pages() calls rcu_free_sync()
   for deferred pages. An early-return path audit is still needed.

7. Pipe/FUSE: fs/fuse/dev.c rejects trying to steal a tagged
   folio from a pipe. This source supports protecting an RCU reader;
   FUSE itself is excluded as reference-free filemap read target on
   this board. The global page allocator may still see tagged folios.

8. Shmem: board's filemap_read_rcu is ext4-only and shmem read_iter
   deliberately skips it. This reduces candidate coverage and is
   NOT a proof that generic free/shmem corner cases can be ignored.

9. Final free: mm/page_alloc.c takes PG_rcu_free into deferred
   per-CPU RCU queue and only returns to buddy after grace. The
   PG_hwpoison special case has separate reader waits. Queue pressure
   and RCU grace-period progression remain S3 questions.

10. ARMv7 size: include/linux/fs.h i_size_read is wrapped in
    seqcount for CONFIG_SMP and BITS_PER_LONG==32. A model built
    from 64-bit acquire-load code would be INVALID for SM-T311.

11. Unknown: pointer-address ABA in a stale speculative lookup
    across a discarded unmarked folio and a newly reinserted
    marked folio, exact data publication barriers, address_space
    exiting and cache aliasing. Do not claim corruption unless a
    source-accurate permitted interleaving and unsafe contents
    are demonstrated by actual LKMM and code review.

Automated anchors: tools/testing/selftests/smt311/rcu_writer_sites.py
currently locates 23 sites and asserts 17 source-level invariants.
Its JSON is Documentation/mm/sm-t311-rcu-writer-inventory.json.
This cannot enumerate EVERY dynamic callgraph path or prove semantics.

LKMM actual runner prerequisite
-------------------------------

tools/testing/selftests/smt311/lkmm_prepare.py --probe checks that
the external herd7 tool can run the kernel's own full-barrier SB
litmus via tools/memory-model/linux-kernel.cfg and returns the
documented Never outcome, not merely an executable with missing
model files. Debian herdtools7 7.58-1 has a documented issue where
its model resource files were missing (Debian bug 1144657); use a
verified fixed distribution / source installation. If unavailable,
the tool exits nonzero and S2 stays BLOCKED. The local three litmus
cases are intentionally simplified and cannot prove all memory
safety even if they execute successfully.

Mandatory plan:
S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED; S5 BLOCKED;
S6 BLOCKED; S7 PARTIAL. The next SM-T311 response MUST AGAIN repeat
DONE/BLOCKERS/NEXT/PHYSICAL BOOT and all S0-S7 with a reminder to
repeat that procedure every following project turn. Never mutate
device RCU ON or perform a new flash just for docs or tests.

Sources:
https://lwn.net/Articles/1100448/
https://lkml.iu.edu/2610.1/14838.html
https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=1144657
