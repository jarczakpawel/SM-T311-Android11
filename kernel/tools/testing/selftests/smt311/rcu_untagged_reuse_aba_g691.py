#!/usr/bin/env python3
"""G691: source-anchored conceptual model of unmarked-folio XArray ABA.

The reader obtains a raw struct folio pointer from xas_load, then checks
PG_rcu_free, rereads xarray and mapping.  If an old UNMARKED folio may
be recycled without grace period, reuse of the SAME page struct at the
SAME inode/index followed by marking may satisfy all these checks.

This is an ABSTRACT interleaving witness, not LKMM proof or actual UAF.
Do NOT use it to justify enabling optional SM-T311 ref-free RCU ON.
"""
from pathlib import Path
from collections import deque
import json
K=Path(__file__).resolve().parents[4]
OUT=K/"Documentation/mm/sm-t311-rcu-untagged-aba-g691.json"
FILES=("mm/filemap.c","mm/page_alloc.c","include/linux/page-flags.h","mm/folio.c")
SRC={p:(K/p).read_text() for p in FILES}
READ_EVENTS=("R_LOAD","R_FLAG","R_SIZE","R_RELOAD","R_MAP","R_COPY","R_UNLOCK")
WRITE_EVENTS=("W_REMOVE","W_LAST_PUT","W_REUSE","W_INSERT","W_MARK")

def between(s,start,end):
    assert start in s,start
    suffix=s.split(start,1)[1]
    assert end in suffix,end
    return suffix.split(end,1)[0]

def ordered(s,*needles):
    ix=[s.find(n) for n in needles]
    return min(ix)>=0 and ix==sorted(ix) and len(ix)==len(set(ix))

def source_assertions(f):
    filemap,allocator,flags,folio=(f[x] for x in FILES)
    lookup=between(filemap,"static struct folio *filemap_read_rcu_lookup(",
                   "static size_t filemap_read_rcu_size(")
    mark=between(filemap,"static noinline void filemap_mark_hot(",
                 "void filemap_read_accessed(")
    single=between(filemap,"static void page_cache_delete(","static void filemap_unaccount_folio(")
    many=between(filemap,"static void page_cache_delete_batch(",
                 "void delete_from_page_cache_batch(")
    free=between(allocator,"static __always_inline bool __free_pages_prepare(",
                 "trace_mm_page_free(page, order);")
    defer=between(allocator,"static void rcu_free_defer(", "/*\n * Free what every CPU")
    worker=between(allocator,"static void rcu_free_workfn(",
                   "static void rcu_free_expedite_workfn(")
    batch=between(filemap,"static bool filemap_read_rcu_batch(",
                  "size_t filemap_read_rcu(")
    rcu=between(filemap,"size_t filemap_read_rcu(", "ssize_t filemap_read(")
    return {
      "RCU_read_section_brackets_raw_lookup":
          ordered(batch,"rcu_read_lock();","filemap_read_rcu_lookup(",
                  "pagefault_enable();") and
          batch.rfind("rcu_read_unlock();")>batch.find("pagefault_enable();"),
      "raw_XArray_then_later_flag_test":
          ordered(lookup,"folio = xas_load(xas);",
                  "READ_ONCE(folio->flags.f) & __PG_RCU_FREE",
                  "xas_reload(xas) != folio"),
      "reader_rechecks_slot_then_mapping":
          ordered(lookup,"xas_reload(xas) != folio",
                  "READ_ONCE(folio->mapping) != mapping"),
      "reader_has_no_generation_or_reference_acquire":
          "folio_try_get" not in lookup and "generation" not in lookup,
      "reader_checks_inode_size_before_reload":
          ordered(lookup,"i_size_read(inode)","xas_reload(xas) != folio"),
      "reader_limits_normal_order_zero":
          "folio_test_large(folio)" in lookup and
          "folio_zonenum(folio) != ZONE_NORMAL" in lookup,
      "mark_only_when_opt_in_and_marked_hot":
          ordered(mark,"if (!lockless_read_enabled())",
                  "folio_test_set_rcu_free(folio)") and
          "EXT4_SUPER_MAGIC" in mark,
      "mark_sets_mapping_eligible_only_if_still_mapped":
          "READ_ONCE(folio->mapping) == mapping" in mark and
          "mapping_set_rcu_read(mapping)" in mark,
      "single_remove_drops_xarray_slot_then_mapping":
          ordered(single,"xas_store(&xas, shadow);","folio->mapping = NULL;"),
      "single_and_batch_preserve_tag_during_inode_exit_on_T311":
          "!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT)" in single and
          "!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT)" in many,
      "last_put_checks_tag_before_buddy":
          ordered(free,"folio_test_rcu_free(folio)",
                  "__folio_clear_rcu_free(folio);",
                  "rcu_free_defer(folio);","return false;"),
      "unmarked_free_has_ordinary_allocator_path":
          "if (unlikely(folio_test_rcu_free(folio))" in free and
          "return false;" in free,
      "defer_queues_folio_to_pending_before_start":
          ordered(defer,"list_add_tail(&folio->lru, &r->pending);",
                  "r->nr_pending +=","rcu_free_start(r);"),
      "rcu_work_only_frees_after_callback":
          "rcu_free_pages(&done);" in worker and
          "queue_rcu_work(rcu_free_wq, &r->rwork)" in allocator,
      "RCU_disabled_by_default_on_SM_T311":
          "static DEFINE_STATIC_KEY_FALSE(lockless_read_key);" in filemap,
      "no_global_mapping_generation_marker":
          "generation" not in lookup and
          "generation" not in rcu,
      "user_backed_iter_g689_guard_preserved":
          "if (!user_backed_iter(iter))" in rcu,
      "safety_flag_pg_rcu_free_exists":
          "#define __PG_RCU_FREE (1UL << PG_rcu_free)" in flags,
      "last_ref_calls_normal_page_free":
          "free_unref_folios(" in folio and
          "folio_put" in folio
    }

