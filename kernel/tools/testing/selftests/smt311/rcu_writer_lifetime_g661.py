#!/usr/bin/env python3
import json
from pathlib import Path

root = Path(__file__).resolve().parents[4]
filemap = (root / "mm/filemap.c").read_text()
migrate = (root / "mm/migrate.c").read_text()
truncate = (root / "mm/truncate.c").read_text()
page_alloc = (root / "mm/page_alloc.c").read_text()
huge = (root / "mm/huge_memory.c").read_text()
failure = (root / "mm/memory-failure.c").read_text()
inode = (root / "fs/inode.c").read_text()
fs = (root / "include/linux/fs.h").read_text()

def section(source, begin, end):
    return source.split(begin, 1)[1].split(end, 1)[0]

def source_checks(filemap_source=filemap, migrate_source=migrate, page_alloc_source=page_alloc):
    single = section(filemap_source, "static void page_cache_delete(", "static void filemap_unaccount_folio(")
    batch = section(filemap_source, "static void page_cache_delete_batch(", "void delete_from_page_cache_batch(")
    replace = section(filemap_source, "void replace_page_cache_folio(", "EXPORT_SYMBOL_GPL(replace_page_cache_folio)")
    add = section(filemap_source, "noinline int __filemap_add_folio(", "EXPORT_SYMBOL_GPL(__filemap_add_folio)")
    reader = section(filemap_source, "static struct folio *filemap_read_rcu_lookup(", "static size_t filemap_read_rcu_size(")
    mark = section(filemap_source, "static noinline void filemap_mark_hot(", "void filemap_read_accessed(")
    migration = section(migrate_source, "static int __folio_migrate_mapping(", "int folio_migrate_mapping(")
    migrate_flags = section(migrate_source, "void folio_migrate_flags(", "EXPORT_SYMBOL(folio_migrate_flags)")
    last_put = section(migrate_source, "static void migrate_folio_done(", "static int migrate_folio_unmap(")
    final = section(truncate, "void truncate_inode_pages_final(", "EXPORT_SYMBOL(truncate_inode_pages_final)")
    recycler = section(page_alloc_source, "static __always_inline bool __free_pages_prepare(", "static bool free_pages_prepare(")
    free = section(page_alloc_source, "static unsigned long rcu_free_flush(", "static void rcu_free_workfn(")
    work = section(page_alloc_source, "static void rcu_free_workfn(", "static void rcu_free_expedite_workfn(")
    split = section(huge, "static void __split_folio_to_order(", "static int __split_unmapped_folio(")

    checks = {
        "readside_unlocked_speculative_xarray": "folio = xas_load(xas);" in reader and "xa_lock" not in reader,
        "readside_flag_before_size_before_slot": "xas_reload(xas) != folio" in reader and reader.index("__PG_RCU_FREE") < reader.index("i_size_read(inode)") < reader.index("xas_reload(xas) != folio"),
        "readside_mapping_and_normal_zone_after_slot": "xas_reload(xas) != folio" in reader and reader.index("xas_reload(xas) != folio") < reader.index("folio_zonenum(folio) != ZONE_NORMAL") < reader.index("READ_ONCE(folio->mapping) != mapping"),
        "single_delete_tag_clear_only_on_exiting": "folio_test_rcu_free(folio) && mapping_exiting(mapping)" in single and "folio_clear_rcu_free(folio)" in single,
        "single_delete_slot_changed_before_mapping_null": single.index("xas_store(&xas, shadow)") < single.index("folio->mapping = NULL"),
        "batch_delete_exiting_tag_exception": "folio_test_rcu_free(folio) && mapping_exiting(mapping)" in batch and "folio_clear_rcu_free(folio)" in batch,
        "batch_delete_null_slot_while_locked": "folio->mapping = NULL;" in batch and "xas_store(&xas, NULL)" in batch,
        "replacement_slot_under_xarray_lock": replace.index("xas_lock_irq(&xas)") < replace.index("xas_store(&xas, new)") < replace.index("xas_unlock_irq(&xas)"),
        "replacement_old_last_put_after_slot_unlock": replace.index("xas_unlock_irq(&xas)") < replace.index("folio_put(old)"),
        "addition_checks_conflicts_with_xarray_lock": add.index("xas_lock_irq(&xas)") < add.index("xas_for_each_conflict(&xas, entry)") < add.index("xas_store(&xas, folio)"),
        "migration_freeze_before_xarray_replace": "folio_ref_freeze(folio, expected_count)" in migration and migration.index("folio_ref_freeze(folio, expected_count)") < migration.index("xas_store(&xas, newfolio)"),
        "migration_unfreeze_after_replacement": migration.index("xas_store(&xas, newfolio)") < migration.index("folio_ref_unfreeze(folio, expected_count - nr)"),
        "migrated_new_folio_not_assumed_tagged": "folio_set_rcu_free(" not in migrate_flags and "folio_test_rcu_free(" not in migrate_flags,
        "migration_old_page_last_put_exists": "folio_put(src)" in last_put,
        "final_truncate_sets_exiting_and_cycles_lock": final.index("mapping_set_exiting(mapping)") < final.index("xa_lock_irq(&mapping->i_pages)") < final.index("truncate_inode_pages(mapping, 0)"),
        "inode_destroy_after_mapping_teardown": "xa_destroy(&inode->i_data.i_pages)" in inode,
        "folio_mark_tag_and_mapping_bit": "folio_test_set_rcu_free(folio)" in mark and "mapping_set_rcu_read(mapping)" in mark,
        "deferred_free_flag_cleared_before_enqueue": recycler.index("__folio_clear_rcu_free(folio)") < recycler.index("rcu_free_defer(folio)") < recycler.index("return false;"),
        "expedited_free_grace_before_recycling": "synchronize_rcu_expedited()" in free and free.index("synchronize_rcu_expedited()") < free.index("rcu_free_pages(list)"),
        "work_handler_recycles_after_rcu_work_callback": "to_rcu_work(work)" in work and "rcu_free_pages(&done)" in work,
        "split_propagates_rcu_bit_before_untail": "__PG_RCU_FREE" in split and split.index("__PG_RCU_FREE") < split.index("smp_wmb()") < split.index("clear_compound_head(new_head)"),
        "hwpoison_release_waits_for_grace": "if (release && folio_test_rcu_free(page_folio(page)))\n\t\tsynchronize_rcu();" in failure,
        "hwpoison_unmap_waits_for_grace": "if (!res && folio_test_rcu_free(folio))\n\t\tsynchronize_rcu();" in failure,
        "arm32_isize_seqcount": "read_seqcount_retry(&inode->i_size_seqcount, seq)" in fs,
    }
    return checks

