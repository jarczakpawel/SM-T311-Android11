#!/usr/bin/env python3
"""G712: migration XArray publication before late folio flag transfer.

Host-only exact-source checks and bounded sequential model. NEVER a
kernel-memory-model proof or a patch recommendation.
"""
import json
from pathlib import Path
from itertools import combinations
from hashlib import sha256

K = Path(__file__).resolve().parents[4]
P = K.parent
paths = [
    "mm/migrate.c", "mm/filemap.c", "mm/page_alloc.c",
    "fs/ext4/inode.c", "mm/migrate_device.c", "fs/aio.c"
]
src = {f: (K / f).read_text() for f in paths}
prev = json.loads((K / "Documentation/mm/sm-t311-rcu-migration-g711.json").read_text())

def body(s, start, next_marker):
    return s[s.index(start):s.index(next_marker, s.index(start))]

def source_gates(s):
    m, f, a, e, dev, aio = (s[x] for x in paths)
    migrate = body(m, "static int __migrate_folio(", "\nint migrate_folio(")
    replace = body(m, "static int __folio_migrate_mapping(", "\nint folio_migrate_mapping(")
    flags = body(m, "void folio_migrate_flags(", "EXPORT_SYMBOL(folio_migrate_flags)")
    buffers = body(m, "static int __buffer_migrate_folio(", "\nint buffer_migrate_folio(")
    return {
        "G711_previous_source_and_SC_gates": all(prev["checks"].values()) and all(prev["source_mutations_detected"].values()),
        "ext4_three_regular_pagecache_mapping_variants": all(
            marker in e for marker in (
                "static const struct address_space_operations ext4_aops",
                "static const struct address_space_operations ext4_journalled_aops",
                "static const struct address_space_operations ext4_da_aops")),
        "ext4_ordered_writeback_use_buffer_migrate_folio": e.count(".migrate_folio\t\t= buffer_migrate_folio,") == 2,
        "ext4_journal_data_uses_norefs_buffer_migration": ".migrate_folio\t\t= buffer_migrate_folio_norefs," in e,
        "buffer_migrate_no_buffers_dispatches_migrate_folio": "if (!head)\n\t\treturn migrate_folio(mapping, dst, src, mode);" in buffers,
        "buffer_migrate_with_buffers_dispatches_filemap_migrate_folio": "rc = filemap_migrate_folio(mapping, dst, src, mode);" in buffers,
        "buffer_migration_has_extra_refcount_and_buffer_lock_criteria": (
            "folio_ref_count(src) != expected_count" in buffers and
            "buffer_migrate_lock_buffers(head, mode)" in buffers and
            "if (check_refs)" in buffers),
        "data_copied_before_xarray_replacement_call": migrate.index(
            "folio_mc_copy(dst, src)") < migrate.index("__folio_migrate_mapping(mapping, dst, src, expected_count)"),
        "flag_transfer_after_XArray_replacement_call": migrate.index(
            "__folio_migrate_mapping(mapping, dst, src, expected_count)") < migrate.index("folio_migrate_flags(dst, src)"),
        "XArray_store_done_under_irq_lock": (
            "xas_lock_irq(&xas);" in replace and "xas_store(&xas, newfolio);" in replace
            and "xas_unlock(&xas);" in replace and replace.index("xas_lock_irq(&xas);") < replace.index("xas_store(&xas, newfolio);") < replace.index("xas_unlock(&xas);")),
        "migration_flags_must_not_be_described_as_atomic_XArray_transaction": (
            "folio_migrate_flags(dst, src)" not in replace
            and "folio_migrate_flags(dst, src)" in migrate),
        "migration_does_not_explicitly_copy_PG_rcu_free": (
            "PG_rcu_free" not in flags and "__PG_RCU_FREE" not in flags
            and "folio_set_rcu_free" not in flags),
        "migration_device_has_same_post_publication_flag_copy": (
            "r = folio_migrate_mapping(mapping, newfolio, folio, extra_cnt);" in dev
            and "folio_migrate_flags(newfolio, folio);" in dev),
        "aio_has_later_flags_transfer_under_completion_lock": (
            aio.index("rc = folio_migrate_mapping(mapping, dst, src, 1);") <
            aio.index("folio_migrate_flags(dst, src);")
            and "spin_lock_irqsave(&ctx->completion_lock, flags);" in aio),
        "RCU_raw_reader_checks_tag_before_XArray_reload": (
            f.index("READ_ONCE(folio->flags.f) & __PG_RCU_FREE") < f.index("if (xas_reload(xas) != folio)")),
        "tagging_ordinary_path_has_key_guard": (
            "if (!lockless_read_enabled())" in f and
            "folio_test_set_rcu_free(folio)" in f),
        "old_tagged_source_deferred_but_new_unmarked_folio_is_not": (
            "if (unlikely(folio_test_rcu_free(folio))" in a and
            "rcu_free_defer(folio)" in a),
        "staged_G692_setter_denies_rcu_ON": (
            "if (val)\n\t\treturn -EOPNOTSUPP;" in f and
            "static DEFINE_STATIC_KEY_FALSE(lockless_read_key)" in f),
        "normal_refcounted_get_then_reload": (
            "folio_try_get(folio)" in f and
            "folio != xas_reload(&xas)" in f),
        "G700_host_binary_sha_kept": sha256(
            (P / "artifacts/G700-RCU-OFF-DEBUGFS-HOST-ONLY/BOOT-G700-RCU-OFF-DEBUGFS-HOST-ONLY-DO-NOT-FLASH.img").read_bytes()
        ).hexdigest() == "aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f",
    }

