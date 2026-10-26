#!/usr/bin/env python3
"""G653 reproducible writer-site inventory for Kiryl's HOT folio RCU design.

Collect exact source line numbers and inspect program-order anchors. The script
must *never* label this source scan a complete concurrency proof. It does not
enable RCU and cannot modify the device.
"""
from pathlib import Path
import json

K = Path(__file__).resolve().parents[4]
OUT = K / "Documentation/mm/sm-t311-rcu-writer-inventory.json"

SITES = [
  ("hot_flag_writer", "mm/filemap.c",
   "if (folio_test_set_rcu_free(folio))", "tagging under referenced HOT read", "verify reader/writer flag->slot ordering under LKMM"),
  ("xarray_remove_single", "mm/filemap.c",
   "static void page_cache_delete(", "removal with inode-exit flag clearing", "prove inode eviction cannot overlap an RCU reader"),
  ("xarray_remove_batch", "mm/filemap.c",
   "static void page_cache_delete_batch(", "bulk removal clears mapping on folios", "prove no late reference-free reader after delete"),
  ("pagecache_replace", "mm/filemap.c",
   "void replace_page_cache_folio(", "old mapped folio replaced by new folio", "old tagged folio must defer its allocator reuse"),
  ("filemap_add", "mm/filemap.c",
   "noinline int __filemap_add_folio(", "slot insertion and generation", "audit ABA for unmarked old slot donor"),
  ("migration_base", "mm/migrate.c",
   "static int __folio_migrate_mapping(", "folio_ref_freeze and xas_store newfolio", "verify old tag lifetime and new generation ordering"),
  ("migration_huge", "mm/migrate.c",
   "int migrate_huge_page_move_mapping(", "huge migration slot replacement", "does board allow this fast read (order0 only)?"),
  ("truncate_final", "mm/truncate.c",
   "void truncate_inode_pages_final(", "inode eviction and final truncate", "prove mapping_exiting writer synchronization"),
  ("truncate_exceptional", "mm/truncate.c",
   "xas_store(&xas, NULL);", "eviction path exceptional XArray entry deletion", "check whether page rather than exceptional entry removed"),
  ("khugepaged_collapse", "mm/khugepaged.c",
   "xas_store(&xas, new_folio);", "collapsed file pages to huge folio", "ensure old tagged page final free grace interval"),
  ("huge_folio_split", "mm/huge_memory.c",
   "static int __folio_split(", "split and page-cache range indexing", "board excludes large folio fast read; check old/new flags"),
  ("shmem_remove", "mm/shmem.c",
   "static void shmem_delete_from_page_cache(", "tmpfs removal", "board excludes shmem fastpath, but page allocator logic global"),
  ("last_free_defer", "mm/page_alloc.c",
   "rcu_free_defer(folio);", "last reference drops deferred into per-CPU queue", "prove no allocator reuse before grace period"),
  ("read_lookup", "mm/filemap.c",
   "static struct folio *filemap_read_rcu_lookup(", "reader xarray load->flag->size->reload", "ARMv7 barrier and same-address reuse"),
  ("read_copy", "mm/filemap.c",
   "static size_t filemap_read_rcu_copy(", "user copy with kmap_local_page", "HighMem alias/folio lifetime and faults"),
  ("fuse_pipe_folio_steal_guard", "fs/fuse/dev.c",
   "if (folio_test_rcu_free(newfolio))", "FUSE rejects stealing a tagged folio from pipe", "prove no tagged folio becomes reused pipe-owned storage while reference-free reader exists"),
  ("hwpoison_free_grace", "mm/memory-failure.c",
   "if (release && folio_test_rcu_free(page_folio(page)))", "hwpoison free explicitly waits for RCU readers", "validate special free branch ordering and poisoned folio lifetime"),
  ("hwpoison_unmap_grace", "mm/memory-failure.c",
   "if (!res && folio_test_rcu_free(folio))", "memory failure waits for RCU before unmapping", "validate post-poison read-side safety and all early returns"),
  ("hwpoison_flush_deferred", "mm/memory-failure.c",
   "rcu_free_sync();", "memory_failure drains special deferred folios", "check absence of order0 non-deferred buddy reuse while old reader active"),
  ("large_split_tag_inheritance", "mm/huge_memory.c",
   "__PG_RCU_FREE |", "split propagates RCU-free pageflag to daughter folios", "board order0 RCU reader may see split outputs; proof needs page head and tail lifetime"),
  ("large_split_flag_publish_barrier", "mm/huge_memory.c",
   "/* Page flags must be visible before we make the page non-compound. */", "barrier protects flag visibility on split", "prove barrier pairing with RCU reader and per-page head rewrite"),
  ("shmem_fastpath_board_exclusion", "mm/shmem.c",
   "retval = 0;", "board bypasses no-ref fastpath in shmem", "verify compile guards and generic free still handles marked non-shmem pages"),
  ("folio_last_free_eligibility", "mm/page_alloc.c",
   "if (unlikely(folio_test_rcu_free(folio)) &&", "allocator free decides on page flag before returning to buddy", "review hwpoison special case and grace queue correctness")
]


