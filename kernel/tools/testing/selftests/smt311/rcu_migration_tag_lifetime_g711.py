#!/usr/bin/env python3
"""G711: source-coupled migration tag lifetime and bounded PFN ABA model.

Sequentially consistent toy model, not an LKMM proof or actual kernel test.
"""
from dataclasses import dataclass, replace
from itertools import combinations
from hashlib import sha256
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[4]
PROJECT = ROOT.parent
FILES = {n: (ROOT / n).read_text() for n in (
    "mm/migrate.c", "mm/filemap.c", "mm/page_alloc.c", "mm/huge_memory.c"
)}
PREV = json.loads((ROOT / "Documentation/mm/sm-t311-rcu-two-reader-g710.json").read_text())
R = ["enter", "load", "flag", "reload_copy", "exit"]
W = ["remove", "retire", "reuse", "insert", "mark"]

@dataclass(frozen=True)
class State:
    active: bool = False
    seen: bool = False
    pinned: bool = False
    passed_flag: bool = False
    raw_wrong_gen: bool = False
    unsafe_copy: bool = False
    copied: bool = False
    slot: int = 2
    pfn_gen: int = 2
    tagged: bool = False
    deferred: bool = False


def apply(s, action, referenced=False, honor_gp=True):
    if action == "enter":
        return replace(s, active=True)
    if action == "load":
        return replace(s, seen=s.slot == 2,
                       pinned=referenced and s.pfn_gen == 2 and s.slot == 2)
    if action == "flag":
        if not s.seen:
            return s
        return replace(s,
            passed_flag=s.pinned if referenced else (s.pfn_gen != 0 and s.tagged),
            raw_wrong_gen=s.raw_wrong_gen or (not referenced and s.pfn_gen != 2))
    if action == "reload_copy":
        if not s.passed_flag or not s.slot or not s.pfn_gen:
            return s
        ok = s.pinned and s.slot == 2 and s.pfn_gen == 2 if referenced else True
        return replace(s, copied=ok, unsafe_copy=s.unsafe_copy or
                       (ok and not referenced and s.pfn_gen != 2))
    if action == "exit":
        return replace(s, active=False, pinned=False)
    if action == "remove":
        return replace(s, slot=0) if s.slot == 2 else None
    if action == "retire":
        if s.slot or s.pfn_gen != 2:
            return None
        deferred = s.tagged or s.pinned
        return replace(s, pfn_gen=2 if deferred else 0, deferred=deferred)
    if action == "reuse":
        if s.slot or s.pinned or (s.deferred and s.active and honor_gp):
            return None
        return replace(s, pfn_gen=3, tagged=False, deferred=False)
    if action == "insert":
        return replace(s, slot=3) if s.slot == 0 and s.pfn_gen == 3 else None
    if action == "mark":
        return replace(s, tagged=True) if s.slot == 3 else None
    raise ValueError(action)


def schedules(destination_tagged=False, referenced=False, honor_gp=True):
    # Migration prefix: old tagged gen1 (PFN-A) is replaced by destination
    # gen2 (PFN-B). A subsequent eviction can reuse PFN-B as gen3.
    rows = dict(combinations=0, complete=0, accepted_ABA=0,
                raw_mismatch_or_freed=0, copied=0,
                witness=None, raw_witness=None)
    for comb in combinations(range(10), 5):
        seq = []
        rp = set(comb)
        ir = iw = 0
        s = State(tagged=destination_tagged)
        ok = True
        for i in range(10):
            if i in rp:
                name = R[ir]; ir += 1
                action = "R." + name
            else:
                name = W[iw]; iw += 1
                action = "W." + name
            seq.append(action)
            s = apply(s, name, referenced, honor_gp)
            if s is None:
                ok = False
                break
        rows["combinations"] += 1
        if not ok:
            continue
        rows["complete"] += 1
        rows["accepted_ABA"] += int(s.unsafe_copy)
        rows["raw_mismatch_or_freed"] += int(s.raw_wrong_gen)
        rows["copied"] += int(s.copied)
        if s.unsafe_copy and rows["witness"] is None:
            rows["witness"] = seq
        if s.raw_wrong_gen and rows["raw_witness"] is None:
            rows["raw_witness"] = seq
    return rows