R = ["enter", "load", "flag", "reload", "exit"]
M = ["copy_data", "freeze_source", "publish_destination", "transfer_flags", "release_source"]

def order_model(mode):
    # Five R and five migration milestones. The reader may see old P0
    # or new P1 at the same XArray slot; it tests a tag and reloads.
    # This models the *transient publication window*, NOT future PFN ABA.
    counts = {"total_interleavings":0, "reader_saw_old":0,
              "reader_saw_new_untagged":0, "reader_saw_new_tagged":0,
              "transient_flag_missing_at_published_dst":0,
              "safe_fallback_on_untagged":0, "unsafe_new_tagged_before_data_ready":0,
              "witness_untagged":None}
    for slots in combinations(range(10),5):
        rs=set(slots)
        ri=mi=0
        published=False
        data_ready=False
        dst_tag=False
        snapshot="none"
        for i in range(10):
            if i in rs:
                step=R[ri];ri+=1
                if step=="load":
                    snapshot="new" if published else "old"
                elif step=="flag":
                    if snapshot=="new":
                        if dst_tag:
                            counts["reader_saw_new_tagged"]+=1
                            if not data_ready:
                                counts["unsafe_new_tagged_before_data_ready"]+=1
                        else:
                            counts["reader_saw_new_untagged"]+=1
                            counts["safe_fallback_on_untagged"]+=1
                            if counts["witness_untagged"] is None:
                                counts["witness_untagged"]="reader_loaded_new_between_publish_and_tag"
                    elif snapshot=="old":
                        counts["reader_saw_old"]+=1
            else:
                step=M[mi];mi+=1
                if step=="copy_data":
                    data_ready=True
                elif step=="publish_destination":
                    published=True
                    if mode=="prepublish_tag":
                        dst_tag=True
                elif step=="transfer_flags" and mode=="late_tag":
                    dst_tag=True
                if published and not dst_tag:
                    counts["transient_flag_missing_at_published_dst"]+=1
        counts["total_interleavings"]+=1
    return counts

