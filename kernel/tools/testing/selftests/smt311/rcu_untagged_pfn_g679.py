#!/usr/bin/env python3
"""G679 auditable real-source PFN provenance obligations for ref-free filemap.

Tests real source regions, their order and deliberate mutated source inputs.
This is NOT a folio-lifetime/RCU proof, and no PFN or UAF was physically
observed. Findings can only support keeping S2/S3 BLOCKED.
"""
from pathlib import Path
import json

KERNEL = Path(__file__).resolve().parents[4]
OUT = KERNEL / "Documentation/mm/sm-t311-rcu-pfn-provenance-g679.json"
PATHS = ("mm/filemap.c", "mm/page_alloc.c", "mm/migrate.c",
         "mm/memory-failure.c", "mm/huge_memory.c",
         "include/linux/page-flags.h", "kernel/workqueue.c")
SRC = {path:(KERNEL/path).read_text() for path in PATHS}


def region(s, a, b):
    if a not in s or b not in s.split(a, 1)[-1]:
        raise AssertionError("missing source region " + a[:60])
    return s.split(a, 1)[1].split(b, 1)[0]


def ordered(s, *tokens):
    offsets=[s.find(t) for t in tokens]
    return all(p>=0 for p in offsets) and offsets == sorted(offsets) and len(set(offsets))==len(offsets)


