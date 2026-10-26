#!/usr/bin/env python3
"""G674 source-bound three-generation ABA interleaving diagnostic.

We model *one physical page address* containing three different generation
identities A, B, C over time. A and C belong to the reader's inode M,
while B belongs to another inode N which is evicted while reader M is
inside RCU. Mark B under a reference and permit N inode-exit cleanup to
clear PG_rcu_free; C is inserted unmarked then retired without RCU grace.

An SC diagnostic showing a possible source-order schedule is not a proof
that ARMv7 can execute it or a confirmed Linux UAF.
"""
from itertools import combinations
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[4]
PATHS = ("mm/filemap.c", "mm/page_alloc.c", "mm/truncate.c",
         "include/linux/page-flags.h", "include/linux/pagemap.h")
SRC = {p:(ROOT/p).read_text() for p in PATHS}
OUT = ROOT/"Documentation/mm/sm-t311-rcu-three-generation-aba-g674.json"

R = ("R_lock", "R_xas_A", "R_read_tag", "R_read_isize",
     "R_xas_reload", "R_check_mapping", "R_copy", "R_unlock")
W = ("W_remove_A", "W_free_A", "W_allocate_B_other_inode", "W_mark_B",
     "W_evict_inode_B", "W_clear_tag_and_remove_B", "W_free_B",
     "W_allocate_C_original_inode", "W_publish_C_same_slot",
     "W_remove_C", "W_free_C")

def source_gates(src):
    fm, pa, tr = (src[k] for k in ("mm/filemap.c","mm/page_alloc.c","mm/truncate.c"))
    fl, pm = (src[k] for k in ("include/linux/page-flags.h","include/linux/pagemap.h"))
    lookup = fm.split("static struct folio *filemap_read_rcu_lookup(",1)[1].split(
        "static size_t filemap_read_rcu_size(",1)[0]
    batch = fm.split("static bool filemap_read_rcu_batch(",1)[1].split(
        "size_t filemap_read_rcu(",1)[0]
    mark = fm.split("static noinline void filemap_mark_hot(",1)[1].split(
        "void filemap_read_accessed(",1)[0]
    delete = fm.split("static void page_cache_delete(",1)[1].split(
        "static void filemap_unaccount_folio(",1)[0]
    deletebatch = fm.split("static void page_cache_delete_batch(",1)[1].split(
        "void delete_from_page_cache_batch(",1)[0]
    free = pa.split("static __always_inline bool __free_pages_prepare(",1)[1].split(
        "trace_mm_page_free(page, order);",1)[0]
    return {
        "reader_holds_rcu_before_lookup":
            batch.find("rcu_read_lock();") >= 0 and
            batch.index("rcu_read_lock();") < batch.index("filemap_read_rcu_lookup("),
        "reader_tag_check_before_slot_recheck":
            "READ_ONCE(folio->flags.f) & __PG_RCU_FREE" in lookup and
            "xas_reload(xas) != folio" in lookup and
            lookup.index("READ_ONCE(folio->flags.f)") < lookup.index("xas_reload(xas) != folio"),
        "reader_same_pointer_recheck_not_generation":
            "if (xas_reload(xas) != folio)" in lookup,
        "reader_uses_original_mapping_check":
            "READ_ONCE(folio->mapping) != mapping" in lookup,
        "marker_requires_live_folio_reference":
            "folio_test_set_rcu_free(folio)" in mark and
            "READ_ONCE(folio->mapping) != mapping" in mark,
        "tagged_last_free_defers":
            "__folio_clear_rcu_free(folio);" in free and
            "rcu_free_defer(folio);" in free,
        "unmarked_last_free_ordinary_path":
            "if (unlikely(folio_test_rcu_free(folio))" in free and
            "trace_mm_page_free(page, order);" in pa,
        "inode_exit_sets_exiting_before_truncate":
            "mapping_set_exiting(mapping);" in tr and
            "truncate_inode_pages(mapping, 0);" in tr,
        "inode_exit_allows_tag_clear_single":
            "folio_test_rcu_free(folio) && mapping_exiting(mapping)" in delete and
            "folio_clear_rcu_free(folio);" in delete,
        "inode_exit_allows_tag_clear_batch":
            "folio_test_rcu_free(folio) && mapping_exiting(mapping)" in deletebatch and
            "folio_clear_rcu_free(folio);" in deletebatch,
        "reader_inode_specific_exception_reason":
            "when the folio's inode is evicted" in fl.lower(),
        "SMT311_single_inode_exit_preserves_tag_until_RCU":
            "!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&" in delete,
        "SMT311_batch_inode_exit_preserves_tag_until_RCU":
            "!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&" in deletebatch,
        "mapping_exiting_not_global_rcu_exclusion":
            "test_bit(AS_EXITING, &mapping->flags)" in pm,
    }

