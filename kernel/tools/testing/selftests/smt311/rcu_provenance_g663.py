#!/usr/bin/env python3
"""G663: bounded, sequentially-consistent XArray pointer/folio provenance explorer.

NOT LKMM, an ARMv7 proof, a C race detector or an authorization for RCU ON.
One writer follows source-ordered single-folio delete, last put, same-address
reallocation, XArray publication, under-ref marker, and a second deletion.
One reader follows filemap_read_rcu_lookup() and the order-0 copy path.
Only a snapshot of source-ordered steps is modeled; the real allocator,
weak memory, xas_load internal tree lifetime, inode size races and filesystem
mutations are outside this bounded model.
"""
import itertools
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
OUTPUT = ROOT / "Documentation/mm/sm-t311-rcu-provenance-g663.json"

R = ("R_lock", "R_load", "R_flag", "R_size", "R_reload",
     "R_mapping", "R_copy", "R_unlock")
W = ("W_erase_old", "W_old_last_put", "W_allocate_new", "W_publish_new",
     "W_mark_new_under_ref", "W_erase_new", "W_new_last_put")


def interleavings():
    """Preserve both threads' program order and yield all 6435 merges."""
    for writer_positions in itertools.combinations(range(len(R) + len(W)), len(W)):
        positions = set(writer_positions)
        ri, wi = iter(R), iter(W)
        yield tuple(next(wi) if i in positions else next(ri)
                    for i in range(len(R) + len(W)))


def source_gate():
    filemap = (ROOT / "mm/filemap.c").read_text()
    allocator = (ROOT / "mm/page_alloc.c").read_text()
    lookup = filemap.split("static struct folio *filemap_read_rcu_lookup(", 1)[1].split(
        "static size_t filemap_read_rcu_size(", 1)[0]
    mapping = filemap.split("static void page_cache_delete(", 1)[1].split(
        "static void filemap_unaccount_folio(", 1)[0]
    marker = filemap.split("static noinline void filemap_mark_hot(", 1)[1].split(
        "void filemap_read_accessed(", 1)[0]
    batch = filemap.split("static bool filemap_read_rcu_batch(", 1)[1].split(
        "size_t filemap_read_rcu(", 1)[0]
    free = allocator.split("static __always_inline bool __free_pages_prepare(", 1)[1].split(
        "static bool free_pages_prepare(", 1)[0]
    checks = {
        "lockless_reader": "folio = xas_load(xas);" in lookup,
        "reader_tag_before_slot_before_mapping": (
            lookup.index("__PG_RCU_FREE") < lookup.index("xas_reload(xas) != folio")
            < lookup.index("READ_ONCE(folio->mapping) != mapping")
        ),
        "reader_section_contains_copy": "rcu_read_lock();" in batch and "filemap_read_rcu_copy(" in batch and "rcu_read_unlock();" in batch,
        "single_delete_slot_before_mapping_null": mapping.index("xas_store(&xas, shadow)") < mapping.index("folio->mapping = NULL"),
        "new_tag_under_reference": "folio_test_set_rcu_free(folio)" in marker,
        "flagged_folio_deferred_before_buddy": "rcu_free_defer(folio)" in free and "return false;" in free,
    }
    assert all(checks.values()), [k for k, v in checks.items() if not v]
    return checks


def simulate(steps, *, donor_marked=False, defer_marked=True,
             new_mapping="M", enforce_mapping=True, enforce_reload=True):
    """A and B are folio generations. Their addresses may coincide."""
    active = False
    slot = ("A", 1)
    by_addr = {"A": 1}
    objs = {1: {"address": "A", "marked": donor_marked, "alive": True,
                "mapping": "M", "index": 0, "zone_normal": True}}
    ptr = None
    observed_at_load = None
    observed_at_flag = None
    flag_ok = size_ok = reload_ok = mapping_ok = False
    reused = False
    copied = False
    copy_alive = False
    copy_generation = None
    copy_mapping = None
    for op in steps:
        if op == "R_lock":
            active = True
        elif op == "R_load":
            if slot is not None:
                ptr, observed_at_load = slot
        elif op == "R_flag":
            o = objs.get(by_addr.get(ptr))
            if o and o["alive"] and o["marked"]:
                flag_ok = True
                observed_at_flag = by_addr[ptr]
        elif op == "R_size":
            # 32-bit seqcount/source ordering assessed by other G657 tests.
            size_ok = flag_ok   # inode size = 4096, position = 0.
        elif op == "R_reload":
            reload_ok = size_ok and (not enforce_reload or
                                     (slot is not None and slot[0] == ptr))
        elif op == "R_mapping":
            o = objs.get(by_addr.get(ptr))
            mapping_ok = bool(reload_ok and o and o["alive"] and
                              o["zone_normal"] and
                              (not enforce_mapping or
                               (o["mapping"] == "M" and o["index"] == 0)))
        elif op == "R_copy":
            if mapping_ok:
                copied = True
                o = objs.get(by_addr.get(ptr))
                copy_alive = bool(o and o["alive"])
                if o:
                    copy_generation = by_addr[ptr]
                    copy_mapping = o["mapping"]
        elif op == "R_unlock":
            active = False
        elif op == "W_erase_old":
            slot = None
            objs[1]["mapping"] = None
        elif op == "W_old_last_put":
            if not (objs[1]["marked"] and defer_marked and active):
                objs[1]["alive"] = False
                by_addr.pop("A", None)
        elif op == "W_allocate_new":
            address = "A" if "A" not in by_addr else "B"
            reused = address == "A"
            objs[2] = {"address": address, "marked": False, "alive": True,
                       "mapping": new_mapping, "index": 0, "zone_normal": True}
            by_addr[address] = 2
        elif op == "W_publish_new":
            slot = (objs[2]["address"], 2)
        elif op == "W_mark_new_under_ref":
            objs[2]["marked"] = True
        elif op == "W_erase_new":
            slot = None
            objs[2]["mapping"] = None
        elif op == "W_new_last_put":
            # A marked page freed during RCU read section is not reusable
            # until grace. Deliberate counterfactual disables this obligation.
            if not (objs[2]["marked"] and defer_marked and active):
                objs[2]["alive"] = False
                by_addr.pop(objs[2]["address"], None)
        else:
            raise ValueError(op)
    mixed = copied and observed_at_load is not None and observed_at_load != copy_generation
    flag_copy_mixed = copied and observed_at_flag is not None and observed_at_flag != copy_generation
    return {
        "copied": copied, "alive": copy_alive, "mixed_generation": mixed,
        "flag_copy_mixed": flag_copy_mixed, "copy_generation": copy_generation,
        "copy_mapping": copy_mapping, "reused_address": reused,
    }


