#!/usr/bin/env python3
"""G677 bounded two-CPU RCU deferred-free queue ownership model.

This enumerates abstract spinlocked transitions, GP callbacks, work, expedite,
and drain. It tests list ownership/accounting, not ARMv7 weak-memory ordering,
Linux workqueue scheduling, OOM behavior or full no-ref reader correctness.
"""
from collections import deque
from dataclasses import dataclass, replace
from pathlib import Path
import json

KERNEL = Path(__file__).resolve().parents[4]
OUTPUT = KERNEL / "Documentation/mm/sm-t311-rcu-queue-interleavings-g677.json"
SRC = (KERNEL / "mm/page_alloc.c").read_text()
WQ = (KERNEL / "kernel/workqueue.c").read_text()
SCHEDULED = 0b1111
CPU_IDS = ((0, 2), (1, 3))
# rcuw: 0 idle, 1 callback awaiting GP, 2 callback finished, work ready
@dataclass(frozen=True)
class CPU:
    pending: int = 0
    batch: int = 0
    rcuw: int = 0
    expedited: int = -1  # -1 idle; >=0 detached mask awaiting GP

@dataclass(frozen=True)
class S:
    cores: tuple[CPU, CPU] = (CPU(), CPU())
    issued: int = 0
    freed: int = 0
    drain: int = -1  # -1 not started, -2 done, >=0 detached mask waiting for GP

def has_source_gate(text):
    keys = {
        "rcu_tag_account_before_pending_publish":
            text.index("__mod_zone_page_state(zone, NR_FREE_RCU_PAGES") <
            text.index("list_add_tail(&folio->lru, &r->pending);"),
        "per_cpu_spinlock_on_defer":
            "spin_lock_irqsave(&r->lock, flags);" in text,
        "start_transfers_pending_to_batch":
            "list_splice_init(&r->pending, &r->batch);" in text and
            "r->nr_batch = r->nr_pending;" in text,
        "start_one_gp_per_cpu":
            "if (r->queued || list_empty(&r->pending))" in text,
        "gp_callbacks_queue_rcu_work":
            "queue_rcu_work(rcu_free_wq, &r->rwork)" in text,
        "worker_removes_batch_under_lock":
            "list_splice_init(&r->batch, &done);" in text,
        "worker_new_gp_before_free":
            "r->queued = false;\n\trcu_free_start(r);" in text,
        "worker_pages_return_after_gp":
            "rcu_free_pages(&done);" in text,
        "expedite_take_then_explicit_grace":
            "rcu_free_take(r, &list);" in text and
            "synchronize_rcu_expedited();" in text,
        "rcu_drain_iterates_possible_cpu":
            "for_each_possible_cpu(cpu)" in text,
        "sync_uses_rcu_specialized_wait":
            "flush_rcu_work(&r->rwork);" in text,
        "all_page_frees_decrement_zone_counter":
            "mod_zone_page_state(folio_zone(folio), NR_FREE_RCU_PAGES" in text,
        "rcu_callback_waits_grace_before_workqueue":
            "call_rcu_hurry(&rwork->rcu, rcu_work_rcufn);" in WQ and
            "__queue_work(WORK_CPU_UNBOUND, rwork->wq, &rwork->work)" in WQ,
    }
    assert len(keys) == 13
    return keys

def bcount(x):
    return x.bit_count()

def change_core(s, cpu, updated):
    c = list(s.cores); c[cpu] = updated
    return replace(s, cores=tuple(c))

def ownership(s):
    fields = [s.freed]
    for cpu in s.cores:
        fields.extend([cpu.pending, cpu.batch])
        if cpu.expedited >= 0:
            fields.append(cpu.expedited)
    if s.drain >= 0:
        fields.append(s.drain)
    out = 0
    for x in fields:
        assert (out & x) == 0, f"Folio double owned 0x{out:x} & 0x{x:x}"
        out |= x
    assert out == s.issued, f"Lost folio: {out:x} != issued {s.issued:x}"
    # nr_pending and nr_batch in actual source must each match mask sizes.
    return {"accounted_not_yet_freed": bcount(s.issued) - bcount(s.freed),
            "all_pages_accounted": out == s.issued}

