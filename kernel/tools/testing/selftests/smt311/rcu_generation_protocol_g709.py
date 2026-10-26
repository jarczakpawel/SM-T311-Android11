#!/usr/bin/env python3
"""G709 - bounded interleaving comparisons for SM-T311 PFN ABA repair ideas.

Pure host-side exploratory state machine, NOT a Linux RCU / LKMM proof,
NOT a replacement for G659/G691. It compares proposed *protocols*.
"""
from dataclasses import dataclass, replace
from pathlib import Path
import hashlib
import json
import math

KERNEL = Path(__file__).resolve().parents[4]
PROJECT = KERNEL.parent
R_STEPS = ("R_enter", "R_load", "R_guard", "R_reload", "R_copy", "R_exit")
W_STEPS = ("W_remove", "W_retire", "W_reuse", "W_insert", "W_mark")


@dataclass(frozen=True)
class State:
    slot: int = 1           # 0 = absent, 1 = old incarnation, 2 = new
    live: int = 1           # 0 = free, 1/2 = physically live at SAME PFN
    old_tag: bool = False   # initial PG_rcu_free of generation 1
    new_tag: bool = False
    deferred: bool = False
    reader_active: bool = False
    seen: int = 0
    guard_ok: bool = False
    reloaded: bool = False
    pin: int = 0
    raw_generation_mismatch: bool = False
    raw_after_free: bool = False
    copied: bool = False
    aba_accepted: bool = False


def do_step(state, action, policy, *, enforce_grace=True,
            enforce_slot_epoch=True, enforce_ref_pin=True):
    s = state
    if action == "R_enter":
        return replace(s, reader_active=True)
    if action == "R_load":
        return replace(s, seen=s.slot)
    if action == "R_guard":
        if not s.seen:
            return s
        if policy == "reference" and enforce_ref_pin:
            # The real speculative refcount operation may pin a NEW incarnation.
            # A subsequent slot reload must validate this referenced object.
            if not s.live:
                return s
            return replace(s, pin=s.live, guard_ok=True)
        # Unpinned PG_rcu_free read: can inspect a different incarnation from
        # the pointer loaded from the XArray before physical reclamation.
        bad_gen = s.live != s.seen and s.live != 0
        freed = s.live == 0
        tagged = ((s.live == 1 and s.old_tag) or
                  (s.live == 2 and s.new_tag))
        return replace(s, guard_ok=tagged,
                       raw_generation_mismatch=s.raw_generation_mismatch or bad_gen,
                       raw_after_free=s.raw_after_free or freed)
    if action == "R_reload":
        if not s.guard_ok:
            return s
        ok = s.slot != 0
        if policy == "reference" and enforce_ref_pin:
            ok = ok and s.live == s.pin and s.slot == s.pin
        elif policy == "slot_epoch" and enforce_slot_epoch:
            # The token needs an actual atomic snapshot of (pointer, epoch)
            # on LOAD. An isolated page->generation is not enough.
            ok = ok and s.slot == s.seen
        # Baseline raw pointer equality checks just the SAME PFN, not the gen.
        return replace(s, reloaded=ok)
    if action == "R_copy":
        if not s.reloaded or not s.live:
            return s
        # Reference-backed read can legally return a new, pinned current
        # incarnation after the original load; it never treats it as OLD.
        aba = (policy != "reference" or not enforce_ref_pin) and s.seen != s.live
        return replace(s, copied=True, aba_accepted=aba)
    if action == "R_exit":
        return replace(s, reader_active=False, pin=0)

    if action == "W_remove":
        if s.slot != 1:
            return None
        return replace(s, slot=0)
    if action == "W_retire":
        if s.slot or s.live != 1:
            return None
        # Universally deferred means every potentially sampled old folio
        # must be protected, including initially UNTAGGED incarnations.
        pending = (policy == "universal_defer" or s.old_tag or s.pin == 1)
        return replace(s, live=1 if pending else 0, deferred=pending)
    if action == "W_reuse":
        if s.slot or (s.live == 1 and not s.deferred):
            return None
        if enforce_grace and s.deferred and s.reader_active:
            return None
        if s.pin == 1:
            return None
        return replace(s, live=2, deferred=False)
    if action == "W_insert":
        if s.slot or s.live != 2:
            return None
        return replace(s, slot=2)
    if action == "W_mark":
        if s.slot != 2 or s.live != 2:
            return None
        return replace(s, new_tag=True)
    raise ValueError(action)