def schedules():
    # all SC interleavings preserving each individual thread's order
    total=len(R)+len(W)
    for r_positions in combinations(range(total),len(R)):
        indexes=set(r_positions)
        riter,witer=iter(R),iter(W)
        yield tuple(next(riter) if i in indexes else next(witer)
                    for i in range(total))

def simulate(order, protected_c=False, require_locked_tag_slot=False,
             b_inode_exit_avoids_early_clear=False):
    generation="A"
    mapping="M"
    slot="A"
    flagged=False
    exiting_b=False
    rcu=False
    tag_seen=False
    tag_generation=None
    accepted=False
    mapping_pass=False
    copied_after_free=False
    actual_free=False
    r_seen_gen=None
    pending_free=False
    witness={}
    for step in order:
        if step=="R_lock":
            rcu=True
        elif step=="R_xas_A":
            r_seen_gen=generation if slot == generation else None
        elif step=="R_read_tag":
            tag_seen=bool(r_seen_gen and flagged)
            tag_generation=generation if tag_seen else None
            # If tag and slot are both checked in one XArray locked
            # critical region, a tag from unrelated inode B cannot be
            # used as the observed M slot's tag.
            if require_locked_tag_slot:
                tag_seen &= slot==generation and mapping=="M"
        elif step=="R_read_isize":
            pass  # inode M is pinned and its size remains valid
        elif step=="R_xas_reload":
            accepted=tag_seen and slot is not None and r_seen_gen is not None
            # Pointer address equality is always true across A/B/C
            # generations. The bool above records only XArray occupancy.
        elif step=="R_check_mapping":
            mapping_pass=accepted and mapping=="M" and slot is not None
        elif step=="R_copy":
            copied_after_free=bool(mapping_pass and actual_free)
            if copied_after_free:
                witness={"flag_generation":tag_generation,
                         "copy_generation":generation,
                         "reader_original_generation":r_seen_gen,
                         "copy_without_live_page":True}
        elif step=="R_unlock":
            rcu=False
        elif step=="W_remove_A":
            slot=None
        elif step=="W_free_A":
            generation=None
            actual_free=True
        elif step=="W_allocate_B_other_inode":
            generation="B";mapping="N";flagged=False;actual_free=False
        elif step=="W_mark_B":
            flagged=True
        elif step=="W_evict_inode_B":
            exiting_b=True
        elif step=="W_clear_tag_and_remove_B":
            if exiting_b and not b_inode_exit_avoids_early_clear:
                flagged=False
            slot=None
        elif step=="W_free_B":
            # Normal code should defer tagged B; model controls
            # disallow B reallocation while R is in its RCU section.
            if flagged and rcu:
                pending_free=True
                return False, {}
            actual_free=True
            generation=None
        elif step=="W_allocate_C_original_inode":
            if pending_free:
                return False, {}
            generation="C";mapping="M";flagged=protected_c;actual_free=False
        elif step=="W_publish_C_same_slot":
            slot="C"
        elif step=="W_remove_C":
            slot=None
        elif step=="W_free_C":
            if flagged and rcu:
                pending_free=True
            else:
                actual_free=True
    return copied_after_free,witness