def explore(**kwargs):
    counts = {"schedules": 0, "accepted": 0, "mixed_generation": 0,
              "flag_copy_mixed": 0, "dead_copy": 0, "foreign_mapping_copy": 0}
    examples = {}
    for steps in interleavings():
        counts["schedules"] += 1
        x = simulate(steps, **kwargs)
        if not x["copied"]:
            continue
        counts["accepted"] += 1
        checks = {
            "mixed_generation": x["mixed_generation"],
            "flag_copy_mixed": x["flag_copy_mixed"],
            "dead_copy": not x["alive"],
            "foreign_mapping_copy": x["copy_mapping"] not in ("M", None),
        }
        for k, ok in checks.items():
            if ok:
                counts[k] += 1
                examples.setdefault(k, steps)
    return {"counts": counts,
            "example_schedules": {k: list(v) for k, v in examples.items()}}


def main():
    anchors = source_gate()
    baseline = explore(donor_marked=False, defer_marked=True)
    donor_tagged = explore(donor_marked=True, defer_marked=True)
    missing_grace = explore(donor_marked=False, defer_marked=False)
    corrupted_mapping_guard = explore(donor_marked=False, defer_marked=True,
                                      new_mapping="Other", enforce_mapping=False)
    correct_mapping_guard = explore(donor_marked=False, defer_marked=True,
                                    new_mapping="Other", enforce_mapping=True)
    no_reload = explore(donor_marked=False, defer_marked=True, enforce_reload=False)

    bc = baseline["counts"]
    assert bc["schedules"] == 6435
    assert bc["mixed_generation"] > 0, bc
    assert bc["dead_copy"] == 0 and bc["foreign_mapping_copy"] == 0, bc
    assert donor_tagged["counts"]["mixed_generation"] == 0, donor_tagged
    assert missing_grace["counts"]["dead_copy"] > 0, missing_grace
    assert corrupted_mapping_guard["counts"]["foreign_mapping_copy"] > 0
    assert correct_mapping_guard["counts"]["foreign_mapping_copy"] == 0
    assert no_reload["counts"]["accepted"] >= bc["accepted"]
    result = {
        "stage": "G663",
        "status": "DIAGNOSTIC ONLY - S2 BLOCKED",
        "source_anchors": anchors,
        "test_model": "source-ordered finite SC interleaving, NOT herd7/LKMM/real XArray allocator",
        "initially_unmarked_donor": baseline,
        "initially_marked_donor": donor_tagged,
        "counterfactual_missing_grace": missing_grace,
        "counterfactual_missing_mapping_check": corrupted_mapping_guard,
        "foreign_mapping_guard_on": correct_mapping_guard,
        "counterfactual_missing_slot_reload": no_reload,
        "limitations": [
            "A same-address mixed-generation observation can copy live B; it is not automatically UAF or data corruption.",
            "Reader flag on B and deferred B final free are distinct from A's earlier unmarked lifetime.",
            "New allocation and marking happen under references in this simplified schedule; real allocator timing and reordering are not modeled.",
            "The proof still needs weak-memory and actual XArray node/slot, i_size, truncate, eviction, migration and ARMv7 cache review.",
            "The source gate covers selected anchors, not a complete callgraph; no permission for RCU ON or physical flash."
        ]
    }
    OUTPUT.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n")
    for k in ("initially_unmarked_donor", "initially_marked_donor",
              "counterfactual_missing_grace", "counterfactual_missing_mapping_check",
              "foreign_mapping_guard_on", "counterfactual_missing_slot_reload"):
        print(k, result[k]["counts"])
    print("G663 source-ordered provenance explorer PASS; S2 remains BLOCKED")


if __name__ == "__main__":
    main()
