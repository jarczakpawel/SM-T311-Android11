#!/usr/bin/env python3
"""SM-T311 G658: fail-closed SOURCE gate for tagged-folio deferred-free routes.

This checks that two actual Linux deferred-free paths still wait for an RCU
grace period before touching/recycling folio memory. This is NOT a memory
model proof, callgraph completeness proof or permission to enable no-ref ON.
"""
import json
from pathlib import Path

K=Path(__file__).resolve().parents[4]
SOURCE=K/"mm/page_alloc.c"
OUT=K/"Documentation/mm/sm-t311-rcu-grace-source-g658.json"

def body(source, name):
    mark=("static void "+name+"(") if name != "rcu_free_flush" else "static unsigned long rcu_free_flush("
    if name == "rcu_free_sync":mark="void rcu_free_sync("
    if name == "__free_pages_prepare":mark="static __always_inline bool __free_pages_prepare("
    if name == "rcu_free_pages":mark="static unsigned long rcu_free_pages("
    idx=source.index(mark)
    start=source.index("{",idx)
    level=0
    for i in range(start,len(source)):
        if source[i]=="{": level+=1
        elif source[i]=="}":
            level-=1
            if not level:return source[start:i+1]
    raise ValueError("Unbalanced source function: "+name)

def structural_gate(source):
    start=body(source,"rcu_free_start")
    work=body(source,"rcu_free_workfn")
    flush=body(source,"rcu_free_flush")
    expedited=body(source,"rcu_free_expedite_workfn")
    drain=body(source,"rcu_free_drain")
    sync=body(source,"rcu_free_sync")
    pending=body(source,"rcu_free_defer")
    actual_free=body(source,"__free_pages_prepare")
    done=body(source,"rcu_free_pages")
    checks={
      "ref_free_mark_gate": "folio_test_rcu_free(folio)" in actual_free,
      "flag_cleared_before_queuing": actual_free.index("__folio_clear_rcu_free") < actual_free.index("rcu_free_defer(folio)"),
      "tagged_free_does_not_recycle_immediately": actual_free.index("rcu_free_defer(folio)") < actual_free.index("return false;"),
      "marked_folio_pending_queue": "list_add_tail(&folio->lru, &r->pending)" in pending,
      "pending_starts_grace_work": "rcu_free_start(r)" in pending,
      "only_one_batch_in_flight": "if (r->queued || list_empty(&r->pending))" in start,
      "batch_initialized_before_rcu_work":0 <= start.find("list_splice_init(&r->pending, &r->batch)") < start.find("queue_rcu_work(rcu_free_wq, &r->rwork)"),
      "rcu_work_handler_post_grace": "to_rcu_work(work)" in work and "rcu_free_pages(&done)" in work,
      "expedited_grace_before_recycling":0 <= flush.find("synchronize_rcu_expedited();") < flush.find("rcu_free_pages(list)"),
      "expedite_path_uses_grace_flush": "rcu_free_flush(&list)" in expedited,
      "drain_path_uses_grace_flush": "rcu_free_flush(&list)" in drain,
      "sync_joins_deferred_rcu_work": "rcu_free_drain();" in sync and "flush_work(&r->rwork.work)" in sync and "flush_work(&r->expedite)" in sync,
      "last_recycler_called_after_grace": "free_unref_folios(&fbatch)" in done,
      "deferred_pages_zone_accounting": "NR_FREE_RCU_PAGES" in done and "NR_FREE_RCU_PAGES" in pending,
      "hwpoison_exception_explicit": "PageHWPoison(page) && !order" in actual_free,
    }
    return checks

def main():
    source=SOURCE.read_text()
    checks=structural_gate(source)
    assert len(checks)==15 and all(checks.values()),[key for key,val in checks.items() if not val]
    negative={
      "removed_queue_rcu_work_must_fail":not all(structural_gate(source.replace("queue_rcu_work(rcu_free_wq, &r->rwork)","queue_work(rcu_free_wq, &r->rwork)",1)).values()),
      "removed_expedited_grace_must_fail":not all(structural_gate(source.replace("synchronize_rcu_expedited();","/* removed */",1)).values()),
      "removed_mark_gate_must_fail":not all(structural_gate(source.replace("folio_test_rcu_free(folio)","false",1)).values()),
    }
    assert all(negative.values()),negative
    result={
        "device":"SM-T311 ARMv7",
        "source_runtime_base":"a2fca4558",
        "passes":len(checks),
        "checks":checks,
        "negative_controls":negative,
        "rcu_gp_obligation":"All currently identified last-free work and expedite paths retain an RCU GP before recycling tagged folio.",
        "limitations":[
            "Source-string check only, not a proof that every freeing caller is covered.",
            "Assumes kernel queue_rcu_work semantics, matching actual kernel RCU behavior; needs callgraph review.",
            "Does not prove untagged-to-tagged speculative pointer ABA absent.",
            "Does not prove ARMv7 HighMem aliasing, RCU ON safety, application performance or energy."
        ],
        "s2_gate":"BLOCKED"
    }
    OUT.write_text(json.dumps(result,indent=2,ensure_ascii=False)+"\n")
    print("G658 SOURCE deferred-RCU-GP GATE PASS",len(checks),"checks",len(negative),"negative controls; S2 BLOCKED")
if __name__=="__main__":main()
