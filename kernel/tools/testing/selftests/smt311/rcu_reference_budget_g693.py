#!/usr/bin/env python3
"""G693 source-grounded speculative XArray folio reference and Normal budget gate.

Explore an intentionally BOUNDED single-page, two-thread SC interleaving.
A writer can recycle the same physical struct page after removing an
UNTAGGED original from XArray. Compare a reader which merely rechecks P
to a reader which performs folio_try_get and xas_reload under RCU.

An accepted pointer to a NEW incarnation is not inherently wrong when
a valid reference to that incarnation is held. Distinguish protected
copies from unpinned reuse; never call this a full LKMM/lifetime proof.
"""
from pathlib import Path
from collections import deque
import json
import re

K=Path(__file__).resolve().parents[4]
SRC_FILES=("mm/filemap.c","mm/page_alloc.c","include/linux/xarray.h",
           "mm/internal.h","include/linux/page_ref.h")
SOURCE={path:(K/path).read_text() for path in SRC_FILES}
SNAP_PATH=K/"Documentation/mm/sm-t311-rcu-zone-snapshot-g693.json"
OUT=K/"Documentation/mm/sm-t311-rcu-ref-protocol-g693.json"

def segment(source,start,end):
    assert start in source,(start,"missing start")
    rest=source.split(start,1)[1]
    assert end in rest,(end,"missing end")
    return rest.split(end,1)[0]

def ordered(text,*markers):
    points=[text.find(m) for m in markers]
    return all(p>=0 for p in points) and points==sorted(points) and len(set(points))==len(points)

def checks(src):
    fm,alloc,xa,internal,ref=(src[p] for p in SRC_FILES)
    get=segment(fm,"void *filemap_get_entry(", "/**\n * __filemap_get_folio_mpol")
    loose=segment(fm,"static struct folio *filemap_read_rcu_lookup(",
                  "static size_t filemap_read_rcu_size(")
    free=segment(alloc,"static __always_inline bool __free_pages_prepare(",
                 "trace_mm_page_free(page, order);")
    xa_reload=segment(xa,"static inline void *xas_reload(", "/**\n * xas_set()")
    marked=segment(fm,"static noinline void filemap_mark_hot(",
                   "void filemap_read_accessed(")
    return {
      "reference_path_xas_load_try_get_then_recheck":
         ordered(get,"folio = xas_load(&xas);",
                 "if (!folio_try_get(folio))",
                 "if (unlikely(folio != xas_reload(&xas)))",
                 "folio_put(folio);"),
      "ref_acquire_must_fail_on_zero":
         "if (!folio_try_get(folio))\n\t\tgoto repeat;" in get and
         "atomic_add_unless(&page->_refcount, nr, 0)" in ref,
      "slot_mismatch_drops_ref_before_retry":
         "folio_put(folio);\n\t\tgoto repeat;" in get,
      "reference_path_holds_RCU_during_lookup":
         ordered(get,"rcu_read_lock();","folio = xas_load(&xas);",
                 "folio_try_get(folio)","rcu_read_unlock();"),
      "xarray_reload_has_no_generation_id":
         "return xa_entry(xas->xa, node, offset);" in xa_reload and
         "generation" not in xa_reload,
      "ref_free_path_checks_unpinned_flag_late":
         ordered(loose,"folio = xas_load(xas);",
                 "READ_ONCE(folio->flags.f) & __PG_RCU_FREE",
                 "xas_reload(xas) != folio") and
         "folio_try_get(" not in loose,
      "source_acknowledges_recycled_page":
         "its memory reused as another folio before the" in fm,
      "normal_folio_only_is_not_a_generation":
         "folio_zonenum(folio) != ZONE_NORMAL" in loose,
      "old_untagged_not_rcu_delayed_by_allocator":
         "unlikely(folio_test_rcu_free(folio))" in free and
         "rcu_free_defer(folio);" in free,
      "not_reclaim_other_zone":
         "SM_T311_RCU_FREE_RESERVE" in internal and
         "SM_T311_RCU_PENDING_SOFT_LIMIT" in internal,
      "reserve_4MiB_constant":
         "#define SM_T311_RCU_FREE_RESERVE\t(SZ_4M >> PAGE_SHIFT)" in internal,
      "queue_soft_limit_2MiB_constant":
         "#define SM_T311_RCU_PENDING_SOFT_LIMIT\t(SZ_2M >> PAGE_SHIFT)" in internal,
      "marking_requires_explicit_key":
         "if (!lockless_read_enabled())" in marked,
      "G692_ON_key_still_fail_closed":
         "if (val)\n\t\treturn -EOPNOTSUPP;" in segment(
              fm,"static int lockless_read_set(","DEFINE_DEBUGFS_ATTRIBUTE("),
      "direct_slowpath_reads_are_already_referenced":
         "filemap_get_pages(iocb, iter->count, &fbatch, false)" in fm,
      "initial_key_is_false":
         "static DEFINE_STATIC_KEY_FALSE(lockless_read_key);" in fm,
    }

