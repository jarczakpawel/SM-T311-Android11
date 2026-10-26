#!/usr/bin/env python3
"""G708: source-anchored RCU read/free/lifetime gate, fail-closed on ARMv7.

This does NOT claim an ABA proof. It detects losing known safety barriers
while retaining the definitive fail-closed experiment switch.
"""
import hashlib
import json
import re
import subprocess
from pathlib import Path

kernel = Path(__file__).resolve().parents[4]
project = kernel.parent

paths = ("mm/filemap.c", "mm/page_alloc.c", "mm/shmem.c", "mm/vma.c")
files = {p: (kernel / p).read_text() for p in paths}
prev = json.loads((kernel / "Documentation/mm/sm-t311-rcu-pageflag-budget-g707.json").read_text())
configs = {
    "G686_EXP": (project / "build/g686-reproducible-exp-on/.config").read_text(),
    "G700_OFF": (project / "build/g700-debugfs-rcu-off/.config").read_text(),
}

def function(source, name):
    # Locate a C function DEFINITION (not call or declaration); the selected
    # source functions have simple brace-balanced bodies. No C is modified.
    for match in re.finditer(r"\b" + re.escape(name) + r"\s*\(", source):
        prefix = source[max(0, match.start() - 120):match.start()]
        if not re.search(r"(?:^|\n)\s*(?:(?:static|inline|noinline|__always_inline|void|bool|size_t|ssize_t|unsigned|struct|int|long|__init|__always_inline)\s+|\*|\w+\s+){1,12}$", prefix):
            continue
        depth, end_args = 0, None
        for pos in range(source.index("(", match.start()), min(match.start() + 400, len(source))):
            if source[pos] == "(": depth += 1
            if source[pos] == ")":
                depth -= 1
                if depth == 0:
                    end_args = pos + 1
                    break
        if end_args is None:
            continue
        tail = source[end_args:end_args+200]
        if "{" not in tail or (";" in tail and tail.index(";") < tail.index("{")):
            continue
        begin = end_args + tail.index("{")
        level = 0
        for pos in range(begin, len(source)):
            if source[pos] == "{": level += 1
            elif source[pos] == "}":
                level -= 1
                if level == 0:
                    return source[begin:pos+1]
    raise RuntimeError("Function definition missing: " + name)

def funcmap(src):
    filemap = src["mm/filemap.c"]
    alloc = src["mm/page_alloc.c"]
    vma = src["mm/vma.c"]
    shmem = src["mm/shmem.c"]
    f = {
        "reader": function(filemap, "filemap_read_rcu_lookup"),
        "batch": function(filemap, "filemap_read_rcu_batch"),
        "entry": function(filemap, "filemap_read_rcu"),
        "mark": function(filemap, "filemap_mark_hot"),
        "slow": function(filemap, "filemap_get_entry"),
        "delete": function(filemap, "page_cache_delete"),
        "delete_batch": function(filemap, "page_cache_delete_batch"),
        "setter": function(filemap, "lockless_read_set"),
        "main": function(filemap, "filemap_read"),
        "alloc_prepare": function(alloc, "__free_pages_prepare"),
        "defer": function(alloc, "rcu_free_defer"),
        "rcu_work": function(alloc, "rcu_free_workfn"),
        "start": function(alloc, "rcu_free_start"),
        "expedite": function(alloc, "rcu_free_flush"),
        "sync": function(alloc, "rcu_free_sync"),
        "mmap": function(vma, "mmap_region"),
        "shmem": function(shmem, "shmem_file_read_iter"),
    }
    return f

