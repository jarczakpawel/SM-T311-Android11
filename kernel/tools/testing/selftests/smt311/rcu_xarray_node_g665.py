#!/usr/bin/env python3
"""G665 conservative XArray cached-slot retirement source checks.

Checks that a detached old XArray node cannot retain an ordinary folio
pointer through the inspected shrink/destroy/empty-node retirement paths.
Not an LKMM proof or complete code-path enumeration.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "Documentation/mm/sm-t311-xarray-node-g665.json"


def section(source, start, stop):
    return source.split(start, 1)[1].split(stop, 1)[0]


def validate(source):
    x = source["lib/xarray.c"]
    h = source["include/linux/xarray.h"]
    f = source["mm/filemap.c"]
    shrink = section(x, "static void xas_shrink(", "static void xas_delete_node(")
    delete = section(x, "static void xas_delete_node(", "static void xas_free_nodes(")
    subtree = section(x, "static void xas_free_nodes(", "static int xas_expand(")
    node_free = section(x, "static void xa_node_free(", "void xas_destroy(")
    reload = section(h, "static inline void *xas_reload(", "static inline void xas_set(")
    load = section(x, "void *xas_load(", "EXPORT_SYMBOL_GPL(xas_load)")
    retry = section(h, "static inline bool xas_retry(", "void *xas_load(")
    reader = section(f, "static struct folio *filemap_read_rcu_lookup(",
                     "static size_t filemap_read_rcu_size(")
    return {
        "reader_rcu_initial_xas_load": "folio = xas_load(xas);" in reader,
        "reader_rejects_retry_at_initial_load": "xas_retry(xas, folio)" in reader,
        "reader_uses_cached_slot_reload": "xas_reload(xas) != folio" in reader,
        "reload_uses_cached_xa_node": "struct xa_node *node = xas->xa_node;" in reload,
        "reload_reads_node_slot": "return xa_entry(xas->xa, node, offset);" in reload,
        "load_traverses_root": "xas_start(xas)" in load,
        "retry_resets_state": "xas_reset(xas);" in retry,
        "shrink_replaces_root": "RCU_INIT_POINTER(xa->xa_head, entry);" in shrink,
        "shrink_nonchild_slot_gets_retry": (
            "if (!xa_is_node(entry))" in shrink and
            "RCU_INIT_POINTER(node->slots[0], XA_RETRY_ENTRY);" in shrink),
        "shrink_retires_old_node": "xa_node_free(node);" in shrink,
        "delete_node_clears_parent_slot": "parent->slots[xas->xa_offset] = NULL;" in delete,
        "delete_node_retires_old_node": "xa_node_free(node);" in delete,
        "free_subtree_overwrites_live_slot_with_retry": (
            "if (entry)" in subtree and
            "RCU_INIT_POINTER(node->slots[offset], XA_RETRY_ENTRY);" in subtree),
        "free_subtree_retires_nodes": "xa_node_free(node);" in subtree,
        "node_free_marks_retirement": "node->array = XA_RCU_FREE;" in node_free,
        "node_free_deferred_by_call_rcu": (
            "call_rcu(&node->rcu_head, radix_tree_node_rcu_free);" in node_free),
        "xas_set_restores_root_walk": "xas->xa_node = XAS_RESTART;" in h,
        "existing_retry_regression": "check_xas_retry(&array);" in
                                      source["lib/test_xarray.c"],
        "existing_shrink_regression": "check_xa_shrink(&array);" in
                                       source["lib/test_xarray.c"],
        "test_xarray_checks_stale_slot_retry": (
            "xa_is_retry(xas_reload(&xas))" in source["lib/test_xarray.c"]
            or "xa_entry_locked(xa, node, 0) != XA_RETRY_ENTRY" in source["lib/test_xarray.c"]),
    }


def main():
    names = ["lib/xarray.c", "include/linux/xarray.h", "mm/filemap.c",
             "lib/test_xarray.c"]
    sources = {name: (ROOT / name).read_text() for name in names}
    ok = validate(sources)
    assert len(ok) == 20, len(ok)
    missing = [key for key, found in ok.items() if not found]
    if missing:
        raise SystemExit("SOURCE CHECK FAIL: " + ", ".join(missing))

    mutations = {
        "shrink_retry_removed": ("lib/xarray.c",
            "RCU_INIT_POINTER(node->slots[0], XA_RETRY_ENTRY);"),
        "subtree_retry_removed": ("lib/xarray.c",
            "RCU_INIT_POINTER(node->slots[offset], XA_RETRY_ENTRY);"),
        "node_grace_removed": ("lib/xarray.c",
            "call_rcu(&node->rcu_head, radix_tree_node_rcu_free);"),
        "parent_slot_clear_removed": ("lib/xarray.c",
            "parent->slots[xas->xa_offset] = NULL;"),
        "cached_reader_reload_removed": ("mm/filemap.c",
            "xas_reload(xas) != folio"),
    }
    rejection = {}
    for name, (filename, token) in mutations.items():
        assert token in sources[filename], (name, token)
        changed = dict(sources)
        changed[filename] = changed[filename].replace(token, "", 1)
        misses = [key for key, found in validate(changed).items() if not found]
        if not misses:
            raise SystemExit("MUTATION NOT REJECTED: " + name)
        rejection[name] = misses

    result = {
        "stage": "G665",
        "audit": "SOURCE RETIREMENT PATHS ONLY",
        "positive_checks": ok,
        "positive_pass": len(ok),
        "rejected_negative_mutations": rejection,
        "test_xarray_host_runtime": "BLOCKED: liburcu development headers absent",
        "remaining": [
            "RCU protection covers node memory lifetime, but not root membership.",
            "The inspected shrink and subtree cleanup paths replace non-child old slots with retry markers.",
            "A null slot after final deletion is not a positive folio equality match.",
            "Unmodeled XArray paths, allocator generation ABA, weak memory, and writer races remain open.",
            "S1 ACTIVE S2/S3 BLOCKED; physical reference-free RCU must stay OFF."
        ]
    }
    OUT.write_text(json.dumps(result, indent=2) + "\n")
    print("G665 XArray cached-node source gates:", len(ok), "PASS,",
          len(rejection), "mutants rejected, runtime host build BLOCKED (liburcu)")


if __name__ == "__main__":
    main()