# R = LOAD, TRY_GET, RELOAD, COPY, PUT
# W = REMOVE, DROP_PAGECACHE_REF, REUSE_PHYSICAL_P, INSERT_NEW, MARK_NEW
# The slot in both incarnations is the same pointer P, so a raw-pointer
# equality test cannot distinguish A from B; the *model* keeps generation
# A/B only for assessing whether the speculative reader has a valid pin.
R=("LOAD","TRY_GET","RELOAD","COPY","PUT")
W=("REMOVE","DROP","REUSE","INSERT","MARK")

def walk(*, acquire_ref):
    # state: (ri,wi,refA,refB,held,accepted,bad)
    first=(0,0,1,0,"",False,False)
    q=deque([(first,[])])
    seen={first}
    witness=None
    complete=0
    accepted=0
    benign_reused=0
    bad=0
    while q:
        (r,w,a,b,held,ok,unsafe),history=q.popleft()
        if r==len(R) and w==len(W):
            complete+=1
        if unsafe:
            bad+=1
            if witness is None:
                witness=history
        if ok:
            accepted+=1
            if held=="B":
                benign_reused+=1
        nexts=[]
        if w<len(W):
            act=W[w]
            can=True;na,nb=a,b
            if act=="REMOVE":
                pass
            elif act=="DROP":
                if a<1:can=False
                else:na-=1
            elif act=="REUSE":
                can=a==0
                if can:nb=1
            elif act=="INSERT":
                can=b>0
            if can:
                nexts.append(((r,w+1,na,nb,held,ok,unsafe),"W_"+act))
        if r<len(R):
            act=R[r]
            can=True;na,nb=a,b;nh=held;no=ok;nu=unsafe
            if act=="LOAD":
                # Force reader to sample P while it belongs to A.
                can=w==0
            elif act=="TRY_GET":
                if acquire_ref:
                    # atomically add unless zero to the incarnation
                    # currently occupying struct page P. If it has
                    # already been recycled, it is B, not stale A.
                    if w>=3 and b>0: nb+=1;nh="B"
                    elif w<3 and a>0: na+=1;nh="A"
                    else:nh="failed"
                else:
                    nh=""
            elif act=="RELOAD":
                # XArray slot holds pointer P before REMOVE or after
                # INSERT. Rechecking P cannot distinguish A and B.
                slot_p=(w==0 or w>=4)
                if acquire_ref:
                    no=(slot_p and nh in ("A","B"))
                else:
                    # G691 unpinned lookup requires a late PG_rcu_free
                    # check, here tagged only on B after W_MARK.
                    no=(slot_p and w>=5)
            elif act=="COPY":
                if no:
                    # The read can be safe when A is still pinned, or
                    # when a valid B reference was acquired and the
                    # reloaded slot points to new B.
                    generation="B" if w>=3 else "A"
                    nu=(nh!=generation) if acquire_ref else (w>=3)
            elif act=="PUT":
                if acquire_ref and nh=="A":
                    if a<1:can=False
                    else:na-=1
                if acquire_ref and nh=="B":
                    if b<1:can=False
                    else:nb-=1
            if can:
                nexts.append(((r+1,w,na,nb,nh,no,nu),"R_"+act))
        for state,event in nexts:
            if state not in seen:
                seen.add(state)
                q.append((state,history+[event]))
    return {"reachable_states":len(seen),
            "terminal_states":complete,"accepted_states":accepted,
            "benign_new_generation_pinned_states":benign_reused,
            "invalid_recycled_unpinned_states":bad,
            "invalid_recycled_witness":witness}

def source_mutants(orig):
    changes={
       "remove_try_get":("mm/filemap.c","if (!folio_try_get(folio))\n\t\tgoto repeat;",
                          "if (false)\n\t\tgoto repeat;"),
       "remove_xarray_ref_reload":("mm/filemap.c",
              "if (unlikely(folio != xas_reload(&xas))) {","if (false) {"),
       "remove_failed_reload_put":("mm/filemap.c",
              "folio_put(folio);\n\t\tgoto repeat;","goto repeat;"),
       "remove_source_RCU":("mm/filemap.c",
              "rcu_read_lock();\nrepeat:","repeat:"),
       "remove_allocator_tag_defer":("mm/page_alloc.c",
              "rcu_free_defer(folio);","/* no grace */"),
       "erase_normal_reserve":("mm/internal.h",
              "#define SM_T311_RCU_FREE_RESERVE\t(SZ_4M >> PAGE_SHIFT)",
              "#define SM_T311_RCU_FREE_RESERVE\t0"),
       "erase_pending_soft_limit":("mm/internal.h",
              "#define SM_T311_RCU_PENDING_SOFT_LIMIT\t(SZ_2M >> PAGE_SHIFT)",
              "#define SM_T311_RCU_PENDING_SOFT_LIMIT\t0"),
       "remove_experiment_failclosed":("mm/filemap.c",
              "if (val)\n\t\treturn -EOPNOTSUPP;",
              "if (val)\n\t\treturn 0;"),
       "remove_key_guard":("mm/filemap.c","if (!lockless_read_enabled())",
              "if (false)"),
       "remove_unpinned_flag_check":("mm/filemap.c",
              "READ_ONCE(folio->flags.f) & __PG_RCU_FREE",
              "READ_ONCE(folio->flags.f) & 0"),
    }
    caught={}
    for name,(path,old,repl) in changes.items():
        assert old in orig[path],name
        copy=dict(orig)
        copy[path]=copy[path].replace(old,repl,1)
        false=[k for k,v in checks(copy).items() if not v]
        assert false,(name,"escaped actual source guard")
        caught[name]=false
    return caught