def checks_for(src, conf=configs):
    f = funcmap(src)
    return {
        "host_G707_status_retained": all(prev["checks"].values()) and all(prev["negative_controls"].values()),
        "source_4_runtime_paths_match_declared_G650_diff": sorted(
            subprocess.run(
                ["git", "diff", "--name-only", "a2fca4558", "HEAD", "--", "mm"],
                cwd=kernel, capture_output=True, text=True, check=True
            ).stdout.splitlines()
        ) == sorted(paths),
        "reader_XArray_raw_lookup_explicitly_unpinned": (
            "xas_load(xas)" in f["reader"] and "folio_try_get" not in f["reader"]
        ),
        "reader_requires_rcu_flag_before_slot_reload": (
            f["reader"].index("READ_ONCE(folio->flags.f)") <
            f["reader"].index("xas_reload(xas)")
            and "__PG_RCU_FREE" in f["reader"]
        ),
        "reader_rechecks_slot_and_address_space": (
            "xas_reload(xas) != folio" in f["reader"] and
            "READ_ONCE(folio->mapping) != mapping" in f["reader"]
        ),
        "reader_checks_zone_and_large_page_for_ARMv7": (
            "folio_zonenum(folio) != ZONE_NORMAL" in f["reader"]
            and "folio_test_large(folio)" in f["reader"]
        ),
        "read_batch_enters_RCU_before_mapping_and_key_checks": (
            f["batch"].index("rcu_read_lock();") <
            f["batch"].index("lockless_read_enabled()") <
            f["batch"].index("mapping_writably_mapped(mapping)")
        ),
        "read_batch_disables_pagefaults_inside_rcu": (
            f["batch"].index("rcu_read_lock();") < f["batch"].index("pagefault_disable();")
            < f["batch"].index("pagefault_enable();") < f["batch"].rindex("rcu_read_unlock();")
        ),
        "read_batch_has_bounded_copy_and_grace_exit": (
            "FILEMAP_RCU_READ_MAX" in f["batch"]
            and "budget -= n;" in f["batch"] and "rcu_read_unlock();" in f["batch"]
        ),
        "reader_only_ext4_user_buffers_experiment": (
            "EXT4_SUPER_MAGIC" in f["entry"] and "user_backed_iter(iter)" in f["entry"]
        ),
        "cold_reader_and_marker_default_disabled": (
            "DEFINE_STATIC_KEY_FALSE(lockless_read_key)" in src["mm/filemap.c"]
            and "if (hot && lockless_read_enabled())" in src["mm/filemap.c"]
        ),
        "debugfs_enable_fail_closed_after_invalid_guard": (
            f["setter"].index("val > 1") < f["setter"].index("if (val)\n\t\treturn -EOPNOTSUPP;")
        ),
        "RCU_experiment_Kconfig_exp_on_release_off": (
            "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" in conf["G686_EXP"]
            and "# CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is not set" in conf["G700_OFF"]
            and "CONFIG_PAGE_RCU_FREE=y" in conf["G686_EXP"]
            and "CONFIG_PAGE_RCU_FREE=y" not in conf["G700_OFF"]
        ),
        "marker_requires_enabled_key_and_reference_hot": (
            "if (!lockless_read_enabled())" in f["mark"]
            and "folio_test_set_rcu_free(folio)" in f["mark"]
        ),
        "marker_only_correct_mapping_and_normal_zone": (
            "READ_ONCE(folio->mapping) != mapping" in f["mark"]
            and "folio_zonenum(folio) != ZONE_NORMAL" in f["mark"]
        ),
        "delete_paths_preserve_marked_folio_on_experiment": all(
            "!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT)" in f[key]
            and "mapping_exiting(mapping)" in f[key]
            for key in ("delete", "delete_batch")
        ),
        "allocator_requires_mark_and_queues_before_reuse": (
            "folio_test_rcu_free(folio)" in f["alloc_prepare"]
            and f["alloc_prepare"].index("__folio_clear_rcu_free(folio)") <
            f["alloc_prepare"].index("rcu_free_defer(folio)") <
            f["alloc_prepare"].index("return false;")
        ),
        "allocator_unmarked_folios_not_forced_RCU_queued": (
            "if (unlikely(folio_test_rcu_free(folio))" in f["alloc_prepare"]
            and "trace_mm_page_free(page, order);" in f["alloc_prepare"]
        ),
        "queue_rcu_work_required_for_ordered_defer": (
            "queue_rcu_work(rcu_free_wq, &r->rwork)" in f["start"]
            and "rcu_free_pages(&done);" in f["rcu_work"]
        ),
        "manual_deferred_flush_waits_expedited_grace": (
            f["expedite"].index("synchronize_rcu_expedited();") <
            f["expedite"].index("rcu_free_pages(list);")
        ),
        "work_drain_waits_rcu_callback_not_just_plain_work": (
            "rcu_free_drain();" in f["sync"]
            and "flush_rcu_work(&r->rwork);" in f["sync"]
            and "flush_work(&r->expedite);" in f["sync"]
        ),
        "new_ext4_mmap_writable_count_before_gp_before_publish": (
            f["mmap"].index("mapping_map_writable(file->f_mapping)") <
            f["mmap"].index("synchronize_rcu();") <
            f["mmap"].index("writable_file_mapping = true;")
            and "EXT4_SUPER_MAGIC" in f["mmap"]
        ),
        "ordinary_reader_acquires_ref_then_reloads_and_puts_on_mismatch": (
            f["slow"].index("folio_try_get(folio)") <
            f["slow"].index("folio != xas_reload(&xas)") <
            f["slow"].index("folio_put(folio)")
        ),
        "RCU_OFF_release_skips_rcu_read_in_filemap_and_shmem": all(
            ("#elif defined(CONFIG_PAGE_RCU_FREE)" in f[key]
             and "fast_read = 0;" in f[key] if key == "main"
             else "#elif defined(CONFIG_PAGE_RCU_FREE)" in f[key]
             and "retval = 0;" in f[key])
            for key in ("main", "shmem")
        ),
        "still_no_reference_generation_identity_in_raw_reader": (
            "folio_try_get(" not in f["reader"]
            and "generation" not in re.sub(r"/\*.*?\*/", "", f["reader"], flags=re.S)
        ),
    }

