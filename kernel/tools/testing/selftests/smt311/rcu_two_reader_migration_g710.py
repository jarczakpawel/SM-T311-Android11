#!/usr/bin/env python3
"""G710: bounded two-reader RCU lifetime counterexamples and migration/split gates.

No kernel runtime code, no weak-memory proof: sequentially-consistent abstraction.
"""
from __future__ import annotations
from dataclasses import dataclass, replace
from collections import deque
from pathlib import Path
from hashlib import sha256
import json
import re

ROOT = Path(__file__).resolve().parents[4]
PROJECT = ROOT.parent
PREV = json.loads((ROOT / "Documentation/mm/sm-t311-rcu-generation-g709.json").read_text())
SOURCE = {x: (ROOT / x).read_text() for x in
          ("mm/filemap.c", "mm/page_alloc.c", "mm/migrate.c", "mm/huge_memory.c", "mm/vma.c")}
READ_STEPS = ("enter", "load", "check", "reload_copy", "exit")
WRITE_STEPS = ("remove", "retire", "reuse", "insert", "tag")


@dataclass(frozen=True)
class Reader:
    step: int = 0
    seen: int = 0
    checked: bool = False
    raw_mismatch: bool = False
    raw_freed: bool = False
    pin: int = 0
    copied: bool = False
    unsafe: bool = False


@dataclass(frozen=True)
class State:
    readers: tuple[Reader, Reader] = (Reader(), Reader())
    wstep: int = 0
    slot: int = 1
    current: int = 1
    tagged: bool = False
    deferred: bool = False
    reuse_permitted: bool = False
    stale_read: bool = False


def step_reader(s: State, who: int, policy: str, *, enforce_epoch=True, enforce_ref=True):
    r = s.readers[who]
    action = READ_STEPS[r.step]
    new = r
    if action == "enter":
        pass
    elif action == "load":
        new = replace(r, seen=s.slot)
    elif action == "check":
        if r.seen:
            if policy == "reference" and enforce_ref:
                # A valid pinned folio has a lifetime until release.
                new = replace(r, pin=s.current if s.current > 0 else 0,
                              checked=s.current > 0)
            else:
                new = replace(
                    r,
                    checked=s.current > 0 and s.tagged,
                    raw_mismatch=r.raw_mismatch or (s.current > 0 and s.current != r.seen),
                    raw_freed=r.raw_freed or s.current == 0)
    elif action == "reload_copy":
        if r.checked and s.slot and s.current:
            if policy == "reference" and enforce_ref:
                ok = r.pin == s.slot and s.current == r.pin
                unsafe = False
            elif policy == "slot_epoch" and enforce_epoch:
                ok = s.slot == r.seen
                unsafe = False
            else:
                ok = True  # raw same-PFN equality, no identity epoch
                unsafe = s.current != r.seen
            new = replace(r, copied=ok, unsafe=r.unsafe or (ok and unsafe))
    elif action == "exit":
        new = replace(r, pin=0)
    new = replace(new, step=r.step+1)
    rr = list(s.readers)
    rr[who] = new
    return replace(s, readers=tuple(rr))


def step_writer(s: State, policy: str, *, grace_all=True, graces_two=True):
    name = WRITE_STEPS[s.wstep]
    if name == "remove":
        return replace(s, slot=0, wstep=s.wstep+1)
    if name == "retire":
        deferred = policy == "universal_defer" or s.tagged or any(r.pin == 1 for r in s.readers)
        return replace(s, current=1 if deferred else 0, deferred=deferred,
                       wstep=s.wstep+1)
    if name == "reuse":
        # An RCU grace protects the *old* retiring folio only if it was
        # deferred. Every reader that entered before retirement must exit.
        active = [r.step in (1, 2, 3, 4) for r in s.readers]
        if s.deferred and grace_all:
            protected = any(active) if graces_two else active[0]
            if protected:
                return None
        if any(r.pin == 1 and r.step != 5 for r in s.readers):
            return None
        return replace(s, current=2, deferred=False, reuse_permitted=True,
                       wstep=s.wstep+1)
    if name == "insert":
        if s.current != 2:
            return None
        return replace(s, slot=2, wstep=s.wstep+1)
    if name == "tag":
        return replace(s, tagged=True, wstep=s.wstep+1)
    raise RuntimeError(name)


