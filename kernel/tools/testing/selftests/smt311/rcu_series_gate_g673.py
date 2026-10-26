#!/usr/bin/env python3
"""G673: verify real 18-commit RCU series ancestry and SM-T311 default-OFF.

Repository integrity and source assertions are NOT a folio lifetime/ABA proof
and are not a substitute for the physical /proc/config.gz capture via ADB.
"""
from __future__ import annotations
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "Documentation/mm/sm-t311-rcu-series-g673.json"
PARENT = "4432bceab"
SERIES_END = "016e91411"
DEPLOYED_SOURCE = "a2fca4558"
RUNTIME_DIRS = ("mm", "fs", "kernel", "drivers", "arch", "include", "init")
EXPECTED_PREFIXES = [
    "05362c1a7", "dc8121b77", "70372e1d8", "8f1727487",
    "600424960", "8f870c46c", "3d859a1f3", "05d297f83",
    "eaa496012", "899f8d53b", "fc553008d", "e7f430601",
    "d46226de3", "1fbcb22d1", "0ab08136b", "aa34c829e",
    "ba9b14cec", "016e91411",
]

def git(*args):
    p = subprocess.run(("git", *args), cwd=ROOT, capture_output=True, text=True)
    if p.returncode:
        raise AssertionError(f"git {' '.join(args)} failed: {p.stderr.strip()}")
    return p.stdout.strip()

def is_ancestor(older, newer):
    p = subprocess.run(("git", "merge-base", "--is-ancestor", older, newer),
                       cwd=ROOT, capture_output=True)
    return p.returncode == 0

def checks(text):
    filemap, allocator, arm, config = (text[name] for name in (
        "mm/filemap.c", "mm/page_alloc.c", "arch/arm/mm/flush.c",
        "arch/arm/configs/sm_t311_android11_defconfig"))
    source_folio = filemap.split("static struct folio *filemap_read_rcu_lookup(", 1)[1].split(
        "static size_t filemap_read_rcu_size(", 1)[0]
    source_batch = filemap.split("static bool filemap_read_rcu_batch(", 1)[1].split(
        "size_t filemap_read_rcu(", 1)[0]
    marker = filemap.split("static noinline void filemap_mark_hot(", 1)[1].split(
        "void filemap_read_accessed(", 1)[0]
    return {
        "SMT311_compiled_RCUswitch_defaults_false":
            "static DEFINE_STATIC_KEY_FALSE(lockless_read_key);" in filemap,
        "SMT311_filemap_rcu_no_ref_requires_enabled_switch":
            "if (!lockless_read_enabled() ||" in filemap,
        "SMT311_mark_HOT_under_reference":
            "folio_test_set_rcu_free(folio)" in marker,
        "SMT311_marker_has_order0_normal_zone_guard":
            "folio_test_large(folio) || folio_zonenum(folio) != ZONE_NORMAL" in marker,
        "SMT311_lookup_requires_flag_before_reloading_xarray":
            source_folio.find("READ_ONCE(folio->flags.f)") >= 0 and
            source_folio.find("xas_reload(xas) != folio") >= 0 and
            source_folio.find("READ_ONCE(folio->flags.f)") <
            source_folio.find("xas_reload(xas) != folio"),
        "SMT311_lookup_checks_4k_zone_and_mapping":
            "folio_test_large(folio)" in source_folio and
            "folio_zonenum(folio) != ZONE_NORMAL" in source_folio and
            "READ_ONCE(folio->mapping) != mapping" in source_folio,
        "SMT311_reader_has_rcu_and_pagefault_scope":
            "rcu_read_lock();" in source_batch and
            "pagefault_disable();" in source_batch and
            "pagefault_enable();" in source_batch and
            "rcu_read_unlock();" in source_batch,
        "SMT311_copy_budget_max_32KB":
            "#define FILEMAP_RCU_READ_MAX\tSZ_32K" in filemap,
        "SMT311_reader_excludes_writable_mmap":
            "mapping_writably_mapped(mapping)" in source_batch,
        "SMT311_tagged_folio_last_free_deferred":
            "rcu_free_defer(folio);" in allocator and
            "__folio_clear_rcu_free(folio);" in allocator,
        "SMT311_grace_on_queue_and_expedited":
            "queue_rcu_work(rcu_free_wq, &r->rwork)" in allocator and
            "synchronize_rcu_expedited();" in allocator,
        "SMT311_configuration_ARM_SMP_PREEMPT_RCU_HIGHMEM":
            all(f"CONFIG_{x}=y" in config for x in ("SMP", "PREEMPT_RCU", "HIGHMEM")),
        "ARM_maintains_D_cache_when_required":
            "__sync_icache_dcache" in arm and
            "cache_is_vipt_aliasing()" in arm,
    }