def anchors(files):
    m, f, a, h = [files[n] for n in (
        "mm/migrate.c", "mm/filemap.c", "mm/page_alloc.c", "mm/huge_memory.c")]
    begin = m.index("void folio_migrate_flags(")
    end = m.index("EXPORT_SYMBOL(folio_migrate_flags)", begin)
    move = m[begin:end]
    begin = m.index("static int __folio_migrate_mapping(")
    end = m.index("int folio_migrate_mapping(", begin)
    mapping = m[begin:end]
    return {
        "G710_prior_22_gates_pass": len(PREV["checks"]) == 22 and all(PREV["checks"].values()),
        "MIGRATION_compiled_both_profiles": all(
            "CONFIG_MIGRATION=y" in (PROJECT / "build" / b / ".config").read_text()
            for b in ("g686-reproducible-exp-on", "g700-debugfs-rcu-off")),
        "preflight_refcount_gate_present":
            "folio_ref_count(src) != expected_count" in m,
        "ref_freeze_precedes_XArray_replacement":
            mapping.index("folio_ref_freeze(folio, expected_count)") <
            mapping.index("xas_store(&xas, newfolio)"),
        "XArray_replaced_before_old_ref_unfreeze":
            mapping.index("xas_store(&xas, newfolio)") <
            mapping.index("folio_ref_unfreeze(folio, expected_count - nr)"),
        "migration_flag_copy_invoked": "folio_migrate_flags(dst, src);" in m,
        "ordinary_flags_copied":
            "folio_mark_uptodate(newfolio)" in move and
            "folio_set_workingset(newfolio)" in move,
        "rcu_marker_not_explicitly_copied":
            all(t not in move for t in ("PG_rcu_free", "__PG_RCU_FREE", "folio_set_rcu_free")),
        "migration_old_source_put": "folio_put(src);" in m and
            "migrate_folio_done(src, reason)" in m,
        "migration_source_mapping_cleared": "src->mapping = NULL;" in m,
        "allocator_only_defers_tagged":
            "if (unlikely(folio_test_rcu_free(folio))" in a and
            "rcu_free_defer(folio);" in a,
        "allocator_unmarked_normal_free":
            "trace_mm_page_free(page, order);" in a,
        "XArray_rcu_raw_read_test_and_reload":
            "folio = xas_load(xas);" in f and
            "READ_ONCE(folio->flags.f) & __PG_RCU_FREE" in f and
            "if (xas_reload(xas) != folio)" in f,
        "normal_refcounted_fallback":
            "folio_try_get(folio)" in f and
            "folio_put(folio);" in f,
        "large_folio_split_clones_flag":
            "__PG_RCU_FREE |" in h and
            "folio_test_large(folio) || folio_zonenum(folio) != ZONE_NORMAL" in f,
        "activation_still_failclosed":
            "if (val)\n\t\treturn -EOPNOTSUPP;" in f,
        "release_G700_exp_disabled":
            "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" not in
            (PROJECT / "build/g700-debugfs-rcu-off/.config").read_text(),
    }


def main():
    old = schedules()
    retain = schedules(destination_tagged=True)
    pin = schedules(referenced=True)
    badgp = schedules(destination_tagged=True, honor_gp=False)
    ac = anchors(FILES)
    controls = {
        "remove_migration_ref_freeze": (
            "mm/migrate.c", "folio_ref_freeze(folio, expected_count)", "folio_ref_freeze(folio, 0)"),
        "skip_migration_XArray_replace": (
            "mm/migrate.c", "xas_store(&xas, newfolio)", "xas_store(&xas, NULL)"),
        "omit_migration_flag_copy": (
            "mm/migrate.c", "folio_migrate_flags(dst, src);", "folio_put(dst);"),
        "remove_source_put": (
            "mm/migrate.c", "folio_put(src);", "folio_put(dst);"),
        "remove_allocator_defer": (
            "mm/page_alloc.c", "rcu_free_defer(folio);", "free_unref_folios(NULL);"),
        "permit_unsafe_activation": (
            "mm/filemap.c", "if (val)\n\t\treturn -EOPNOTSUPP;", "if (val)\n\t\treturn 0;"),
    }
    detected = {}
    for name, (file, before, after) in controls.items():
        assert before in FILES[file], name
        changed = dict(FILES)
        if name == "remove_migration_ref_freeze":
            # This API occurs twice in the migration function (anon and file).
            changed[file] = FILES[file].replace(before, after)
        else:
            changed[file] = FILES[file].replace(before, after, 1)
        try:
            detected[name] = not all(anchors(changed).values())
        except (ValueError, AssertionError):
            detected[name] = True
    checks = dict(ac)
    checks.update({
        "post_migration_untagged_destination_ABA_in_model":old["accepted_ABA"] > 0,
        "retained_tag_with_grace_no_ABA_in_model":retain["accepted_ABA"] == 0,
        "reference_backed_path_no_ABA_in_model":pin["accepted_ABA"] == 0,
        "invalid_tag_without_grace_allows_ABA":badgp["accepted_ABA"] > 0,
        "all_252_order_preserving_interleavings_enumerated":
            all(x["combinations"] == 252 for x in (old, retain, pin, badgp)),
        "all_source_mutation_guards_pass":len(detected) == 6 and all(detected.values()),
        "pinned_G700_full_BOOT_unmodified":sha256(
            (PROJECT / "artifacts/G700-RCU-OFF-DEBUGFS-HOST-ONLY/BOOT-G700-RCU-OFF-DEBUGFS-HOST-ONLY-DO-NOT-FLASH.img").read_bytes()
        ).hexdigest() == "aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f",
    })
    report = {
        "stage":"G711",
        "checks":checks,
        "source_mutations_detected":detected,
        "models":{
            "as_built_destination_untagged":old,
            "hypothetical_destination_tagged":retain,
            "normal_reference_counted":pin,
            "hypothetical_tag_without_grace":badgp
        },
        "new_observation":"Tagged migrated source can be followed by an untagged destination; a later lifetime of that destination can exhibit the already-known old-untagged same-PFN ABA abstract race.",
        "not_proven":["Correctness of copying PG_rcu_free as a patch",
            "Linux memory ordering and every filesystem migration callback",
            "Migration publication ordering to readers",
            "ARMv7 cache/HighMem/lockdep",
            "Real-time performance and physical kernel success"],
        "safe_decision":"Keep G700 RCU OFF and G692 staged activation fail-closed",
        "RCU_no_ref_ON_approved":False,
        "new_runtime_C_changes":False,
        "physical_flash_performed":False
    }
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if not all(checks.values()):
        raise SystemExit(1)

if __name__ == "__main__":
    main()