def memory_scenarios(snap):
    normal=snap["normal"];high=snap["highmem"]
    page=snap["page_bytes"]
    free=normal["free_pages"];managed=normal["managed_pages"]
    high_watermark=normal["high_pages"]
    reserve=1024;softlimit=512
    hypothetical={}
    for fraction in (0.01,0.05,0.10,0.25):
        pages=round(managed*fraction)
        hypothetical[str(int(fraction*100))+"pct_managed_normal"]={
          "pages":pages,"MiB":round(pages*page/1048576,2),
          "share_of_current_normal_free_pct":round(100*pages/free,1),
          "larger_than_2MiB_queue_soft_limit":pages>softlimit,
          "normal_free_minus_hypothetical_wait_pages":free-pages,
          "would_be_under_high_plus_4MiB_reserve_now":
             free-pages <= high_watermark+reserve,
        }
    data={
       "normal_managed_pages":managed,
       "normal_managed_MiB":round(managed*page/1048576,2),
       "normal_free_pages":free,
       "normal_free_MiB":round(free*page/1048576,2),
       "normal_high_watermark_pages":high_watermark,
       "normal_4MiB_extra_reserve_pages":reserve,
       "normal_2MiB_pending_soft_limit_pages":softlimit,
       "normal_free_over_high_plus_reserve_pages":free-high_watermark-reserve,
       "highmem_free_pages":high["free_pages"],
       "highmem_high_watermark_pages":high["high_pages"],
       "highmem_below_high":high["free_pages"]<high["high_pages"],
       "illustrative_whole_normal_zone_deferred_free":hypothetical,
    }
    return data

def main():
    a=checks(SOURCE)
    assert len(a)==16 and all(a.values()),a
    mutants=source_mutants(SOURCE)
    assert len(mutants)==10
    no_pin=walk(acquire_ref=False)
    pin=walk(acquire_ref=True)
    assert no_pin["invalid_recycled_witness"],no_pin
    assert pin["invalid_recycled_witness"] is None,pin
    assert pin["accepted_states"]>0 and pin["terminal_states"]>0
    snap=json.loads(SNAP_PATH.read_text())
    assert snap["page_bytes"]==4096
    assert snap["physical_boot_stage"]=="G650"
    budget=memory_scenarios(snap)
    assert budget["highmem_below_high"]
    assert budget["normal_free_over_high_plus_reserve_pages"]>0
    report={
      "stage":"G693",
      "scope":"Source-anchored referenced XArray reader protocol and snapshot-based hypothetical Normal defer budget, no physical workload",
      "C_source_guards":a,"passed_source_guards":len(a),
      "actual_C_mutations_rejected":mutants,
      "abstract_2_task_SC":{
         "late_flag_unpinned_old_to_marked_new":no_pin,
         "folio_try_get_then_xas_reload_then_put":pin,
      },
      "snapshot_source":"Documentation/mm/sm-t311-rcu-zone-snapshot-g693.json",
      "measured_single_snapshot_and_hypothetical_budget":budget,
      "conclusions":[
         "A referenced lookup uses atomic nonzero ref acquisition followed by slot revalidation. Even if a recycled B is referenced, B is safely pinned during copy in the simplified model.",
         "The unpinned old->new PFN ABA witness remains; no object generation token is introduced.",
         "Do not add global RCU deferred free of all ext4 folios without Normal memory pressure and cache impact tests.",
         "The snapshot is one passive sample, not performance, memory stress or production validation.",
         "The G692 experiment ON fail-closed policy must remain intact.",
         "Neither finite state SC models nor these source checks prove global Linux kernel LKMM/ARMv7 correctness.",
      ],
      "kernel_source_changes":[],
      "deployed":False,
      "four_staged_runtime_paths":["mm/filemap.c","mm/page_alloc.c","mm/shmem.c","mm/vma.c"],
    }
    OUT.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    print("G693 source",len(a),"/16 PASS, C mutants",len(mutants),
          "/10 caught")
    print("G693 unpinned:",no_pin)
    print("G693 pinned:",pin)
    print("G693 physical snapshot/budget:",budget)

if __name__=="__main__":
    main()
