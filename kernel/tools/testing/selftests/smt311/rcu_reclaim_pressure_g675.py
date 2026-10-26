#!/usr/bin/env python3
"""G675: source-owned PG_rcu_free lifecycle + deferred queue soft-limit gate.

Source audits and arithmetic scenarios, not a runtime stress test,
data-integrity proof, or evidence of a safe RCU-enabled tablet.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "Documentation/mm/sm-t311-rcu-reclaim-pressure-g675.json"
FILES = (
    "mm/filemap.c", "mm/page_alloc.c", "mm/migrate.c",
    "mm/memory-failure.c", "mm/truncate.c", "mm/internal.h",
    "include/linux/page-flags.h", "include/linux/mmzone.h",
    "fs/fuse/dev.c", "mm/vmscan.c",
)
SRC = {name: (ROOT / name).read_text() for name in FILES}


def checks(src):
    f, pa, mig, poison, tr, internal, flags, zone, fuse, scan = (
        src[x] for x in FILES)
    deletion = f.split("static void page_cache_delete(", 1)[1].split(
        "static void filemap_unaccount_folio(", 1)[0]
    deletion_batch = f.split("static void page_cache_delete_batch(",1)[1].split(
        "void delete_from_page_cache_batch(",1)[0]
    mark = f.split("static noinline void filemap_mark_hot(",1)[1].split(
        "void filemap_read_accessed(",1)[0]
    free_prepare = pa.split("static __always_inline bool __free_pages_prepare(",1)[1].split(
        "trace_mm_page_free(page, order);",1)[0]
    queue = pa.split("static void rcu_free_defer(",1)[1].split(
        "static void rcu_free_drain(",1)[0]
    gp = pa.split("static unsigned long rcu_free_flush(",1)[1].split(
        "static void rcu_free_defer(",1)[0]
    migr_flags = mig.split("void folio_migrate_flags(",1)[1].split(
        "EXPORT_SYMBOL(folio_migrate_flags);",1)[0]
    return {
        "only_2_inode_exit_tag_clear_calls":
            len(re.findall(r"(?<!_)folio_clear_rcu_free\(",f))==2,
        "single_inode_clear_disabled_for_experiment":
            "!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&" in deletion,
        "batch_inode_clear_disabled_for_experiment":
            "!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&" in deletion_batch,
        "free_prepare_sole_raw_tag_clear":
            pa.count("__folio_clear_rcu_free(folio);")==1,
        "free_prepare_tagged_defers_before_buddy":
            free_prepare.find("__folio_clear_rcu_free(folio);")>=0 and
            free_prepare.find("rcu_free_defer(folio);") >
            free_prepare.find("__folio_clear_rcu_free(folio);"),
        "soft_mark_limit_2m":
            "SM_T311_RCU_PENDING_SOFT_LIMIT\t(SZ_2M >> PAGE_SHIFT)" in internal,
        "mark_hot_checks_soft_limit_only_before_setting_bit":
            mark.find("NR_FREE_RCU_PAGES) >=") >= 0 and
            mark.find("folio_test_set_rcu_free(folio)") >= 0 and
            mark.find("NR_FREE_RCU_PAGES) >=") <
            mark.find("folio_test_set_rcu_free(folio)"),
        "mark_only_ext4_order0_Normal":
            all(x in mark for x in ("EXT4_SUPER_MAGIC", "ZONE_NORMAL",
                                    "folio_test_large(folio)")),
        "reserve_4m":
            "SM_T311_RCU_FREE_RESERVE\t(SZ_4M >> PAGE_SHIFT)" in internal,
        "expedite_threshold_512k":
            "#define RCU_FREE_EXPEDITE\t(SZ_512K >> PAGE_SHIFT)" in pa,
        "expedite_needs_low_Normal_even_if_queue_over_128":
            "expedite = free <= high_wmark_pages(zone) + SM_T311_RCU_FREE_RESERVE;" in queue,
        "defer_accounts_every_previously_marked_folio":
            all(x in queue for x in ("NR_FREE_RCU_PAGES", "list_add_tail(&folio->lru, &r->pending)",
                                     "r->nr_pending + r->nr_batch")),
        "one_queued_grace_period_per_cpu":
            "if (r->queued || list_empty(&r->pending))" in pa and
            "queue_rcu_work(rcu_free_wq, &r->rwork)" in pa,
        "callback_clears_batch_counter":
            "r->nr_batch = 0;" in gp and "rcu_free_pages(&done);" in gp,
        "sync_drain_under_pressure":
            "rcu_free_drain();" in pa and "NR_FREE_RCU_PAGES" in pa,
        "migrate_does_not_copy_pg_rcu_free":
            "folio_set_rcu_free" not in migr_flags and
            "folio_clear_rcu_free" not in migr_flags,
        "migration_frozen_old_folio_and_releases_refs":
            "folio_ref_freeze(folio, expected_count)" in mig and
            "folio_ref_unfreeze(folio, expected_count - nr)" in mig,
        "hwpoison_waits_for_existing_flag":
            poison.count("folio_test_rcu_free(")>=2 and
            poison.count("synchronize_rcu();")>=2,
        "final_inode_exiting_then_truncate":
            tr.index("mapping_set_exiting(mapping);") <
            tr.index("truncate_inode_pages(mapping, 0);"),
        "fuse_does_not_steal_tagged_pipe_folio":
            "if (folio_test_rcu_free(newfolio))" in fuse,
        "reclaim_checks_exiting_mapping":
            "mapping_exiting(mapping)" in scan,
        "rcu_pending_counter_zone_defined":
            "NR_FREE_RCU_PAGES" in zone
    }


def mutational_controls(original, true_keys):
    cases={
        "single_eviction_guard_removed": ("mm/filemap.c",
             "\tif (!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&", 1),
        "both_eviction_guards_removed": ("mm/filemap.c",
             "!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&", 2),
        "allocator_defer_removed": ("mm/page_alloc.c", "rcu_free_defer(folio);", 1),
        "mark_soft_limit_removed": ("mm/filemap.c",
             "zone_page_state(zone, NR_FREE_RCU_PAGES) >=",1),
        "expedite_512k_changed": ("mm/page_alloc.c",
             "#define RCU_FREE_EXPEDITE\t(SZ_512K >> PAGE_SHIFT)",1),
        "migration_ref_freeze_removed": ("mm/migrate.c",
             "folio_ref_freeze(folio, expected_count)",2),
        "hwpoison_wait_removed": ("mm/memory-failure.c",
             "synchronize_rcu();",2),
    }
    rejected={}
    for label,(filename,pattern,count) in cases.items():
        changed=dict(original)
        assert changed[filename].count(pattern)>=count,(label,pattern)
        # Deleting matching tokens is intentional: rerun SAME full source checks
        changed[filename]=changed[filename].replace(pattern,"",count)
        damaged=[key for key,value in checks(changed).items() if not value]
        assert damaged, f"Mutation escaped: {label}"
        rejected[label]=damaged
    return rejected


def backlog_scenario():
    # All pages were ALREADY tagged in referenced inode(s) before teardown.
    # The 2MiB limit only controls marking NEW pages; it cannot untag them.
    page_kib=4
    soft_pages=(2*1024)//page_kib
    reserve_pages=(4*1024)//page_kib
    expedite_pages=512//page_kib
    # Read-only snapshot from SM-T311 /proc/zoneinfo on G675.
    free_pages=76850
    high_pages=1350
    low_pages=1125
    highmem_free_pages=294
    highmem_low_pages=314
    retired_already_tagged=1536
    return {
        "unit":"4KiB pages",
        "normal_snapshot_pages_free":free_pages,
        "normal_high_watermark_pages":high_pages,
        "normal_low_watermark_pages":low_pages,
        "highmem_snapshot_pages_free":highmem_free_pages,
        "highmem_low_watermark_pages":highmem_low_pages,
        "highmem_below_low":highmem_free_pages<highmem_low_pages,
        "marker_soft_limit_pages":soft_pages,
        "marker_soft_limit_mib":soft_pages*page_kib/1024,
        "expedite_queued_threshold_pages":expedite_pages,
        "low_normal_reserve_pages":reserve_pages,
        "expedite_free_watermark_plus_reserve_pages":high_pages+reserve_pages,
        "normal_above_pressure_threshold_at_snapshot":
            free_pages>(high_pages+reserve_pages),
        "simulated_previously_tagged_folios_retired":retired_already_tagged,
        "illustrative_deferred_folios_mib":retired_already_tagged*page_kib/1024,
        "deferred_can_exceed_mark_soft_limit":
            retired_already_tagged>soft_pages,
        "scenario_is_live_measurement":False,
        "interpretation":"Previously tagged pages do not lose flags solely because global queue exceeds soft mark threshold; this source gate is NOT a hard upper bound on eviction deferred memory."
    }


def main():
    positive=checks(SRC)
    failed=[name for name,valid in positive.items() if not valid]
    if failed:
        raise AssertionError(f"Source checks failed {failed}")
    negatives=mutational_controls(SRC,positive)
    scenario=backlog_scenario()
    assert scenario["deferred_can_exceed_mark_soft_limit"]
    assert scenario["highmem_below_low"]
    assert scenario["normal_above_pressure_threshold_at_snapshot"]
    data={
        "stage":"G675","type":"source lifecycle audit + bounded queue arithmetic",
        "source_positive_checks":positive,
        "source_positive_pass":len(positive),
        "modified_source_negative_rejections":negatives,
        "hypothetical_backlog_scenario":scenario,
        "source_only":True,
        "caveats":[
            "No actual RCU ON or reclaim stress was performed on tablet.",
            "Current idle Normal-zone headroom does not guarantee headroom during bulk eviction.",
            "HighMem below low in one snapshot; cannot infer memory-system correctness or user experience.",
            "The 2MiB soft mark limit is not a hard maximum for folios tagged previously.",
            "Migration preserving old tagged folio through free does not establish all cross-generation ARMv7 ordering.",
            "Hardware-poison grace path is source visible, but full race proof remains unavailable.",
            "S1 ACTIVE, S2/S3 BLOCKED; no-ref RCU ON not approved."
        ]
    }
    OUT.write_text(json.dumps(data,indent=2)+"\n")
    print(f"G675: {len(positive)}/{len(positive)} lifecycle source checks PASS, "
          f"{len(negatives)}/{len(negatives)} modified-source variants REJECT")
    print("2MiB marker soft cap is NOT a hard queue cap; example 1536 already-tagged pages = 6MiB deferred")
    print("Idle measured Normal free=76850 x4KiB, HighMem free=294 < low=314; S2/S3 BLOCKED")


if __name__=="__main__":
    main()