checks = checks_for(files)
mutants = {
    "reject_raw_lookup_without_RCU_flag": (
        "mm/filemap.c", "READ_ONCE(folio->flags.f) & __PG_RCU_FREE",
        "READ_ONCE(folio->flags.f) & 0"
    ),
    "reject_missing_XArray_revalidation": (
        "mm/filemap.c", "xas_reload(xas) != folio",
        "xas_reload(xas) == NULL"
    ),
    "reject_missing_mapping_identity": (
        "mm/filemap.c", "READ_ONCE(folio->mapping) != mapping",
        "READ_ONCE(folio->mapping) == NULL"
    ),
    "reject_ref_free_activation_allowed": (
        "mm/filemap.c", "if (val)\n\t\treturn -EOPNOTSUPP;",
        "if (val)\n\t\treturn 0;"
    ),
    "reject_normal_zone_reader_unbounded": (
        "mm/filemap.c", "folio_zonenum(folio) != ZONE_NORMAL",
        "folio_zonenum(folio) == ZONE_DMA"
    ),
    "reject_reference_path_no_tryget": (
        "mm/filemap.c", "if (!folio_try_get(folio))",
        "if (!folio_test_uptodate(folio))"
    ),
    "reject_unprotected_inode_exit_elision": (
        "mm/filemap.c", "!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&",
        "IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&"
    ),
    "reject_immediate_recycling_of_marked_folio": (
        "mm/page_alloc.c", "rcu_free_defer(folio);",
        "rcu_free_pages(NULL);"
    ),
    "reject_premature_RCU_work_dequeue": (
        "mm/page_alloc.c", "queue_rcu_work(rcu_free_wq, &r->rwork)",
        "queue_work(rcu_free_wq, &r->rwork.work)"
    ),
    "reject_missing_expedited_grace": (
        "mm/page_alloc.c", "synchronize_rcu_expedited();",
        "cond_resched();"
    ),
    "reject_workqueue_only_flush": (
        "mm/page_alloc.c", "flush_rcu_work(&r->rwork);",
        "flush_work(&r->rwork.work);"
    ),
    "reject_writer_missing_rcu_grace": (
        "mm/vma.c", "synchronize_rcu();",
        "cond_resched();"
    ),
}
negative = {}
for label, (path, old, new) in mutants.items():
    if old not in files[path]:
        raise RuntimeError("Mutation fixture absent: " + label)
    changed = dict(files)
    changed[path] = files[path].replace(old, new, 1)
    try:
        after = checks_for(changed)
    except (ValueError, RuntimeError) as ex:
        negative[label] = True
    else:
        negative[label] = not all(after.values())

report = {
    "stage": "G708",
    "strategy": "trace complete experimental no-ref read, hot folio tagging, two delete variants, deferred allocator recycle, RCU sync, mmap writer and refcounted fallback; reject unsafe source mutations",
    "checks": checks,
    "negative_controls": negative,
    "paths": paths,
    "proof_limits": [
        "Source ordering alone does not prove C11/LKMM ordering or lack of PFN ABA",
        "Raw XArray pointer from an initially UNTAGGED folio can be recycled before PG_rcu_free is sampled (G691)",
        "RCU grace protecting NEW marked incarnation cannot retroactively protect an OLD unmarked incarnation",
        "ARMv7 HighMem/VIPT alias/cache and lockdep progress are not signed off",
        "Debugfs activation is fail-closed only in staged G692/G686 source, NOT confirmed on physical G650",
        "G700 RCU OFF is an existing host-only boot image; it has not been tested on hardware",
    ],
    "release_interlock_staged": True,
    "rcu_no_ref_enable_approved": False,
    "S1_reader_writer_proof_complete": False,
    "S2_PFN_lifetime_proof_complete": False,
    "S3_HighMem_lockdep_proof_complete": False,
    "physical_G700_installed": False,
    "flash_authorized": False,
    "G700_BOOT_sha256": hashlib.sha256(
        (project / "artifacts/G700-RCU-OFF-DEBUGFS-HOST-ONLY/BOOT-G700-RCU-OFF-DEBUGFS-HOST-ONLY-DO-NOT-FLASH.img").read_bytes()
    ).hexdigest(),
}
print(json.dumps(report, indent=2, ensure_ascii=False))
if not all(checks.values()) or not all(negative.values()):
    raise SystemExit(1)
