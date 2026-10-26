SM-T311 G653: RCU folio lifetime reader/writer interleavings (SOURCE AUDIT)
===============================================================================

This is the first S1 engineering review, NOT a completed LKMM proof.
Current source and associated line numbers correspond to kernel Git G653
3f613168c; runtime code identical to physical G650 a2fca4558.

Reader (mm/filemap.c around lines 2998-3040):
  R0 rcu_read_lock in filemap_read_rcu_batch;
  R1 xas_load i_pages, which returns a speculative folio pointer;
  R2 READ_ONCE(flags) must contain PG_rcu_free;
  R3 smp_rmb, i_size_read(inode), pos<isize;
  R4 ARMv7 smp_rmb, xas_reload must equal the original pointer;
  R5 smp_rmb, require order0, ZONE_NORMAL and mapping equality;
  R6 copy user bytes with pagefault_disable and kmap_local_page;
  R7 rcu_read_unlock. Marking is performed by a separate normal-ref
  slow-path filemap_mark_hot, not while this speculative read is active.

Writer classes and outstanding proof obligations:

1. Single deletion (mm/filemap.c:132 page_cache_delete):
   xas_store(shadow) and folio->mapping=NULL. If mapping_exiting is
   true it explicitly clears PG_rcu_free. The proof must establish no
   ref-free reader of a dead inode is in progress when mapping_exiting
   causes early tag clearing.

2. Batch deletion (mm/filemap.c:285 page_cache_delete_batch):
   under XArray lock, removes cache entries and sets folio->mapping=NULL.
   Identical inode eviction exception requires proof. Old marked folio
   must survive any RCU read-side critical section until allocator defer.

3. Atomic replacement (mm/filemap.c:820 replace_page_cache_folio):
   xas_store(new), old->mapping=NULL, folio_put(old). An old tagged
   folio must retain its tag until __free_pages_prepare sees it.
   New folio may be untagged, so no-ref coverage can drop after replace;
   this alone is not a correctness bug.

4. Migration (mm/migrate.c:583 __folio_migrate_mapping):
   folio_ref_freeze(old), newfolio mapping/index setup, xas_store(new),
   folio_ref_unfreeze(old). Flag is not cleared on the old folio here.
   Verify the old folio's final allocator release still sees its tag,
   including cross-zone migration and delayed remaining references.
   Separate huge migration helper at mm/migrate.c:739; board's no-ref
   whitelist excludes large folios but the generic allocator is shared.

5. Truncate and exceptional entries (mm/truncate.c:44,109,498):
   XArray entries may be removed or replaced. Explicitly distinguish
   an exceptional/shadow entry (not a folio) from actual folio deletion;
   no arbitrary grep-based assumption that xas_store(NULL) always frees
   a page. DIO, hole punch and mmap-write interleavings remain open.

6. Collapse/split (mm/khugepaged.c:2616 and mm/huge_memory.c:4146):
   collapse inserts a new large folio, and split changes folio orders
   and indices. Production SM-T311 no-ref whitelist requires order0,
   but old marked order0 folios can be removed by collapse; verify
   defer on their final release and no bit reuse by split.

7. Shmem (mm/shmem.c:954 shmem_delete_from_page_cache):
   SM-T311 reference-free read path excludes shmem; still verify generic
   page allocator and swap interactions because deferred RCU free is
   a kernel-wide facility.

8. Allocator (mm/page_alloc.c:1576 rcu_free_defer):
   The final free of a tagged folio clears PG_rcu_free and queues
   deferred free. No page allocator reuse until a sufficient RCU
   grace period for existing readers. Also study batched workqueue,
   synchronize_rcu_expedited, pressure accounting and memory offline.

Potential ABA schedule requiring explicit proof:
   Reader R1 loads physical address A when cache slot contains A.
   A is unmarked and can be removed/freed (ordinary allocator path).
   Writer reuses physical address A for a new folio at same XArray
   index. It may become HOT and acquire PG_rcu_free before reader R2.
   Reader R2 sees tag, R4 sees same pointer address A after reinsert.
   Is this benign concurrent file content change, or can reader observe
   incorrect data/size/metadata? Answer requires real LKMM and full
   writer/reader synchronization review. Toy G653 model detects address
   generation change but is NOT proof of a kernel bug.

Marked-folio protected schedule:
   R1 sees A already marked, writer removes A, last put attempts free,
   allocator waits for RCU grace. A cannot be recycled until R7.
   R4 either sees a different slot and aborts, or reads old A while it
   remains alive. This is only an intended invariant; prove with LKMM
   and actual code, do not assert verified merely from this timeline.

Observed experiment scope:
   Physical production G650 RCU OFF; 15 located writer/reader sites,
   nine source structure assertions; abstract 120 event shuffles with
   negative controls. No herd7 installed in tested Debian workspace.
   S1 remains ACTIVE; S2 remains BLOCKED; HighMem RCU ON NOT SAFE TO
   SHIP based on this document.

Sources:
   Linux experimental Linus branch noref-filemap-read-with-llm 2026-10-10
   Kiryl / Linus branch rcu-free-v3 2026-10-13; Kiryl Oct19 18 RFC.
   Full owner-facing plan in ledger SMT311_LINUS_KIRYL_PLAN_G653.md.

Mandatory continuation protocol: every SM-T311 project report must repeat
DONE, ACTIVE/BLOCKED, NEXT, PHYSICAL BOOT status S0-S7 and remind itself
to repeat the whole table in the following report.
