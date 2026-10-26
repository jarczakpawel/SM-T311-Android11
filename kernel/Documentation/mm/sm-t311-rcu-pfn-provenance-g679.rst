SM-T311 G679: source-traced PFN reuse and untagged-donor lifetime
=================================================================

Purpose and verdict
-------------------
Continue the real Linus/Kiryl ref-free filemap reader review on
SM-T311, specifically the risk that a pointer returned by xas_load()
is an old unmarked folio A that has since been returned to the
allocator, then reused as a tagged folio B (and potentially later as
a folio C) at the SAME numeric address. There is no allocator
generation comparison in the actual fastpath.

This step is NOT another toy concurrency enumerator. It traces
concrete source conditions in the actual kernel and runs mutation
controls against changed copies of the real source. It is NOT a
formal proof of lifetime, a physical PFN tracing run, nor permission
to enable ref-free RCU.

Reproduce:

    python3 tools/testing/selftests/smt311/rcu_untagged_pfn_g679.py

The test verified 24/24 code-local, source-bounded predicates and
rejected 9/9 real source mutations (including removal of the reader
RCU lock, mapping recheck, inode eviction tag retention, allocator
defer, hwpoison RCU grace, migration ref freeze, split tag transfer
and replacement XArray publication). Raw results are in
Documentation/mm/sm-t311-rcu-pfn-provenance-g679.json.

Physical reuse sequence
-----------------------
Consider one numeric struct folio / PFN address P, reused with
successive object identities:

* A is an unmarked order-0 file-backed folio of inode M.
* Reader M enters RCU, xas_load() returns address P (A), but takes no
  folio reference.
* Writer removes A from the XArray. A is unmarked, so its last
  reference can go through ordinary allocator free without an RCU
  grace period. P may become a different object B.
* B belongs to inode M or a different inode N. Under a real reference
  in filemap_read_accessed(), filemap_mark_hot() may set B's
  PG_rcu_free; only hot ext4, order-0, ZONE_NORMAL candidates are
  allowed in the SM-T311 experiment.
* Reader M could observe B's PG_rcu_free after obtaining A's address.
  This tag sample is NOT an observation of allocation generation.
  The later smp_rmb/i_size/xas_reload/mapping checks may reject the
  new state, or may find that P has returned to the original slot.
* If the reader does accept the tag of B, B's physical memory MUST
  remain unrecycled for that reader's RCU lifetime. Without that
  premise, a later C can occupy the same address and may be freed
  during the reader's copy.

The necessary protocol is *not* that A can never be recycled during
RCU; that is demonstrably false for unmarked A. Rather, **the
generation whose marked flag was accepted may not itself be recycled
until an RCU grace period**. A same-address equality check is not a
folio generation check. These constraints have source support, but
proving them across all real paths remains open.

Precise code anchors
--------------------
Reader: mm/filemap.c:filemap_read_rcu_batch() enters rcu_read_lock()
before the lookup and holds it through _copy_to_iter and unlock.
filemap_read_rcu_lookup() reads xas_load() pointer, then
READ_ONCE(flags) and __PG_RCU_FREE, orders i_size and XArray reload
with smp_rmb, and rejects a mismatching folio->mapping. The reader
does NOT call folio_try_get() and does NOT compare a generation.

Marker: filemap_mark_hot() is called after referenced slow-path
file reads, guarded by the static key (default FALSE). Only
order-0, ZONE_NORMAL ext4 and accepted flags can be tagged.
folio_test_set_rcu_free() is atomic. Unlike a synchronous pin,
the mark can be observed by a speculative reader of another inode
if the same numeric address P was recycled from A.

Single and batch eviction: page_cache_delete() and
page_cache_delete_batch() ordinarily used to clear PG_rcu_free on
AS_EXITING. The G674 staged SM-T311 fix explicitly excludes
CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT from this optimization. This
protects B even when *B's own inode* N is being evicted but the
speculative reader belongs to *other inode* M.

Allocator: mm/page_alloc.c:__free_pages_prepare() clears the
accepted flag on last free and routes tagged folios to
rcu_free_defer(), not immediately into the buddy/PCP allocator.
queue_rcu_work() / RCU GP and the G676 flush_rcu_work() correction
protect the physical lifetime, subject to the remaining concurrency
and callback proofs.

Migration: mm/migrate.c:__folio_migrate_mapping() freezes references
under the XArray lock, stores the replacement pointer, and unfreezes
the old folio. folio_migrate_flags() does NOT transfer PG_rcu_free.
The old tagged folio is expected to reach the deferred last-free
path, while the new folio is independently ineligible until marked.
These facts are not a complete proof of an unreferenced speculative
reader spanning all migration cases.

Split: mm/huge_memory.c copies __PG_RCU_FREE for newly separated
parts and publishes page flags before clearing compound-tail state.
However the actual SM-T311 ref-free reader explicitly excludes
folio_test_large(), so higher-order folio split is outside the
normal candidate population of this experiment. Do not generalize
this conclusion to other configurations.

Hwpoison: mm/page_alloc.c permits a special non-deferred path for
a marked, order-0 HWPoison page. mm/memory-failure.c waits on RCU
in page_handle_poison() before setting HWPoison with release, and
memory_failure() has an explicit tagged-folio RCU wait. A poisoned
page is quarantined and not a normal reusable buddy folio; this is
a separate exception, not evidence that tagged normal pages may be
reused without a grace period.

Missing proof obligations
-------------------------
1. Real pointer/PFN generation publication and reuse under Linux
   PREEMPT_RCU and ARMv7 weak memory ordering (no test-only
   generation field).
2. All alternate error/exiting/migration/split/memory-failure paths,
   including locks, reference ownership and completion of callbacks.
3. Concurrent mapping/size/data mutation and writable-mmap aliasing
   while a no-ref reader copies with pagefault_disable.
4. Real Cortex-A9 VIPT cache/TLB/HighMem and worst-case Normal-zone
   reclaim/RCU deferred free pressure.
5. Safety of the chosen static-key activation and measurable benefit
   on actual dual-core UI workloads. None has been established.

Source results cannot promote S2/S3 to PASS. The safe default
remains RCU OFF.

Deployment
----------
G679 adds tests, evidence and documentation only; NO executable
kernel source diff. The only staged executable files relative to
physically deployed G650 source a2fca4558 remain
mm/filemap.c (G674) and mm/page_alloc.c (G676). Both are STAGED NOT
INSTALLED. No experimental flash, no backups, and no nighttime
audio/camera/phone testing.

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED, S5 BLOCKED,
S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