def gates(src):
    f,a,m,h,sp,flags,wq=(src[k] for k in PATHS)
    lookup=region(f, "static struct folio *filemap_read_rcu_lookup(", "static size_t filemap_read_rcu_size(")
    copy=region(f, "static size_t filemap_read_rcu_copy(", "/* Bytes one RCU read section")
    batch=region(f, "static bool filemap_read_rcu_batch(", "size_t filemap_read_rcu(")
    mark=region(f, "static noinline void filemap_mark_hot(", "void filemap_read_accessed(")
    single=region(f, "static void page_cache_delete(", "static void filemap_unaccount_folio(")
    multiple=region(f, "static void page_cache_delete_batch(", "void delete_from_page_cache_batch(")
    replace=region(f, "void replace_page_cache_folio(", "EXPORT_SYMBOL_GPL(replace_page_cache_folio);")
    add=region(f, "noinline int __filemap_add_folio(", "int filemap_add_folio(")
    migrate=region(m, "static int __folio_migrate_mapping(", "/*\n * Copy the flags")
    mapped_migrate=migrate.split("oldzone = folio_zone(folio);",1)[1]
    migrflags=region(m, "void folio_migrate_flags(", "EXPORT_SYMBOL(folio_migrate_flags);")
    lastfree=region(a, "static __always_inline bool __free_pages_prepare(", "trace_mm_page_free(page, order);")
    poison=region(h, "static bool page_handle_poison(", "static hwpoison_filter_func_t")
    mf=region(h, "int memory_failure(unsigned long pfn, int flags)", "#define MEMORY_FAILURE_FIFO_ORDER")
    split=region(sp, "new_folio->flags.f |= (folio->flags.f &", "new_folio->mapping = folio->mapping;")
    prepare=f.split("static bool filemap_read_rcu_can_copy(",1)[1].split("/*\n * Return the folio at",1)[0]
    enabled=region(f, "static int lockless_read_set(", "DEFINE_DEBUGFS_ATTRIBUTE(")
    have={
      "no_ref_reader_disabled_by_default":
          "static DEFINE_STATIC_KEY_FALSE(lockless_read_key);" in f and
          "lockless_read_enabled()" in f.split("size_t filemap_read_rcu(",1)[1].split("ssize_t filemap_read(",1)[0],
      "reader_under_rcu_and_pagefault_disabled":
          ordered(batch,"rcu_read_lock();","mapping_writably_mapped(mapping)",
                  "pagefault_disable();","filemap_read_rcu_lookup(",
                  "filemap_read_rcu_copy(", "pagefault_enable();") and
          batch.rfind("rcu_read_unlock();") > batch.find("pagefault_enable();"),
      "reader_does_not_hold_reference":
          "folio_try_get(" not in lookup and "folio_try_get(" not in batch,
      "lookup_raw_pointer_then_flag_then_slot":
          ordered(lookup,"folio = xas_load(xas);","READ_ONCE(folio->flags.f)",
                  "i_size_read(inode)","xas_reload(xas) != folio",
                  "READ_ONCE(folio->mapping) != mapping"),
      "reader_only_order0_normal":
          "folio_test_large(folio)" in lookup and
          "folio_zonenum(folio) != ZONE_NORMAL" in lookup and
          "folio_test_large(folio)" in prepare,
      "reader_data_copy_in_rcu_window":
          "_copy_to_iter(kaddr + offset, len, iter);" in copy and
          "filemap_read_rcu_copy(" in batch,
      "no_runtime_generation_oracle":
          "generation" not in lookup and "generation" not in batch,
      "mark_hot_requires_ext4_normal_order0":
          all(v in mark for v in ("EXT4_SUPER_MAGIC","ZONE_NORMAL",
                                  "folio_test_large(folio)","folio_test_set_rcu_free(folio)")),
      "mark_hot_sets_flag_under_reference":
          "filemap_read_accessed(" in f and "filemap_mark_hot(mapping, folio);" in f and
          "folio_test_set_rcu_free(folio)" in mark,
      "single_inode_exit_keeps_flag_in_experiment":
          "!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&" in single,
      "batch_inode_exit_keeps_flag_in_experiment":
          "!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&" in multiple,
      "tagged_last_put_defers_free":
          ordered(lastfree, "folio_test_rcu_free(folio)", "__folio_clear_rcu_free(folio);",
                  "rcu_free_defer(folio);","return false;"),
      "poison_exception_no_normal_recycle":
          "!(PageHWPoison(page) && !order)" in lastfree and
          "PageHWPoison(page)" in a,
      "poison_release_waits_gp":
          ordered(poison,"folio_test_rcu_free(page_folio(page))","synchronize_rcu();",
                  "SetPageHWPoison(page);","put_page(page);"),
      "memory_failure_waits_rcu":
          "if (!res && folio_test_rcu_free(folio))" in mf and
          "synchronize_rcu();" in mf,
      "migrate_slot_lock_and_freeze_order":
          ordered(mapped_migrate,"xas_lock_irq(&xas);","folio_ref_freeze(folio, expected_count)",
                  "xas_store(&xas, newfolio);", "folio_ref_unfreeze(folio, expected_count - nr)",
                  "xas_unlock(&xas);"),
      "migrate_does_not_transfer_tag":
          "folio_set_rcu_free" not in migrflags and
          "PG_rcu_free" not in migrflags,
      "replace_xarray_before_old_put":
          ordered(replace,"xas_lock_irq(&xas);","xas_store(&xas, new);",
                  "xas_unlock_irq(&xas);","folio_put(old);"),
      "add_new_mapping_index_before_publish":
          ordered(add,"folio->mapping = mapping;","folio->index = xas.xa_index;",
                  "xas_lock_irq(&xas);"),
      "large_split_transfers_pg_rcu_free":
          "__PG_RCU_FREE |" in split and "smp_wmb();" in sp,
      "reader_never_accepts_large_split":
          "folio_test_large(folio)" in lookup and
          "#define FILEMAP_RCU_READ_MAX\tSZ_32K" in f,
      "flag_bit_definition":
          "#define __PG_RCU_FREE (1UL << PG_rcu_free)" in flags,
      "gp_callback_queues_only_after_grace":
          "call_rcu_hurry(&rwork->rcu, rcu_work_rcufn);" in wq and
          "__queue_work(WORK_CPU_UNBOUND, rwork->wq, &rwork->work);" in wq,
      "debugfs_activation_guard_not_global_proof":
          'of_machine_is_compatible("samsung,t311")' in enabled and
          "SM_T311_RCU_FREE_RESERVE" in enabled,
    }
    return have