def explore(policy, *, old_tag=False, grace_all=True, graces_two=True,
            enforce_epoch=True, enforce_ref=True):
    start=State(tagged=old_tag)
    todo=deque([start])
    prior={start: None}
    edges={}
    terminals=0
    unsafe_states=[]
    mismatches=[]
    freed=[]
    waited=0
    max_frontier=0
    while todo:
        max_frontier=max(max_frontier,len(todo))
        s=todo.popleft()
        if s.wstep==5 and all(r.step==5 for r in s.readers):
            terminals+=1
            if any(r.unsafe for r in s.readers):
                unsafe_states.append(s)
            if any(r.raw_mismatch for r in s.readers):
                mismatches.append(s)
            if any(r.raw_freed for r in s.readers):
                freed.append(s)
            continue
        next_states=[]
        for i,r in enumerate(s.readers):
            if r.step<5:
                next_states.append((f"R{i}.{READ_STEPS[r.step]}",
                    step_reader(s,i,policy,enforce_epoch=enforce_epoch,enforce_ref=enforce_ref)))
        if s.wstep<5:
            w=step_writer(s,policy,grace_all=grace_all,graces_two=graces_two)
            if w is not None:
                next_states.append(("W."+WRITE_STEPS[s.wstep],w))
            else:
                waited+=1
        for action,n in next_states:
            if n not in prior:
                prior[n]=s
                edges[n]=action
                todo.append(n)
    def witness(seq):
        if not seq:
            return None
        state=seq[0]; steps=[]
        while prior[state] is not None:
            steps.append(edges[state])
            state=prior[state]
        return steps[::-1]
    return {
        "reachable_states":len(prior),
        "terminal_states":terminals,
        "unsafe_terminal_states":len(unsafe_states),
        "raw_new_generation_terminal_states":len(mismatches),
        "raw_freed_terminal_states":len(freed),
        "rcu_wait_blocked_edges":waited,
        "max_frontier":max_frontier,
        "unsafe_witness":witness(unsafe_states),
        "raw_mismatch_witness":witness(mismatches),
    }


def anchored_checks():
    f=SOURCE["mm/filemap.c"]
    alloc=SOURCE["mm/page_alloc.c"]
    migr=SOURCE["mm/migrate.c"]
    split=SOURCE["mm/huge_memory.c"]
    b686=(PROJECT/"build/g686-reproducible-exp-on/.config").read_text()
    b700=(PROJECT/"build/g700-debugfs-rcu-off/.config").read_text()
    def named_body(text, begin, end):
        assert begin in text and end in text and text.index(begin)<text.index(end)
        return text[text.index(begin):text.index(end)]
    flag_move=named_body(migr,"void folio_migrate_flags(", "EXPORT_SYMBOL(folio_migrate_flags)")
    flag_split=split[split.index("static void __split_folio_to_order("):]; flag_split=flag_split[:flag_split.index("\nstatic ", 20)]
    return {
        "G709_previous_model_valid":all(PREV["source_checks"].values()),
        "RCU_reader_uses_unpinned_flag_and_slot_recheck":(
            "folio = xas_load(xas);" in f and
            "READ_ONCE(folio->flags.f) & __PG_RCU_FREE" in f and
            "if (xas_reload(xas) != folio)" in f),
        "RCU_reader_is_gated_to_order0_and_Normal":(
            "folio_test_large(folio)" in f and "folio_zonenum(folio) != ZONE_NORMAL" in f),
        "folio_migration_replaces_XArray_slot":(
            "xas_store(&xas, dst);" in migr and
            "folio_ref_unfreeze(src," in migr),
        "migration_copies_flags_by_named_accessors":(
            "folio_migrate_flags(dst, src);" in migr and
            "folio_mark_uptodate(newfolio)" in flag_move and
            "folio_set_workingset(newfolio)" in flag_move),
        "migration_does_not_explicitly_transfer_rcu_free_tag":(
            "rcu_free" not in flag_move and "__PG_RCU_FREE" not in flag_move),
        "migration_moves_source_to_new_folio_and_releases_old":(
            "move_to_new_folio(dst, src, mode)" in migr and
            "migrate_folio_done(src, reason)" in migr),
        "large_folio_split_copies_rcu_free_flag":(
            "__PG_RCU_FREE |" in flag_split and
            "new_folio->flags.f |=" in flag_split),
        "large_folio_split_contains_tail_memory_metadata_operations":(
            "clear_compound_head(new_head)" in flag_split and
            "smp_wmb();" in flag_split),
        "preexisting_larger_folios_excluded_from_SMT311_reader":(
            "if (folio_test_large(folio) ||" in f or
            "if (folio_test_large(folio) ||\n" in f),
        "allocator_still_queues_only_tagged_folio":(
            "if (unlikely(folio_test_rcu_free(folio))" in alloc
            and "rcu_free_defer(folio);" in alloc),
        "experiment_exp_only_G700_OFF":(
            "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" in b686
            and "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" not in b700),
        "G692_activation_still_failclosed":(
            "if (val)\n\t\treturn -EOPNOTSUPP;" in f),
    }