def successors(s, *, violate_worker_gp=False, violate_expedite_gp=False,
               violate_drain_gp=False):
    for cpu in range(2):
        c = s.cores[cpu]
        for id in CPU_IDS[cpu]:
            bit = 1 << id
            if not s.issued & bit:
                new = replace(c, pending=c.pending | bit)
                if new.rcuw == 0:
                    new = replace(new, batch=new.pending, pending=0, rcuw=1)
                yield f"cpu{cpu}_defer_page{id}", replace(change_core(s,cpu,new),issued=s.issued|bit), True
        if c.rcuw == 1:
            new = replace(c, rcuw=2)
            yield f"cpu{cpu}_rcu_gp_complete",change_core(s,cpu,new),True
        if c.rcuw == 2 or (c.rcuw == 1 and violate_worker_gp):
            # Worker owns the batch once it enters the locked section.
            # If work was not preceded by GP, this is an invalid free.
            safe = c.rcuw == 2 or c.batch == 0
            nextc = replace(c, batch=0, rcuw=0)
            if nextc.pending:
                nextc=replace(nextc,batch=nextc.pending,pending=0,rcuw=1)
            n=change_core(s,cpu,nextc)
            yield f"cpu{cpu}_rcu_workfn",replace(n,freed=n.freed|c.batch),safe
        if c.expedited == -1 and (c.pending or c.batch):
            detached = c.pending | c.batch
            # The in-flight callback can eventually run with an empty batch.
            nc=replace(c,pending=0,batch=0,expedited=detached)
            yield f"cpu{cpu}_expedite_take",change_core(s,cpu,nc),True
        if c.expedited >= 0:
            n=change_core(s,cpu,replace(c,expedited=-1))
            yield f"cpu{cpu}_expedite_gp_and_free",replace(n,freed=n.freed|c.expedited),(not violate_expedite_gp or c.expedited == 0)
    if s.drain == -1:
        took=0
        n=s
        for cpu in range(2):
            c=n.cores[cpu]
            took |= c.pending | c.batch
            n=change_core(n,cpu,replace(c,pending=0,batch=0))
        yield "rcu_free_drain_take",replace(n,drain=took),True
    if s.drain >= 0:
        # Simulate synchronize_rcu_expedited() inside rcu_free_flush().
        yield "rcu_free_drain_gp_and_free",replace(s,freed=s.freed|s.drain,drain=-2),(not violate_drain_gp or s.drain == 0)

def explore(**mutants):
    initial=S()
    queue=deque([initial])
    seen={initial}
    steps=0
    finish=0
    witness_bad=None
    max_live=0
    parents = {initial: None}
    while queue:
        s=queue.popleft()
        props=ownership(s)
        steps+=1
        max_live=max(max_live,props["accounted_not_yet_freed"])
        # Fully drained pages, and callbacks may still be pending with
        # empty lists in this simplified model. We do not force those
        # callbacks to terminate in this acceptance check.
        if s.issued == SCHEDULED and s.freed == SCHEDULED:
            finish+=1
        for label,n,safe in successors(s,**mutants):
            ownership(n)
            if not safe:
                witness_bad=label
                trace=[label]
                cursor=s
                while parents[cursor] is not None:
                    previous, event = parents[cursor]
                    trace.append(event)
                    cursor=previous
                trace.reverse()
                return {"visited_states":steps,"unsafe_transition":witness_bad,
                        "concrete_schedule":trace,
                        "unprotected_folio_mask":
                            (s.cores[int(label[3])].batch if "rcu_workfn" in label else
                             s.cores[int(label[3])].expedited if "expedite_gp" in label else s.drain)}
            if n not in seen:
                seen.add(n)
                parents[n]=(s,label)
                queue.append(n)
    assert finish>0
    return {"visited_states":steps,"successful_terminal_states":finish,
            "max_simultaneous_unfreed_folios":max_live,
            "duplicate_or_lost_ownership":False,
            "unsafe_transition":witness_bad}

def main():
    checks=has_source_gate(SRC)
    assert all(checks.values()),[k for k,v in checks.items() if not v]
    mutants={
        "omit_worker_gp":dict(violate_worker_gp=True),
        "omit_expedite_gp":dict(violate_expedite_gp=True),
        "omit_drain_gp":dict(violate_drain_gp=True),
    }
    baseline=explore()
    rejected={}
    for name,args in mutants.items():
        outcome=explore(**args)
        assert outcome["unsafe_transition"],(name,outcome)
        assert outcome["unprotected_folio_mask"] != 0, (name,outcome)
        rejected[name]=outcome
    source_mutants={
        "remove_work_completion_wait":("flush_rcu_work(&r->rwork);", ""),
        "remove_worker_new_batch":("r->queued = false;\n\trcu_free_start(r);", ""),
        "remove_page_accounting":("mod_zone_page_state(folio_zone(folio), NR_FREE_RCU_PAGES", ""),
        "remove_queue_gp":("queue_rcu_work(rcu_free_wq, &r->rwork)", ""),
    }
    source_rejections={}
    for label,(before,after) in source_mutants.items():
        assert before in SRC,label
        gate=has_source_gate(SRC.replace(before,after,1))
        bad=[k for k,v in gate.items() if not v]
        assert bad,(label,gate)
        source_rejections[label]=bad
    out={
        "stage":"G677","type":"bounded SC ownership/GP queue model, two CPU",
        "max_folios_in_model":4,"source_checks_pass":len(checks),
        "source_checks":checks,"source_mutants_rejected":source_rejections,
        "baseline_exhaustive_result":baseline,"mutated_gp_models_detect_unsafe_free":rejected,
        "important_limits":[
            "Abstract SC state machine models spinlock-protected queue ownership only.",
            "This is NOT the ARMv7 LKMM and NOT a simulation of genuine RCU callback execution timing.",
            "No physical kernel UAF, deadlock or speedup was demonstrated.",
            "A 4-folio model cannot prove absence of overflow, OOM or starvation for large workloads.",
            "Concurrent new folios may enter after a drain snapshot, so drain is NOT an unconditional global quiescence barrier.",
            "S2/S3 stay BLOCKED, no-ref RCU remains DEFAULT OFF, staged changes are NOT flashed."
        ]
    }
    OUTPUT.write_text(json.dumps(out,indent=2)+"\n")
    print("G677 2-CPU ownership state BFS:",baseline,
          "source checks",len(checks),"source mutants",len(source_rejections),
          "GP safety mutations",len(rejected))

if __name__=="__main__":
    main()