def mutate_sources(original):
    needles={
     "remove_reader_rcu_lock":("mm/filemap.c","rcu_read_lock();\n#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT","#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT"),
     "remove_reader_mapping_check":("mm/filemap.c","READ_ONCE(folio->mapping) != mapping","0 /* deleted mapping comparison */"),
     "remove_single_inode_tag_preservation":("mm/filemap.c","\tif (!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&","\tif ("),
     "remove_batch_inode_tag_preservation":("mm/filemap.c","\t\tif (!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&","\t\tif ("),
     "remove_allocator_defer":("mm/page_alloc.c","rcu_free_defer(folio);",""),
     "remove_hwpoison_grace":("mm/memory-failure.c","synchronize_rcu();",""),
     "remove_migration_freeze":("mm/migrate.c","folio_ref_freeze(folio, expected_count)","false /* removed freeze */"),
     "remove_large_split_tag_copy":("mm/huge_memory.c","__PG_RCU_FREE |",""),
     "remove_xarray_replace_publication":("mm/filemap.c","xas_store(&xas, new);",""),
    }
    failures={}
    for label,(filename,needle,altered) in needles.items():
        assert needle in original[filename],label
        parts=dict(original)
        if label == "remove_reader_mapping_check":
            prefix,region_part=parts[filename].split(
                "static struct folio *filemap_read_rcu_lookup(",1)
            assert needle in region_part
            parts[filename]=prefix+"static struct folio *filemap_read_rcu_lookup("+region_part.replace(needle,altered,1)
        elif label == "remove_migration_freeze":
            prefix,region_part=parts[filename].split("oldzone = folio_zone(folio);",1)
            assert needle in region_part
            parts[filename]=prefix+"oldzone = folio_zone(folio);"+region_part.replace(needle,altered,1)
        else:
            parts[filename]=parts[filename].replace(needle,altered,1)
        found=[k for k,v in gates(parts).items() if not v]
        if not found:
            raise AssertionError("mutant escaped: "+label)
        failures[label]=found
    return failures

def main():
    results=gates(SRC)
    failed=[k for k,v in results.items() if not v]
    if failed:
        raise AssertionError("G679 source anchored gate failures: "+repr(failed))
    rejected=mutate_sources(SRC)
    assert len(results)==24
    assert len(rejected)==9
    out={
      "stage":"G679", "type":"source-anchored PFN reuse/provenance obligations",
      "source_checks":results, "source_checks_passed":len(results),
      "actual_changed_source_controls_rejected":rejected,
      "evidence_scope":"source of this kernel HEAD; no runtime kernel execution or PFN trace",
      "key_conclusions":[
        "The old unmarked page A MAY be reclaimed and reused within the reader RCU critical section: no grace is promised for A.",
        "The reader relies on the tag sampled after obtaining raw numeric pointer; it does not compare an allocation generation.",
        "If a tagged reuser B is observed, B must not be recycled until reader GP: G674 preserves tag at final inode eviction; allocator normally defers B last put.",
        "The order0 Normal filter makes large-folio split outside the SM-T311 experimental fastpath; migration can replace XArray slot without transferring the flag to new folio.",
        "Hwpoison has dedicated RCU synchronization and quarantines poisoned pages; this is not an all-writers proof.",
        "No full formal proof of ARMv7 cache, allocator PFN ABA, ref-free folio contents consistency or all exceptional writers exists."
      ],
      "remaining_gaps":[
        "No general generation token in XArray+folio lookup to distinguish A and C under same numeric PFN pointer.",
        "No exhaustively validated publication/removal memory ordering for every migration, replace, truncate, ext4 mmap or hwpoison path.",
        "No physical ON/ABBA test, and no verified normal-zone reclaim/OOM bound for G674's extended tag lifetime.",
      ],
      "deployment": {
        "physical_source_commit":"a2fca4558", "physical_boot":"G650",
        "staged_executable_files":["mm/filemap.c","mm/page_alloc.c"],
        "rcu_default_on":False,
        "s1":"ACTIVE","s2":"BLOCKED","s3":"BLOCKED"
      }
    }
    OUT.write_text(json.dumps(out,indent=2,ensure_ascii=False)+"\n")
    print(f"G679 real-source PFN-provenance gates {len(results)}/24 PASS; {len(rejected)}/9 actual source-mutant controls rejected")
    print("Untagged donor A may recycle, tagged B lifetime relies on GP; full generation/lifetime S2 remains BLOCKED")

if __name__=="__main__":
    main()