def main():
    commits = git("rev-list", "--reverse", f"{PARENT}..{SERIES_END}").splitlines()
    assert len(commits) == 18, "RCU source history no longer matches 18 Oct19 imported commits"
    assert [c[:len(pre)] for c, pre in zip(commits, EXPECTED_PREFIXES)] == EXPECTED_PREFIXES
    assert is_ancestor(SERIES_END, DEPLOYED_SOURCE), "18-series is NOT ancestor of physical G650 source"
    assert is_ancestor(DEPLOYED_SOURCE, "HEAD")
    tree_changes = git("diff", "--name-only", DEPLOYED_SOURCE, "HEAD",
                       "--", *RUNTIME_DIRS).splitlines()
    # G686 adds a Git-tracked experimental Kconfig overlay; it is NOT
    # executable source and must not be mislabeled physically installed.
    optional_config = "arch/arm/configs/sm_t311_rcu_experiment_g686.fragment"
    assert tree_changes.count(optional_config) == 1, tree_changes
    fragment = (ROOT / optional_config).read_text()
    assert "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" in fragment
    assert "CONFIG_DEBUG_FS=y" in fragment
    g700_config = "arch/arm/configs/sm_t311_rcu_off_keep_debugfs_g700.fragment"
    assert tree_changes.count(g700_config)==1,tree_changes
    g700_text=(ROOT / g700_config).read_text()
    assert "CONFIG_DEBUG_FS=y" in g700_text
    assert "# CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT is not set" in g700_text
    assert "# CONFIG_PAGE_RCU_FREE is not set" in g700_text
    optional_configs={optional_config,g700_config}
    executable_diff=[p for p in tree_changes if p not in optional_configs]
    progress = json.loads((ROOT / "Documentation/mm/sm-t311-rcu-progress.json").read_text())
    staged = progress["staged_runtime_code_changes_NOT_INSTALLED"]
    assert sorted(executable_diff) == sorted(staged), (
        "Executable source differs from physical G650 without matching staged declaration: "
        + repr(executable_diff))
    assert progress["physical_kernel_source_commit"] == DEPLOYED_SOURCE
    assert progress["physical_boot_release"] == "G650"
    assert progress["rcu_ON_safety_proven"] is False
    files = ("mm/filemap.c", "mm/page_alloc.c",
             "arch/arm/mm/flush.c", "arch/arm/configs/sm_t311_android11_defconfig")
    source = {name: (ROOT / name).read_text() for name in files}
    values = checks(source)
    assert len(values) == 13, len(values)
    missing = [k for k, ok in values.items() if not ok]
    if missing:
        raise AssertionError("G673 source regression: " + repr(missing))
    mutations = {
        "default_ON": ("mm/filemap.c", "static DEFINE_STATIC_KEY_FALSE(lockless_read_key);"),
        "remove_XArray_reread": ("mm/filemap.c", "xas_reload(xas) != folio"),
        "no_queue_grace": ("mm/page_alloc.c", "queue_rcu_work(rcu_free_wq, &r->rwork)"),
        "no_normal_zone_reader_guard": ("mm/filemap.c", "folio_zonenum(folio) != ZONE_NORMAL"),
    }
    rejected = {}
    for label, (file, target) in mutations.items():
        changed = dict(source)
        assert target in changed[file], (label, target)
        if label == "no_normal_zone_reader_guard":
            assert target in changed[file].split("static struct folio *filemap_read_rcu_lookup(",1)[1]
            pre, post = changed[file].split("static struct folio *filemap_read_rcu_lookup(", 1)
            post = post.replace(target, "false /* mutated missing zone guard */", 1)
            changed[file] = pre + "static struct folio *filemap_read_rcu_lookup(" + post
        else:
            changed[file] = changed[file].replace(target, "", 1)
        flaws = [k for k, ok in checks(changed).items() if not ok]
        if not flaws:
            raise AssertionError("Mutant not caught by integrity gate: " + label)
        rejected[label] = flaws

    output = {
        "stage": "G673", "type": "COMMIT ANCESTRY, SOURCE AND DEFAULT-OFF VERIFICATION",
        "series_commit_count": len(commits),
        "series_sha256_commit_ids": commits,
        "first_series_commit": commits[0], "last_series_commit": commits[-1],
        "g650_source_commit": git("rev-parse", DEPLOYED_SOURCE),
        "g650_contains_all_18_series_commits": True,
        "runtime_source_identical_to_G650": not bool(executable_diff),
        "runtime_source_staged_NOT_INSTALLED": executable_diff,
        "physical_boot_source_unchanged": True,
        "source_checks": values,
        "source_checks_passed": len(values),
        "negative_mutations_rejected": rejected,
        "physical_config_verified_separately_via_adb": {
            "CONFIG_PAGE_RCU_FREE": "y", "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT": "y",
            "CONFIG_PREEMPT_RCU": "y", "CONFIG_SMP": "y", "CONFIG_HIGHMEM": "y",
            "evidence": "/proc/config.gz read-only adb_shell on G673",
        },
        "critical_qualifications": [
            "Code is in the compiled physical kernel but runtime no-ref path is DEFAULT OFF.",
            "Build inclusion is NOT runtime feature activation, safety or speedup.",
            "PG_rcu_free flags and XArray ABA/allocator reuse remain unproven for ARMv7.",
            "HighMem and VIPT alias lifetime remain BLOCKED.",
            "The kernel debugfs toggle was not directly readable in G673; do not claim it was read.",
            "G665 real native userspace XArray binary UNRUN due missing liburcu-dev headers.",
            "No executable kernel change and no new physical BOOT flash needed."
        ],
        "stages": {"S0": "PASS", "S1": "ACTIVE", "S2": "BLOCKED", "S3": "BLOCKED",
                   "S4": "BLOCKED", "S5": "BLOCKED", "S6": "BLOCKED",
                   "S7": "PARTIAL", "S8": "ACTIVE"},
    }
    OUT.write_text(json.dumps(output, indent=2, ensure_ascii=False) + "\n")
    print(f"G673 Linus/Kiryl: {len(commits)}/18 commits in physical G650 source, "
          f"{len(values)}/13 sources PASS, {len(rejected)} mutations REJECT; "
          "runtime no-ref DEFAULT OFF; S2/S3 BLOCKED")

if __name__ == "__main__":
    main()