def main():
    programs={
        "two_readers_conditional_old_untagged":explore("conditional"),
        "two_readers_conditional_old_tagged":explore("conditional",old_tag=True),
        "two_readers_universal_defer_old_untagged":explore("universal_defer"),
        "two_readers_atomic_slot_epoch_old_untagged":explore("slot_epoch"),
        "two_readers_refcounted_old_untagged":explore("reference"),
    }
    mutants={
        "universal_defer_grace_omitted":explore("universal_defer",grace_all=False),
        "universal_defer_grace_only_reader0":explore("universal_defer",graces_two=False),
        "slot_epoch_verification_omitted":explore("slot_epoch",enforce_epoch=False),
        "refcount_pin_omitted":explore("reference",enforce_ref=False),
    }
    checks={
        **anchored_checks(),
        "two_readers_old_untagged_conditional_exposes_ABA":programs["two_readers_conditional_old_untagged"]["unsafe_terminal_states"]>0,
        "two_readers_tagged_old_safe_in_this_model":programs["two_readers_conditional_old_tagged"]["unsafe_terminal_states"]==0,
        "two_readers_universal_defer_safe_in_this_model":programs["two_readers_universal_defer_old_untagged"]["unsafe_terminal_states"]==0,
        "two_readers_universal_defer_has_no_raw_reused_read":programs["two_readers_universal_defer_old_untagged"]["raw_new_generation_terminal_states"]==0,
        "two_readers_epoch_prevents_acceptance_only":programs["two_readers_atomic_slot_epoch_old_untagged"]["unsafe_terminal_states"]==0,
        "two_readers_epoch_does_not_prevent_raw_generation_dereference":programs["two_readers_atomic_slot_epoch_old_untagged"]["raw_new_generation_terminal_states"]>0,
        "two_readers_reference_path_safe_in_this_model":programs["two_readers_refcounted_old_untagged"]["unsafe_terminal_states"]==0,
        "all_mutants_reopen_ABA":all(x["unsafe_terminal_states"]>0 for x in mutants.values()),
        "all_model_runs_have_terminal_states":all(x["terminal_states"]>0 for x in (*programs.values(),*mutants.values())),
    }
    normal_managed=203153
    normal_free=75863
    scenarios=[]
    for pages in (1024,4096,8192,16384,32768):
        scenarios.append({
            "assumed_4K_pages_deferred":pages,
            "held_MiB":pages/256,
            "percent_of_G709_sampled_Normal_free":round(100*pages/normal_free,2),
            "percent_of_G709_sampled_Normal_managed":round(100*pages/normal_managed,2),
        })
    report={
        "stage":"G710",
        "checks":checks,
        "protocols":programs,
        "broken_controls":mutants,
        "migration_split_findings":{
            "migrate_folio_replaces_XArray_and_handles_flag_transfer":True,
            "folio_migrate_flags_explicitly_transfers_PG_rcu_free":False,
            "large_folio_split_copies___PG_RCU_FREE":True,
            "SMT311_reader_excludes_large_folios":True,
            "migration_and_split_end_to_end_RCU_proof":False,
        },
        "memory_envelopes_relative_to_old_passive_snapshot":scenarios,
        "limitations":[
            "Breadth-first state exploration with two R readers and one W but sequentially consistent actions, NOT Linux LKMM/herd7 nor ARMv7 weak ordering.",
            "Two reader identity checks share one XArray slot and one physical address, only two incarnations; real migration, splitting and arbitrary free paths are source-reviewed but NOT simulated.",
            "The model's successful reference protocol is idealized; the Linux folio refcount discipline must remain authoritative.",
            "The grace-period wait assumes all active readers are included and does NOT establish scheduler or lockdep correctness.",
            "Zone snapshot is G709 historical read-only data; none of the hypothetical deferred-memory volumes is a measurement or actual RCU throughput.",
        ],
        "safety_decision":"KEEP_FAIL_CLOSED_NO_REF_RCU_OFF",
        "no_ref_RCU_ON_approved":False,
        "new_executable_kernel_runtime_modifications":False,
        "physical_flash_authorized":False,
        "G700_8MiB_host_BOOT_sha256":sha256((PROJECT/"artifacts/G700-RCU-OFF-DEBUGFS-HOST-ONLY/BOOT-G700-RCU-OFF-DEBUGFS-HOST-ONLY-DO-NOT-FLASH.img").read_bytes()).hexdigest(),
    }
    print(json.dumps(report,indent=2,ensure_ascii=False))
    if not all(checks.values()):
        raise SystemExit(1)

if __name__=="__main__":
    main()