checks = source_checks()
if not all(checks.values()):
    raise SystemExit("G661 source safety anchors failed: " + ", ".join(k for k, v in checks.items() if not v))

negative = {
    "remove_reader_slot_reload": not all(source_checks(filemap_source=filemap.replace("xas_reload(xas) != folio", "", 1)).values()),
    "remove_expedited_grace": not all(source_checks(page_alloc_source=page_alloc.replace("synchronize_rcu_expedited();", "", 1)).values()),
    "remove_migration_freeze": not all(source_checks(migrate_source=migrate.replace("folio_ref_freeze(folio, expected_count)", "")).values()),
}
if not all(negative.values()):
    raise SystemExit("G661 negative controls failed")
result = {
    "label": "G661 source-derived writer/reader lifetime graph, NOT proof of same-address ABA safety",
    "kernel_runtime_base": "a2fca4558",
    "checks_pass": len(checks),
    "checks": checks,
    "negative_controls": negative,
    "constraints": [
        "The initial XArray reader does not take xa_lock; writer-side xa_lock does not serialize its first load.",
        "A folio unmarked before first read can be freed and reallocated before the speculative flag test; same-address generation confusion is not ruled out by this audit.",
        "Marked old folios take RCU deferred-free routes unless a special lifetime exception is exercised; exact coverage still requires manual and formal proof.",
        "folio_migrate_flags does not transfer PG_rcu_free to the new folio; old flagged folio still needs deferred lifetime coverage.",
        "Source-string checks are not a complete callgraph, allocator provenance model, or proof of correct memory ordering.",
        "S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4-6 BLOCKED, S7 PARTIAL; physical RCU ON remains prohibited."
    ]
}
(root / "Documentation/mm/sm-t311-writer-lifetime-g661.json").write_text(json.dumps(result, indent=2) + "\n")
print(f"G661 reader/writer source graph: {len(checks)}/{len(checks)} PASS, {len(negative)} diagnostics; S1 ACTIVE S2 BLOCKED")
