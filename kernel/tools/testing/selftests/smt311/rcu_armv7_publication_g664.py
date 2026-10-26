#!/usr/bin/env python3
"""G664 ARMv7 marker publication/XArray/RCU-GP source safety preflight.

This verifies a bounded set of *actual source paths*, not a weak-memory
proof, closed-world callgraph analysis, kernel binary test, or ON approval.
Negative controls rerun exactly the original checks over mutated sources.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
PATHS = {
    "filemap": "mm/filemap.c",
    "page_alloc": "mm/page_alloc.c",
    "page_flags": "include/linux/page-flags.h",
    "pagemap": "include/linux/pagemap.h",
    "arm_bitops": "arch/arm/include/asm/bitops.h",
    "arm_testset": "arch/arm/lib/testsetbit.S",
    "arm_bitops_macro": "arch/arm/lib/bitops.h",
    "arm_barrier": "arch/arm/include/asm/barrier.h",
    "arm_defconfig": "arch/arm/configs/sm_t311_android11_defconfig",
    "xarray": "include/linux/xarray.h",
    "xarray_impl": "lib/xarray.c",
    "memory_failure": "mm/memory-failure.c",
    "huge": "mm/huge_memory.c",
}
SOURCE = {key: (ROOT / value).read_text() for key, value in PATHS.items()}
OUT = ROOT / "Documentation/mm/sm-t311-rcu-armv7-publication-g664.json"


def section(text, start, end):
    return text.split(start, 1)[1].split(end, 1)[0]


def ordered(text, *tokens):
    return all(token in text for token in tokens) and list(map(text.index, tokens)) == sorted(map(text.index, tokens))


def checks(src):
    f = src["filemap"]
    a = src["page_alloc"]
    mark = section(f, "static noinline void filemap_mark_hot(", "void filemap_read_accessed(")
    lookup = section(f, "static struct folio *filemap_read_rcu_lookup(",
                     "static size_t filemap_read_rcu_size(")
    read_batch = section(f, "static bool filemap_read_rcu_batch(", "size_t filemap_read_rcu(")
    recycler = section(a, "static __always_inline bool __free_pages_prepare(",
                       "static bool free_pages_prepare(")
    start = section(a, "static void rcu_free_start(", "static void rcu_free_take(")
    expedited = section(a, "static unsigned long rcu_free_flush(", "static void rcu_free_workfn(")
    handler = section(a, "static void rcu_free_workfn(", "static void rcu_free_expedite_workfn(")
    xload = section(src["xarray_impl"], "void *xas_load(", "EXPORT_SYMBOL_GPL(xas_load)")
    # The comparisons are source-ordered and fail closed if an expected
    # primitive disappears. They are NOT a C/ARMv7 interleaving proof.
    return {
        "board_armv7_smp": "CONFIG_SMP=y" in src["arm_defconfig"],
        "board_preempt_rcu": "CONFIG_PREEMPT_RCU=y" in src["arm_defconfig"],
        "mark_under_ref_atomic_rmw": "folio_test_set_rcu_free(folio)" in mark,
        "mark_before_mapping_opt_in": ordered(mark, "folio_test_set_rcu_free(folio)", "mapping_set_rcu_read(mapping)"),
        "mark_4k_normal_only": "folio_zonenum(folio) != ZONE_NORMAL" in mark and
                               "folio_test_large(folio)" in mark,
        "pageflag_uses_test_and_set_bit": (
            "FOLIO_TEST_SET_FLAG(rcu_free, FOLIO_HEAD_PAGE)" in src["page_flags"] and
            "test_and_set_bit(PG_##name, folio_flags(folio, page))" in src["page_flags"]),
        "armv7_smp_dispatches_atomic_asm": (
            "#define ATOMIC_BITOP(name,nr,p)\t\t_##name(nr,p)" in src["arm_bitops"] and
            "#define test_and_set_bit(nr,p)" in src["arm_bitops"]),
        "arm_testset_instantiates_testop": "testop\t_test_and_set_bit, orreq, streq" in
                                           src["arm_testset"],
        "arm_testop_dmb_before_and_after": (
            "smp_dmb" in src["arm_bitops_macro"] and
            "testop, name, instr, store" in src["arm_bitops_macro"] and
            src["arm_bitops_macro"].count("\t\\barrier") >= 2),
        "armv7_smp_mb_is_inner_shareable_dmb": (
            "#define __smp_mb()\tdmb(ish)" in src["arm_barrier"] and
            "#define __smp_rmb()\t__smp_mb()" in src["arm_barrier"]),
        "reader_rcu_and_fault_safeguard": (
            "rcu_read_lock();" in read_batch and "pagefault_disable();" in read_batch and
            "pagefault_enable();" in read_batch and "rcu_read_unlock();" in read_batch),
        "reader_flag_size_reload_order": (
            "xas_reload(xas) != folio" in lookup and
            lookup.index("READ_ONCE(folio->flags.f) & __PG_RCU_FREE") <
            lookup.index("smp_rmb();") < lookup.index("i_size_read(inode)") <
            lookup.index("xas_reload(xas) != folio")),
        "reader_second_barrier_after_reload": (
            lookup.count("smp_rmb();") >= 3 and
            lookup.index("xas_reload(xas) != folio") <
            lookup.rindex("smp_rmb();")),
        "xarray_reader_load_under_rcu": (
            "folio = xas_load(xas);" in lookup and
            "xas_start(xas)" in xload and "xas_descend(xas, node)" in xload),
        "xarray_node_retired_after_rcu": (
            "call_rcu(&node->rcu_head, radix_tree_node_rcu_free)" in src["xarray_impl"]),
        "xarray_slot_rcu_dereference": (
            "rcu_dereference_check(node->slots[offset]" in src["xarray"]),
        "xarray_reload_rechecks_slot": (
            "return xa_entry(xas->xa, node, offset);" in src["xarray"]),
        "tagged_final_free_diverted": (
            "folio_test_rcu_free(folio)" in recycler and
            recycler.index("__folio_clear_rcu_free(folio)") <
            recycler.index("rcu_free_defer(folio)") <
            recycler.index("return false;")),
        "rcu_work_grace_before_recycle": (
            "queue_rcu_work(rcu_free_wq, &r->rwork)" in start and
            "to_rcu_work(work)" in handler and "rcu_free_pages(&done)" in handler),
        "expedited_gp_before_free": (
            ordered(expedited, "synchronize_rcu_expedited();", "rcu_free_pages(list)")),
        "hwpoison_lastfree_exception_has_grace": (
            "if (release && folio_test_rcu_free(page_folio(page)))\n\t\tsynchronize_rcu();" in
            src["memory_failure"]),
        "hwpoison_unmap_exception_has_grace": (
            "if (!res && folio_test_rcu_free(folio))\n\t\tsynchronize_rcu();" in
            src["memory_failure"]),
        "split_has_rcu_tag_before_tail_switch": (
            "__PG_RCU_FREE" in src["huge"] and
            src["huge"].index("__PG_RCU_FREE") <
            src["huge"].index("smp_wmb();", src["huge"].index("__PG_RCU_FREE")) <
            src["huge"].index("clear_compound_head(new_head);",
                              src["huge"].index("__PG_RCU_FREE"))),
    }


def mutate(key, before, after=""):
    source = dict(SOURCE)
    assert before in source[key], (key, before)
    source[key] = source[key].replace(before, after, 1)
    return source


def main():
    positive = checks(SOURCE)
    assert len(positive) == 23, len(positive)
    failing = [key for key, ok in positive.items() if not ok]
    if failing:
        raise SystemExit("G664 SOURCE GATE FAIL: " + ", ".join(failing))
    negative = {
        "drop_arm_smp_testop_barrier":
            mutate("arm_bitops_macro", "\t\\barrier\n", "") ,
        "drop_atomic_mark":
            mutate("filemap", "if (folio_test_set_rcu_free(folio))", "if (false)"),
        "drop_xarray_node_rcu":
            mutate("xarray_impl", "call_rcu(&node->rcu_head, radix_tree_node_rcu_free)", "radix_tree_node_rcu_free(&node->rcu_head)"),
        "drop_rcu_work_gp":
            mutate("page_alloc", "queue_rcu_work(rcu_free_wq, &r->rwork)", "queue_work(rcu_free_wq, &r->rwork)"),
        "drop_expedited_gp":
            mutate("page_alloc", "synchronize_rcu_expedited();", "/* gone */"),
    }
    rejected = {name: [k for k, ok in checks(src).items() if not ok]
                for name, src in negative.items()}
    failed_controls = [name for name, bad in rejected.items() if not bad]
    if failed_controls:
        raise SystemExit("G664 MUTATION GATE FAIL: " + ", ".join(failed_controls))
    result = {
        "stage": "G664",
        "type": "SOURCE STRUCTURAL PREFLIGHT ONLY",
        "kernel_runtime_base": "a2fca4558",
        "positive_pass": len(positive),
        "checks": positive,
        "negative_controls": rejected,
        "s2": "BLOCKED",
        "s3": "BLOCKED",
        "rcu_no_ref_default": "OFF",
        "limitations": [
            "Confirms symbol and barrier implementation ordering, not CPU-level or LKMM global correctness.",
            "xas_reload rechecks a cached slot, not allocation-generation identity.",
            "Last-free and hwpoison/eviction/migration coverage is not a closed-world proof.",
            "Does not qualify a physical RCU ON test or a BOOT flash."
        ],
    }
    OUT.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n")
    print("G664 structural guards:", len(positive), "PASS;",
          len(rejected), "real mutated-source negative gates REJECT; S2/S3 BLOCKED")


if __name__ == "__main__":
    main()
