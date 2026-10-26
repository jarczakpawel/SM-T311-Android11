#!/usr/bin/env python3
"""G676 source gate: RCU callback-to-workqueue completion in rcu_free_sync().

A registered rcu_work may wait for a grace period BEFORE its embedded
work_struct is queued. flush_work() is not the correct wait contract in
that state; kernel/workqueue.c provides flush_rcu_work(). Pure host test,
not a physical ARMv7 concurrency test or proof of lockless reader safety.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "Documentation/mm/sm-t311-rcu-work-completion-g676.json"
PATHS = (
    "mm/page_alloc.c", "kernel/workqueue.c", "mm/memory_hotplug.c",
    "mm/memory-failure.c", "include/linux/workqueue.h", "mm/internal.h",
)
SOURCES = {k: (ROOT / k).read_text() for k in PATHS}


def section(body: str, a: str, b: str) -> str:
    assert a in body and b in body, (a, b)
    return body.split(a, 1)[1].split(b, 1)[0]


def has_order(body: str, *args: str) -> bool:
    indexes = [body.find(s) for s in args]
    return all(i >= 0 for i in indexes) and indexes == sorted(indexes)


def checks(data):
    a, wq, hotplug, failure, api, headers = (data[p] for p in PATHS)
    start = section(a, "static void rcu_free_start(", "\nstatic void rcu_free_take(")
    work = section(a, "static void rcu_free_workfn(", "\nstatic void rcu_free_expedite_workfn(")
    free = section(a, "static void rcu_free_defer(", "\nstatic void rcu_free_drain(")
    drain = section(a, "static void rcu_free_drain(", "\n/* Free every waiting folio")
    sync = section(a, "void rcu_free_sync(", "\nstatic void __init rcu_free_init(")
    work_queue = section(wq, "bool queue_rcu_work(", "\nEXPORT_SYMBOL(queue_rcu_work);")
    callback = section(wq, "static void rcu_work_rcufn(", "\n/**")
    flush = section(wq, "bool flush_rcu_work(", "\nEXPORT_SYMBOL(flush_rcu_work);")
    memfail = section(failure, "rcu_free_sync();", "\n")
    return {
        "queue_rcu_work_uses_call_rcu_hurry":
            "call_rcu_hurry(&rwork->rcu, rcu_work_rcufn);" in work_queue,
        "queue_rcu_work_sets_pending_before_rcu_callback":
            has_order(work_queue, "test_and_set_bit(WORK_STRUCT_PENDING_BIT",
                      "call_rcu_hurry(&rwork->rcu, rcu_work_rcufn);"),
        "rcu_callback_queues_embedded_work_only_after_grace":
            "__queue_work(WORK_CPU_UNBOUND, rwork->wq, &rwork->work);" in callback,
        "api_flush_rcu_work_accounts_for_pending_callback":
            has_order(flush, "test_bit(WORK_STRUCT_PENDING_BIT",
                      "rcu_barrier();", "flush_work(&rwork->work);"),
        "api_exposes_flush_rcu_work":
            "extern bool flush_rcu_work(struct rcu_work *rwork);" in api,
        "allocator_uses_one_inflight_callback_per_cpu":
            "if (r->queued || list_empty(&r->pending))" in start,
        "allocator_queues_callback_under_percpu_lock":
            has_order(start, "r->queued = true;",
                      "queue_rcu_work(rcu_free_wq, &r->rwork)"),
        "allocator_backlog_accounting":
            all(s in free for s in ("r->nr_pending + r->nr_batch",
                                     "RCU_FREE_EXPEDITE", "NR_FREE_RCU_PAGES")),
        "worker_take_batch_under_spinlock":
            has_order(work, "spin_lock_irq(&r->lock);",
                      "list_splice_init(&r->batch, &done);",
                      "r->queued = false;", "rcu_free_start(r);",
                      "spin_unlock_irq(&r->lock);",
                      "rcu_free_pages(&done);"),
        "drain_uses_mutex_and_waits_rcu_grace":
            has_order(drain, "mutex_lock(&lock);", "rcu_free_take(r, &list);",
                      "rcu_free_flush(&list)", "mutex_unlock(&lock);"),
        "sync_drains_before_wait_for_rcu_work":
            has_order(sync, "rcu_free_drain();",
                      "flush_rcu_work(&r->rwork);",
                      "flush_work(&r->expedite);"),
        "sync_does_not_use_generic_flush_for_rcuwork":
            "flush_work(&r->rwork.work);" not in sync,
        "sync_includes_all_possible_cpu_rcu_workers":
            "for_each_possible_cpu(cpu)" in sync,
        "hotplug_uses_rcu_free_sync":
            "rcu_free_sync();" in hotplug,
        "memory_failure_uses_rcu_free_sync":
            "rcu_free_sync();" in failure and
            "if (!res && folio_test_rcu_free(folio))" in failure,
        "source_respects_page_tag_free_soft_cap":
            "SM_T311_RCU_PENDING_SOFT_LIMIT" in headers,
    }


def wait_model(use_specialized: bool, registered: bool, work_enqueued: bool):
    """Coarse callback state machine, NOT a kernel scheduler simulation."""
    events = ["rcu_free_drain", "begin_per_cpu_wait"]
    if not registered:
        return {"returned": True, "rcu_callback_complete": True,
                "work_done": True, "events": events + ["already_idle"]}
    if work_enqueued:
        return {"returned": True, "rcu_callback_complete": True,
                "work_done": True, "events": events + ["flush_work_wait"]}
    if use_specialized:
        events += ["rcu_barrier_wait", "rcu_callback_queues_work",
                   "flush_work_wait", "return"]
        return {"returned": True, "rcu_callback_complete": True,
                "work_done": True, "events": events}
    # Ordinary flush_work() does not promise to wait for the
    # RCU callback that has yet to queue the embedded work.
    return {"returned": True, "rcu_callback_complete": False,
            "work_done": False, "events": events + ["generic_flush_cannot_guarantee_rcu_callback"]}


def main():
    valid = checks(SOURCES)
    bad = [name for name, yes in valid.items() if not yes]
    assert len(valid) == 16, len(valid)
    if bad:
        raise AssertionError("source gate failed: " + repr(bad))
    mutants = {
        "revert_to_generic_flush": ("mm/page_alloc.c",
                                    "flush_rcu_work(&r->rwork);",
                                    "flush_work(&r->rwork.work);"),
        "remove_rcu_barrier": ("kernel/workqueue.c",
                               "rcu_barrier();", ""),
        "remove_queue_gp": ("kernel/workqueue.c",
                            "call_rcu_hurry(&rwork->rcu, rcu_work_rcufn);", ""),
        "remove_drain": ("mm/page_alloc.c",
                         "rcu_free_drain();", ""),
        "skip_expedite_completion": ("mm/page_alloc.c",
                                       "flush_work(&r->expedite);", ""),
    }
    rejected = {}
    for label, (filename, needle, replacement) in mutants.items():
        if needle not in SOURCES[filename]:
            raise AssertionError("mutant precondition missing: " + label)
        copy = dict(SOURCES)
        copy[filename] = copy[filename].replace(needle, replacement, 1)
        failed = [k for k, v in checks(copy).items() if not v]
        if not failed:
            raise AssertionError("mutant escaped: " + label)
        rejected[label] = failed
    generic = wait_model(False, True, False)
    specialized = wait_model(True, True, False)
    assert not generic["rcu_callback_complete"]
    assert specialized["rcu_callback_complete"] and specialized["work_done"]
    assert wait_model(True, True, True)["work_done"]
    assert wait_model(True, False, False)["work_done"]
    output = {
        "stage": "G676",
        "source_assertions_pass": len(valid),
        "source_assertions": valid,
        "modified_source_rejected": rejected,
        "example_registered_but_work_not_enqueued": {
            "generic_flush_work": generic,
            "correct_flush_rcu_work": specialized,
        },
        "limits": [
            "Toy model is API-contract documentation, not CPU race reproduction.",
            "The queued callback may be waiting on RCU when generic flush_work is called.",
            "No physical hung flush, UAF, memory corruption or demonstrated exploit.",
            "flush_rcu_work includes rcu_barrier and can be more expensive in rare synchronizing paths.",
            "Current G674/G676 executable changes are STAGED NOT INSTALLED.",
            "S1 ACTIVE, S2/S3 BLOCKED; no-ref RCU must stay DEFAULT OFF.",
        ],
    }
    OUT.write_text(json.dumps(output, indent=2) + "\n")
    print(f"G676: {len(valid)}/16 RCU callback/workqueue source checks PASS, "
          f"{len(rejected)}/5 real source mutations rejected")
    print("queue_rcu_work pending GP -> flush_rcu_work required for explicit sync; "
          "not full lifetime proof")


if __name__ == "__main__":
    main()
