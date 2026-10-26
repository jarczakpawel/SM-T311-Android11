#!/usr/bin/env python3
"""G653 abstract RCU pagecache pointer-ABA interleaving explorer.

This is NOT an LKMM model, not an ARMv7 ordering test, and never accesses
the device. It enumerates simple reader/writer event schedules to test that
the engineering plan accounts for dereference AFTER a concurrent removal.
Negative controls deliberately relax deferred reuse and flag preconditions.
"""
import itertools
import json
from pathlib import Path

OUT = Path(__file__).resolve().parents[4] / "Documentation/mm/sm-t311-rcu-aba-explorer.json"
READER = ("R_lock", "R_load", "R_flag", "R_size", "R_reload", "R_copy", "R_unlock")
WRITER = ("W_remove", "W_drop_last_ref", "W_reinsert")


def schedules():
    for where in itertools.combinations(range(len(READER) + len(WRITER)), len(WRITER)):
        it_r, it_w = iter(READER), iter(WRITER)
        yield [next(it_w) if i in where else next(it_r)
               for i in range(len(READER) + len(WRITER))]


def run(actions, old_marked, defer_flagged_free, new_marked):
    # Address identity A can be reused after a free. Generation is a test-only
    # oracle, not something real filemap_read_rcu_lookup stores or compares.
    slot = ("A", 1)
    memory = {"A": {"generation": 1, "marked": old_marked, "alive": True}}
    loaded_address = None
    loaded_generation = None
    flag_passed = False
    slot_passed = False
    old_deferred = False
    allow_reuse_old = False
    writer_slot_removed = False
    reader_active = False
    completed = False
    copy_aba = False
    for action in actions:
        if action == "R_lock":
            reader_active = True
        elif action == "R_load":
            if slot is not None:
                loaded_address, loaded_generation = slot
        elif action == "R_flag":
            if loaded_address is not None:
                # A stale pointer can point to either old OR a newly reused A.
                f = memory.get(loaded_address)
                flag_passed = bool(f and f["alive"] and f["marked"])
        elif action == "R_size":
            pass  # Abstract: no i_size or memory-order model available.
        elif action == "R_reload":
            slot_passed = bool(
                flag_passed and slot is not None and loaded_address == slot[0])
        elif action == "R_copy":
            if slot_passed and loaded_address is not None:
                item = memory[loaded_address]
                if item["alive"]:
                    completed = True
                    copy_aba = item["generation"] != loaded_generation
        elif action == "R_unlock":
            reader_active = False
        elif action == "W_remove":
            slot = None
            writer_slot_removed = True
        elif action == "W_drop_last_ref":
            # A flagged folio must NOT be recycled during active RCU reader.
            old_deferred = bool(old_marked and defer_flagged_free and reader_active)
            allow_reuse_old = not old_deferred
            if allow_reuse_old:
                memory["A"]["alive"] = False
        elif action == "W_reinsert":
            if not writer_slot_removed:
                continue
            if allow_reuse_old:
                memory["A"] = {"generation": 2, "marked": new_marked, "alive": True}
                slot = ("A", 2)
            else:
                memory["B"] = {"generation": 2, "marked": new_marked, "alive": True}
                slot = ("B", 2)
    return {"complete": completed, "aba": copy_aba}


def explore(old_marked, defer_flagged_free, new_marked):
    tested = 0
    examples = []
    accepted = 0
    for order in schedules():
        tested += 1
        result = run(order, old_marked, defer_flagged_free, new_marked)
        accepted += result["complete"]
        if result["aba"] and len(examples) < 3:
            examples.append(order)
    return {"tested_interleavings": tested,
            "accepted_copies": accepted,
            "pointer_ABA_copies": len(examples),
            "example_schedules": examples}


def main():
    intact = explore(True, True, True)
    missing_defer = explore(True, False, True)
    unmarked_donor = explore(False, True, True)
    # This controlled abstract model specifically checks pointer lifetime.
    assert intact["pointer_ABA_copies"] == 0, intact
    assert missing_defer["pointer_ABA_copies"] > 0, missing_defer
    # If a reader first sees an unmarked donor, it can later see a marked,
    # reinserted generation under same address: needs separate ordering review.
    assert unmarked_donor["pointer_ABA_copies"] > 0, unmarked_donor
    report = {
      "device": "Samsung SM-T311 Cortex-A9 ARMv7",
      "model": "abstract sequentially consistent event ordering, NOT LKMM/herd7",
      "flagged_and_deferred": intact,
      "negative_control_no_defer": missing_defer,
      "unmarked_donor_then_marked_reinsertion": unmarked_donor,
      "interpretation": [
        "Flagged folio deferred reuse prevents same-physical-address generation ABA in this toy model.",
        "Removing deferred reuse admits accepted pointer-ABA histories; not proof of actual kernel execution.",
        "An initially unmarked folio can be recycled and a new folio flagged before the stale lookup checks the flag, yielding pointer-ABA histories; whether the real kernel allows a dangerous instance requires ARMv7 LKMM/writer proof.",
        "A stale pointer-ABA observation is not in itself proof of corrupt application data. This model omits inode, i_size, xarray locks, cache-coherency and barriers."
      ],
      "safety_gate": "S2 MUST REMAIN BLOCKED until LKMM + real writer/reader review is complete"
    }
    OUT.write_text(json.dumps(report, indent=2) + "\n")
    for k in ("flagged_and_deferred", "negative_control_no_defer", "unmarked_donor_then_marked_reinsertion"):
        print(k, report[k]["tested_interleavings"], "ABA sample count", report[k]["pointer_ABA_copies"])
    print("G653 ABSTRACT MODEL NEGATIVE CONTROLS PASS — NOT ARMv7 PROOF")


if __name__ == "__main__":
    main()