# State: (reader step, writer step, raw P captured, last sampled PG flag,
#         aborted, accepted, copied_new). Object address is always the
# same physical struct page pointer P if/when recycled, intentionally.
# Under tagged-old case the free is deferred until reader UNLOCK.
def schedules(old_tagged, *, require_generation=False):
    initial=(0,0,False,None,False,False,False)
    queue=deque([(initial,[])])
    reached={initial}
    accepted_old=accepted_new=0
    witness=None
    ended=0
    while queue:
        (r,w,p,seen_tag,abort,accept,new_copy),hist=queue.popleft()
        if r==len(READ_EVENTS) and w==len(WRITE_EVENTS):
            ended+=1
        if accept and new_copy:
            accepted_new+=1
            if witness is None:
                witness=hist
        if accept and not new_copy:
            accepted_old+=1
        next_steps=[]
        # W_REMOVE removes same pointer from XA. W_LAST_PUT clears
        # tag+defers if tagged, otherwise returns page to buddy.
        # W_REUSE picks same physical address P; tagged-old cannot be
        # reused until reader's grace period finishes.
        if w<len(WRITE_EVENTS):
            event=WRITE_EVENTS[w]
            permitted=not(event=="W_REUSE" and old_tagged and r<7)
            if permitted:
                next_steps.append(((r,w+1,p,seen_tag,abort,accept,new_copy),
                                   event))
        if r<len(READ_EVENTS):
            event=READ_EVENTS[r]
            ok=True
            if event=="R_LOAD":
                ok=w==0
                newp=ok
                sampled=seen_tag
                ab=abort
                ac=accept
                nc=new_copy
            elif event=="R_FLAG":
                # Old tag clears at the last put, whereas new occupant
                # gains its PG flag only after W_MARK.
                observed_tag= ((old_tagged and w<2) or w>=5)
                sampled=observed_tag
                ab=abort or not observed_tag
                newp=p
                ac=accept
                nc=new_copy
            elif event=="R_SIZE":
                newp=p;sampled=seen_tag;ab=abort;ac=accept;nc=new_copy
            elif event=="R_RELOAD":
                # The slot stores P before W_REMOVE and after
                # W_INSERT, but not between; pointer equality alone
                # cannot distinguish the recycled incarnation.
                slot_has_p=w==0 or w>=4
                generation_ok=not require_generation or w<3
                ab=abort or not slot_has_p or not generation_ok
                newp=p;sampled=seen_tag;ac=accept;nc=new_copy
            elif event=="R_MAP":
                # Adversarial but legal: recycled folio gets mapped
                # at SAME inode and SAME index as the old one.
                ab=abort
                newp=p;sampled=seen_tag;ac=accept;nc=new_copy
            elif event=="R_COPY":
                newp=p;sampled=seen_tag;ab=abort
                ac=not abort and bool(seen_tag)
                nc=ac and w>=3
            else:
                newp=p;sampled=seen_tag;ab=abort;ac=accept;nc=new_copy
            if event!="R_LOAD" or ok:
                next_steps.append(((r+1,w,newp,sampled,ab,ac,nc),event))
        for state,event in next_steps:
            if state not in reached:
                reached.add(state)
                queue.append((state,hist+[event]))
    return {"states":len(reached),"terminal_sequences":ended,
            "accepted_states_old":accepted_old,
            "accepted_states_with_recycled_same_PFN":accepted_new,
            "ABA_witness":witness}