def explore(policy, initial_tag, **overrides):
    # Enumerate EVERY sequentially-consistent order-preserving interleaving
    # of these exact six R and five W milestones, with guarded writer reuse.
    result = {
        "completed_interleavings": 0,
        "accepted_reads": 0,
        "ABA_accepted": 0,
        "raw_dereference_other_generation": 0,
        "raw_dereference_after_free": 0,
        "RCU_wait_blocked_schedule_edges": 0,
        "ABA_witness": None,
        "generation_mismatch_witness": None,
    }

    def visit(ri, wi, s, path):
        if ri == len(R_STEPS) and wi == len(W_STEPS):
            result["completed_interleavings"] += 1
            if s.copied:
                result["accepted_reads"] += 1
            if s.aba_accepted:
                result["ABA_accepted"] += 1
                if result["ABA_witness"] is None:
                    result["ABA_witness"] = path
            if s.raw_generation_mismatch:
                result["raw_dereference_other_generation"] += 1
                if result["generation_mismatch_witness"] is None:
                    result["generation_mismatch_witness"] = path
            if s.raw_after_free:
                result["raw_dereference_after_free"] += 1
            return
        if ri < len(R_STEPS):
            name = R_STEPS[ri]
            child = do_step(s, name, policy, **overrides)
            if child is not None:
                visit(ri + 1, wi, child, path + [name])
        if wi < len(W_STEPS):
            name = W_STEPS[wi]
            child = do_step(s, name, policy, **overrides)
            if child is not None:
                visit(ri, wi + 1, child, path + [name])
            elif name == "W_reuse" and s.deferred and s.reader_active:
                result["RCU_wait_blocked_schedule_edges"] += 1

    visit(0, 0, State(old_tag=initial_tag), [])
    return result


def validate_source():
    fmap = (KERNEL / "mm/filemap.c").read_text()
    alloc = (KERNEL / "mm/page_alloc.c").read_text()
    exp = (PROJECT / "build/g686-reproducible-exp-on/.config").read_text()
    off = (PROJECT / "build/g700-debugfs-rcu-off/.config").read_text()
    return {
        "raw_load_and_unpinned_flag_source": (
            "folio = xas_load(xas);" in fmap and
            "READ_ONCE(folio->flags.f) & __PG_RCU_FREE" in fmap and
            "if (xas_reload(xas) != folio)" in fmap
        ),
        "marked_only_allocator_deferral": (
            "if (unlikely(folio_test_rcu_free(folio))" in alloc
            and "rcu_free_defer(folio);" in alloc
            and "trace_mm_page_free(page, order);" in alloc
        ),
        "speculative_reference_fallback": (
            "if (!folio_try_get(folio))" in fmap
            and "if (unlikely(folio != xas_reload(&xas)))" in fmap
            and "folio_put(folio);" in fmap
        ),
        "staged_debugfs_no_ref_enable_denied": (
            "if (val)\n\t\treturn -EOPNOTSUPP;" in fmap
            and "DEFINE_STATIC_KEY_FALSE(lockless_read_key)" in fmap
        ),
        "config_EXP_compiled_release_OFF": (
            "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" in exp
            and "CONFIG_PAGE_RCU_FREE=y" in exp
            and "CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y" not in off
            and "CONFIG_PAGE_RCU_FREE=y" not in off
        ),
    }