def main():
    positive=source_gates(SRC)
    assert len(positive)==14
    bad=[x for x,v in positive.items() if not v]
    assert not bad, f"Source path changed: {bad}"
    mutants={
        "remove_early_clear_on_inode_exit":("mm/filemap.c",
          "folio_test_rcu_free(folio) && mapping_exiting(mapping)"),
        "remove_rcu_flag_precheck":("mm/filemap.c",
          "READ_ONCE(folio->flags.f) & __PG_RCU_FREE"),
        "remove_reader_mapping_check":("mm/filemap.c",
          "READ_ONCE(folio->mapping) != mapping"),
        "remove_deferred_allocator_free":("mm/page_alloc.c","rcu_free_defer(folio);"),
        "remove_set_exiting":("mm/truncate.c","mapping_set_exiting(mapping);"),
        "remove_SM_T311_single_tag_preservation":("mm/filemap.c",
          "\tif (!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&"),
        "remove_SM_T311_batch_tag_preservation":("mm/filemap.c",
          "\t\tif (!IS_ENABLED(CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT) &&"),
    }
    negative={}
    for label,(file,needle) in mutants.items():
        assert needle in SRC[file],label
        changed=dict(SRC)
        changed[file]=changed[file].replace(needle,"")
        failed=[name for name,valid in source_gates(changed).items() if not valid]
        assert failed, f"mutation escaped: {label}"
        negative[label]=failed

    states={"SC_total":0,"cross_generation_tag_read_before_slot_reload":0,
            "potential_unprotected_C_copy":0,
            "copy_with_locked_tag_and_slot":0,
            "copy_if_B_early_clear_disallowed":0,
            "copy_if_C_always_marked":0}
    example=None
    for order in schedules():
        states["SC_total"]+=1
        passed,witness=simulate(order)
        if passed:
            states["potential_unprotected_C_copy"]+=1
            if example is None:
                example=list(order)
            if witness["flag_generation"]=="B":
                states["cross_generation_tag_read_before_slot_reload"]+=1
        if simulate(order,require_locked_tag_slot=True)[0]:
            states["copy_with_locked_tag_and_slot"]+=1
        if simulate(order,b_inode_exit_avoids_early_clear=True)[0]:
            states["copy_if_B_early_clear_disallowed"]+=1
        if simulate(order,protected_c=True)[0]:
            states["copy_if_C_always_marked"]+=1
    assert states["SC_total"]==75582,states
    assert states["cross_generation_tag_read_before_slot_reload"]>0,states
    # Intent: a locked check of the flag belonging to M's XArray slot
    # refuses the other inode B; not a proposed fastpath algorithm.
    assert states["copy_with_locked_tag_and_slot"]==0,states
    assert states["copy_if_B_early_clear_disallowed"]==0,states
    assert states["copy_if_C_always_marked"]==0,states

    data={
        "stage":"G674","classification":"SC source-linked cross-mapping three-generation race DIAGNOSTIC",
        "source_checks":positive,"positive_checks":len(positive),
        "current_SM_T311_patched_model_witnesses":
            states["copy_if_B_early_clear_disallowed"],
        "upstream_inode_exit_early_clear_reference_witnesses":
            states["potential_unprotected_C_copy"],
        "mutated_source_negatives_rejected":negative,
        "events":{"reader":R,"writer":W},
        "interleavings":states,"first_witness":example,
        "preconditions":[
            "Reader of inode M pins M and obtains an unmarked folio A address via XArray.",
            "A is removed/freed (normal unmarked allocator) while reader is RCU locked.",
            "The same physical page becomes B on DIFFERENT inode N; B acquires PG_rcu_free.",
            "Reader sees B's marked flag, but B's inode N enters final eviction; the tag may be cleared before freeing B because no actual N inode reader exists.",
            "Same physical address becomes new unmarked folio C for inode M, inserted into the same XArray slot; pointer and mapping checks can pass after the unrelated tag read.",
            "C may be removed and freed without RCU deferral before the earlier speculative reader copies."
        ],
        "cautions":[
            "This is a necessary-order feasibility witness, not proof allocator will reuse the page twice in this timing on Linux/ARMv7.",
            "The source model does not emulate real XArray node memory ordering, cache coherence, RCU grace, inode refcount internals or kernel object initialization.",
            "A real runtime UAF, wrong-byte response, exploitable fault or kernel panic has NOT been observed.",
            "The patched path preserves the tag when N exits; source-model counterexamples disappear, not a full proof.",
            "Do not use absence of witness in simplified control models as a correctness proof.",
            "S2 remains BLOCKED, S3 remains BLOCKED, no-ref RCU stays DEFAULT OFF. No flash."
        ]}
    OUT.write_text(json.dumps(data,indent=2)+"\n")
    print("G674",states, "source gates",len(positive),
          "negative source mutants",len(negative))
    print("SC cross-generation witness",example)
if __name__=="__main__":
    main()