def mutants(sources):
    cases={
       "remove_raw_slot_reload":("mm/filemap.c",
           "if (xas_reload(xas) != folio)", "if (false)"),
       "remove_flag_test":("mm/filemap.c",
           "READ_ONCE(folio->flags.f) & __PG_RCU_FREE",
           "READ_ONCE(folio->flags.f) & 0"),
       "remove_mapping_identity":("mm/filemap.c",
           "READ_ONCE(folio->mapping) != mapping", "false"),
       "replace_deferred_free_by_direct":("mm/page_alloc.c",
           "rcu_free_defer(folio);", "free_one_page(zone, page, pfn, order, fpi_flags);"),
       "remove_last_put_tag_clear":("mm/page_alloc.c",
           "__folio_clear_rcu_free(folio);", "/* flag left uncleared */"),
       "drop_inode_exit_tag_preservation":("mm/filemap.c",
           "if (!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&",
           "if ("),
       "drop_pending_rcu_work_enqueue":("mm/page_alloc.c",
           "rcu_free_start(r);", "/* no RCU enqueue */"),
       "remove_reader_RCU_lock":("mm/filemap.c",
           "rcu_read_lock();\n#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT",
           "#ifdef CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT"),
       "drop_user_iterator_whitelist":("mm/filemap.c",
           "if (!user_backed_iter(iter))", "if (false)"),
    }
    caught={}
    for key,(source,before,after) in cases.items():
        assert before in sources[source],key
        changed=dict(sources)
        if key in ("remove_mapping_identity", "drop_pending_rcu_work_enqueue"):
            # This expression also appears in hot-marker code; mutate the
            # LATER instance in the lookup being attested, not the marker.
            head,sep,tail=changed[source].rpartition(before)
            assert sep,key
            changed[source]=head+after+tail
        else:
            changed[source]=changed[source].replace(before,after,1)
        failed=[k for k,v in source_assertions(changed).items() if not v]
        assert failed,(key,"modified source escaped")
        caught[key]=failed
    return caught