def line_numbers(source, needle):
    return [i for i, line in enumerate(source.splitlines(), 1) if needle in line]


def main():
    entries = []
    for ident, relative_path, anchor, effect, question in SITES:
        file = K / relative_path
        source = file.read_text()
        hits = line_numbers(source, anchor)
        if not hits:
            raise AssertionError(f"missing required source site {ident}: {relative_path} {anchor!r}")
        entries.append({
            "id": ident, "file":relative_path, "line_numbers":hits,
            "source_anchor":anchor, "effect":effect, "still_unproven":question,
            "status":"LOCATED_NOT_FORMALLY_PROVED"
        })
    filemap = (K / "mm/filemap.c").read_text()
    allocator = (K / "mm/page_alloc.c").read_text()
    migration = (K / "mm/migrate.c").read_text()
    fusion = (K / "fs/fuse/dev.c").read_text()
    huge = (K / "mm/huge_memory.c").read_text()
    failure = (K / "mm/memory-failure.c").read_text()
    shmem = (K / "mm/shmem.c").read_text()
    fs_header = (K / "include/linux/fs.h").read_text()
    requirements={
      "reader_slot_revalidation": "xas_reload(xas) != folio" in filemap,
      "reader_armv7_barrier": filemap.count("smp_rmb();") >= 2,
      "hot_mark_under_normal_held_ref": "folio_test_set_rcu_free(folio)" in filemap,
      "inode_exit_clears_tag": "folio_test_rcu_free(folio) && mapping_exiting(mapping)" in filemap,
      "migrate_refcount_freeze": "folio_ref_freeze(folio, expected_count)" in migration,
      "migrate_writes_new_xarray_slot": "xas_store(&xas, newfolio)" in migration,
      "allocator_deferred_grace": "__folio_clear_rcu_free(folio);" in allocator and "rcu_free_defer(folio);" in allocator,
      "highmem_excluded_from_rcu_fastpath": "folio_zonenum(folio) != ZONE_NORMAL" in filemap,
      "board_rcu_default_false": "static DEFINE_STATIC_KEY_FALSE(lockless_read_key)" in filemap,
      "fuse_rejects_tagged_stolen_folio": "if (folio_test_rcu_free(newfolio))" in fusion,
      "hwpoison_free_waits_grace": "if (release && folio_test_rcu_free(page_folio(page)))" in failure and "synchronize_rcu();" in failure,
      "hwpoison_unmap_waits_grace": "if (!res && folio_test_rcu_free(folio))" in failure,
      "hwpoison_deferred_drain": "rcu_free_sync();" in failure,
      "folio_split_copies_rcu_tag": "__PG_RCU_FREE |" in huge,
      "folio_split_has_publish_wmb": "Page flags must be visible before we make the page non-compound" in huge and "smp_wmb();" in huge,
      "board_shmem_fast_rcu_excluded": "retval = 0;" in shmem and "SM-T311 explicitly restricts no-reference reads to ext4" in shmem,
      "armv7_size_is_seqcounted": "#if BITS_PER_LONG==32 && defined(CONFIG_SMP)" in fs_header and "read_seqcount_retry(&inode->i_size_seqcount, seq)" in fs_header
    }
    assert all(requirements.values()), [k for k,v in requirements.items() if not v]
    report={
      "device":"Samsung SM-T311 ARMv7 Cortex-A9",
      "kernel_runtime_source_anchor":"a2fca4558",
      "source_site_count":len(entries),
      "inventory":entries,
      "structural_requirements":requirements,
      "decision":"S1 REMAINS ACTIVE; S2 LKMM AND HIGHMEM SAFETY BLOCKED",
      "warning":"This is a path locator and source invariant scan, NOT a closed-world proof. Search can miss dynamically dispatched or architecture-dependent pagecache writers. Must conduct manual source review and formal memory-model test.",
      "priority_questions":[
        "Unmarked old folio can be recycled and a new marked folio reinserted at same physical address before stale reader sees flag: does the actual XArray/barrier sequence exclude dangerous outcomes?",
        "Is the page's mapping pointer dereference always safe during concurrent truncate/migration with PREEMPT_RCU on 32-bit Cortex-A9?",
        "Can migration/collapse or inode eviction clear PG_rcu_free before every eligible lockless reader leaves its grace period?",
        "Can kmap_local_page and cache flush be correctly used on HighMem without referenced folio lifetime?",
        "Can existing Normal zone memory policy preserve correctness without throttling its actual benefit under pressure?"
      ]
    }
    OUT.write_text(json.dumps(report, indent=2)+"\n")
    print(f"G655 SOURCE MAP PASS: {len(entries)} writer/reader sites, {len(requirements)} structural checks; NO LKMM PROOF")
    print("OUT", OUT)


if __name__ == "__main__":
    main()