def main():
    checks=source_gates(src)
    cases = {
        "as_built_no_tag_transfer":order_model("never"),
        "hypothetical_late_tag_copy":order_model("late_tag"),
        "hypothetical_prepublication_tag":order_model("prepublish_tag"),
    }
    checks.update({
        "all_252_source_interleavings": all(x["total_interleavings"]==252 for x in cases.values()),
        "actual_destination_can_be_published_untagged": cases["as_built_no_tag_transfer"]["reader_saw_new_untagged"]>0,
        "late_copy_has_published_untagged_window": cases["hypothetical_late_tag_copy"]["reader_saw_new_untagged"]>0,
        "late_copy_only_marks_after_data_copy": cases["hypothetical_late_tag_copy"]["unsafe_new_tagged_before_data_ready"]==0,
        "prepublish_copy_eliminates_tag_absence_under_SC_model": cases["hypothetical_prepublication_tag"]["reader_saw_new_untagged"]==0,
        "prepublish_tag_happens_after_data_ready_in_model": cases["hypothetical_prepublication_tag"]["unsafe_new_tagged_before_data_ready"]==0,
        "as_built_no_read_new_tag_from_migration": cases["as_built_no_tag_transfer"]["reader_saw_new_tagged"]==0,
    })
    fixtures={
        "omit_XArray_store":("mm/migrate.c", "xas_store(&xas, newfolio);", "xas_store(&xas, NULL);"),
        "omit_late_folio_flag_copy":("mm/migrate.c", "folio_migrate_flags(dst, src);", "folio_put(dst);"),
        "remove_ext4_journalled_callback":("fs/ext4/inode.c", ".migrate_folio\t\t= buffer_migrate_folio_norefs,", ".migrate_folio\t\t= buffer_migrate_folio,"),
        "disable_refcounted_fallback":("mm/filemap.c", "if (!folio_try_get(folio))", "if (!folio_test_uptodate(folio))"),
        "permit_experimental_RCU_enable":("mm/filemap.c", "if (val)\n\t\treturn -EOPNOTSUPP;", "if (val)\n\t\treturn 0;"),
        "remove_migration_buffer_lock":("mm/migrate.c", "if (!buffer_migrate_lock_buffers(head, mode))", "if (!folio_test_locked(src))"),
    }
    mutants={}
    for name,(path,before,after) in fixtures.items():
        if before not in src[path]:
            raise RuntimeError("missing mutation fixture "+name)
        changed=dict(src)
        changed[path]=src[path].replace(before,after)
        try:
            mutants[name]=not all(source_gates(changed).values())
        except (ValueError, AssertionError):
            mutants[name]=True
    checks["all_six_source_mutations_rejected"]=len(mutants)==6 and all(mutants.values())
    report={
        "stage":"G712",
        "checks":checks,
        "mutated_source_controls":mutants,
        "publication_model":cases,
        "established_source_order":"folio_mc_copy before __folio_migrate_mapping XArray publish, then folio_migrate_flags (not atomic with slot publish)",
        "ext4_pagecache_migration":"regular+delalloc buffer_migrate_folio; journalled buffer_migrate_folio_norefs; both through filemap_migrate_folio and later flag transfer",
        "critical_limits":[
            "The 252 schedule toy model deliberately represents ONE reader and ONE migration writer; it does not test subsequent destination eviction/reuse modeled in G711.",
            "A tag checked as zero triggers slow/refcounted fallback; the source-level publication window alone is NOT proof of data corruption.",
            "Prepublish tagging is not authorized: requires correct data and lifetime publication ordering, all filesystem callbacks, lock constraints, memory pressure and ARMv7/LKMM proof.",
            "No-ref RCU stays fail-closed; G700 remains host-only and no physical BOOT/recovery was modified."
        ],
        "candidate_release_decision":"KEEP_G700_OFF_AND_SLOW_REFCOUNTED_FALLBACK",
        "migration_tag_patch_approved":False,
        "RCU_no_ref_ON_approved":False,
        "S1_LKMM_closed":False,"S2_PFN_ABA_closed":False,"S3_ARMv7_cache_closed":False,
        "G700_flashed":False,
        "new_runtime_kernel_C_change":False
    }
    print(json.dumps(report,ensure_ascii=False,indent=2))
    if not all(checks.values()):
        raise SystemExit(1)

if __name__=="__main__":
    main()