def main():
    src=source_assertions(SRC)
    assert len(src)==19,len(src)
    assert all(src.values()),[k for k,v in src.items() if not v]
    caught=mutants(SRC)
    assert len(caught)==9
    old_untagged=schedules(False)
    old_tagged=schedules(True)
    hypothetical_generation=schedules(False,require_generation=True)
    assert old_untagged["ABA_witness"],old_untagged
    assert not old_tagged["ABA_witness"],old_tagged
    assert not hypothetical_generation["ABA_witness"],hypothetical_generation
    assert old_untagged["accepted_states_with_recycled_same_PFN"]>0
    # G659 ALREADY diagnosed old/unmarked folio ABA with FOUR actual
    # LKMM herd7 runs. G691 extends its provenance with final-put
    # and same-inode/index allocator-incarnation steps; this is not a
    # newly discovered vulnerability and not a complete LKMM proof.
    earlier=json.loads((K/"Documentation/mm/sm-t311-rcu-aba-g659.json").read_text())
    g659={
        k:v["observation"] for k,v in earlier["models"].items()
        if k in ("SMT311+XALockUnmarkedABA", "SMT311+XALockGraceABA",
                 "SMT311+XALockOldNewGenNoGrace",
                 "SMT311+XALockOldNewGenGrace")
    }
    assert len(g659)==4 and all(x=="Sometimes" for x in g659.values())
    report={
      "stage":"G691",
      "earlier_G659_four_LKMM_diagnostics_NOT_new_discovery":g659,
      "incremental_value_vs_G659":"G691 adds source-anchored explicit old-untagged -> buddy last put -> same physical P reallocation -> same mapping/index new tag before stale reader checks; G659 had synthetic LKMM pointer/generation observations.",
      "result":"UNMARKED-to-marked physical PFN reuse is an abstract ABA witness, not a proven live kernel corruption",
      "source_guards":src,
      "source_guards_passed":len(src),
      "actual_modified_source_inputs_rejected":caught,
      "interleaving":{
         "initial_xarray_folio_UNTAGGED_can_free_and_reuse":old_untagged,
         "initial_xarray_folio_TAGGED_waits_grace_before_reuse":old_tagged,
         "hypothetical_atomic_generation_identity_comparison":hypothetical_generation
      },
      "witness_assumptions":[
         "Two generations of a physical struct page pointer P appear in the SAME mapping inode/index; XArray has no unique generation token checked by reader.",
         "First generation is untagged, so final put may return it to buddy before old RCU reader has finished processing its raw xas_load pointer.",
         "Second generation is inserted and becomes hot/PG_rcu_free marked before the reader tests the raw pointer's flag.",
         "The reader's xas_reload compares pointer P with pointer P and the mapping field also matches. This allows an abstract ABA acceptance.",
         "Folio content validity, i_size, pagefault and cache behavior are simplified; no real UAF or corruption reproduced on SM-T311.",
         "The no-witness tagged-old result applies ONLY to the G691 simplified two-task model; G659 synthetic grace LKMM variants had Sometimes outcomes and are not resolved or disproved by this test.",
      ],
      "blocking_decision":"S2 ARMv7 PFN/ABA BLOCKED. The existing guards alone cannot be claimed to universally rule out this scenario.",
      "safe_alternatives_to_research":[
         "Generation identity coupled atomically to XArray slot and validated before/after dereference (requires rigorous design).",
         "RCU grace delayed reuse for ALL candidate ext4 pagecache folios (potentially prohibitive Normal memory overhead).",
         "Reference-acquiring fallback before unpinned dereference, or ship normal RCU-OFF kernel only.",
      ],
      "no_changes_to_executable_sources":True,
      "physical":"G650, unchanged",
      "staged_runtime_sources_NOT_INSTALLED":["mm/filemap.c","mm/page_alloc.c","mm/shmem.c","mm/vma.c"],
    }
    OUT.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    print(f"G691 {len(src)}/19 actual source checks PASS; {len(caught)}/9 C mutations rejected")
    for name,d in report["interleaving"].items():
        print(name,d)

if __name__=="__main__":
    main()