def main():
    prior = json.loads((KERNEL / "Documentation/mm/sm-t311-rcu-lifetime-g708.json").read_text())
    rows = {
        "current_conditional_old_untagged": explore("conditional", False),
        "current_conditional_old_tagged": explore("conditional", True),
        "candidate_defer_every_sampled_old": explore("universal_defer", False),
        "candidate_atomic_slot_epoch_without_retire_lifetime": explore("slot_epoch", False),
        "known_good_refcounted_fallback": explore("reference", False),
    }
    mutants = {
        "omit_all_folio_RCU_grace": explore("universal_defer", False, enforce_grace=False),
        "drop_slot_epoch_compare": explore("slot_epoch", False, enforce_slot_epoch=False),
        "drop_reference_pin": explore("reference", False, enforce_ref_pin=False),
    }
    snap = {
        "source": "read-only physical Android G650 /proc/zoneinfo and /proc/meminfo snapshot 2026-10-21",
        "MemTotal_kB": 1483332,
        "Normal_managed_pages": 203153,
        "Normal_free_pages": 75863,
        "Normal_high_watermark_pages": 1350,
        "note": "Snapshots vary. This is NOT the available pages for a new kernel image; total memory and zone managed/free must not be conflated.",
    }
    bursts = []
    for pages in (1024, 4096, 16384):
        mib = pages * 4096 / (1024 * 1024)
        bursts.append({
            "deferred_pages": pages,
            "held_MiB": mib,
            "fraction_of_sampled_Normal_managed_percent": round(100 * pages / snap["Normal_managed_pages"], 2),
            "fraction_of_sampled_Normal_free_percent": round(100 * pages / snap["Normal_free_pages"], 2),
        })
    rate_envelopes = []
    for rate, grace_ms in ((100, 10), (1000, 100), (10000, 500)):
        pages = math.ceil(rate * grace_ms / 1000)
        rate_envelopes.append({
            "assumed_retired_pages_per_second": rate,
            "assumed_grace_ms": grace_ms,
            "estimated_concurrent_pending_pages": pages,
            "estimated_held_MiB": round(pages / 256, 3),
        })
    checks = {
        **validate_source(),
        "G708_previous_new_safety_anchors_passed": all(prior["checks"].values()) and all(prior["negative_controls"].values()),
        "baseline_untagged_allows_same_PFN_generation_ABA": rows["current_conditional_old_untagged"]["ABA_accepted"] > 0,
        "baseline_tagged_has_no_ABA_in_THIS_model": rows["current_conditional_old_tagged"]["ABA_accepted"] == 0,
        "full_lifetime_defer_eliminates_ABA_in_THIS_model": rows["candidate_defer_every_sampled_old"]["ABA_accepted"] == 0,
        "full_lifetime_defer_no_raw_mismatch_in_THIS_model": rows["candidate_defer_every_sampled_old"]["raw_dereference_other_generation"] == 0 and rows["candidate_defer_every_sampled_old"]["raw_dereference_after_free"] == 0,
        "full_lifetime_defer_blocks_some_writer_schedules": rows["candidate_defer_every_sampled_old"]["RCU_wait_blocked_schedule_edges"] > 0,
        "slot_epoch_catches_ABA_acceptance_in_THIS_model": rows["candidate_atomic_slot_epoch_without_retire_lifetime"]["ABA_accepted"] == 0,
        "slot_epoch_STILL_dereferences_reused_incarnation": rows["candidate_atomic_slot_epoch_without_retire_lifetime"]["raw_dereference_other_generation"] > 0,
        "refcounted_reader_never_accepts_invalid_generation_in_model": rows["known_good_refcounted_fallback"]["ABA_accepted"] == 0,
        "refcounted_reader_does_not_raw_check_freed_folio_in_model": rows["known_good_refcounted_fallback"]["raw_dereference_after_free"] == 0,
        "mutant_skipping_universal_grace_reopens_ABA": mutants["omit_all_folio_RCU_grace"]["ABA_accepted"] > 0,
        "mutant_missing_epoch_recheck_reopens_ABA": mutants["drop_slot_epoch_compare"]["ABA_accepted"] > 0,
        "mutant_missing_refcount_reopens_ABA": mutants["drop_reference_pin"]["ABA_accepted"] > 0,
        "all_profiles_have_completed_schedules": all(x["completed_interleavings"] > 0 for x in (*rows.values(), *mutants.values())),
        "physical_Normal_zone_sample_nonzero": snap["Normal_free_pages"] > snap["Normal_high_watermark_pages"] > 0,
        "pinned_G700_host_boot_hash_unchanged": hashlib.sha256(
            (PROJECT / "artifacts/G700-RCU-OFF-DEBUGFS-HOST-ONLY/BOOT-G700-RCU-OFF-DEBUGFS-HOST-ONLY-DO-NOT-FLASH.img").read_bytes()
        ).hexdigest() == "aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f",
    }
    report = {
        "stage": "G709",
        "model": "exhaustive sequentially-consistent schedule interleavings of one reader and one writer with two physical incarnations of the SAME PFN, not LKMM or an end-to-end kernel proof",
        "source_checks": checks,
        "protocol_comparisons": rows,
        "deliberately_broken_protocols": mutants,
        "physical_snapshot": snap,
        "illustrative_burst_costs": bursts,
        "illustrative_retire_rate_grace_costs": rate_envelopes,
        "decision": "KEEP_REFCOUNTED_RELEASE_AND_NO_REF_ON_FAIL_CLOSED",
        "candidate_priority": "A universal RCU lifetime protocol is worth proving but has real low-memory implications; a standalone atomic slot epoch blocks acceptance only, not stale unpinned memory dereferences; reference-backed fallback is the safe supported production path.",
        "remaining_gates": ["S1 reader/writer complete Linux LKMM", "S2 folio recycling across ALL free paths", "S3 HighMem/VIPT/mmap/lockdep", "S6 verified physical TWRP/BOOT"],
        "rcu_ON_approved": False,
        "new_runtime_kernel_code": False,
        "G700_physically_installed": False,
        "flash_authorized": False,
    }
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if not all(checks.values()):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
