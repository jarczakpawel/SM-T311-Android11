#!/usr/bin/env python3
"""G666 source-derived ext4 writer -> filemap eviction graph with mutants.

A structural gate only: not real concurrency, weak-memory/ARMv7 proof,
runtime Android RCU ON test, or all possible filesystem data operations.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
NAMES = (
    "fs/ext4/file.c", "fs/ext4/inode.c", "fs/ext4/extents.c",
    "fs/iomap/direct-io.c", "mm/filemap.c", "mm/truncate.c",
    "mm/page_alloc.c",
)
SOURCES = {name: (ROOT / name).read_text() for name in NAMES}
OUT = ROOT / "Documentation/mm/sm-t311-rcu-ext4-invalidation-g666.json"


def body(text, name):
    for found in re.finditer(r"\b" + re.escape(name) + r"\s*\(", text):
        start = text.find("(", found.start())
        level = 0
        end = 0
        for i in range(start, len(text)):
            if text[i] == "(":
                level += 1
            if text[i] == ")":
                level -= 1
                if level == 0:
                    end = i + 1
                    break
        if not end:
            continue
        match = re.match(r"\s*\{", text[end:])
        if match:
            begin = end + match.end()
            # Function body closing } at column zero, as in Linux style.
            closure = text.find("\n}", begin)
            if closure < 0:
                raise ValueError("Unterminated function: " + name)
            return text[begin:closure]
    raise ValueError("Function body not found: " + name)


def before(source, first, second):
    return first in source and second in source and source.index(first) < source.index(second)


def gates(src):
    ef = src["fs/ext4/file.c"]
    ei = src["fs/ext4/inode.c"]
    ee = src["fs/ext4/extents.c"]
    iod = src["fs/iomap/direct-io.c"]
    fm = src["mm/filemap.c"]
    tr = src["mm/truncate.c"]
    pa = src["mm/page_alloc.c"]

    write = body(ef, "ext4_file_write_iter")
    dio = body(ef, "ext4_dio_write_iter")
    iomap = body(iod, "__iomap_dio_rw")
    invalid = body(fm, "kiocb_invalidate_pages")
    invalid_range = body(fm, "filemap_invalidate_pages")
    invalidate2 = body(tr, "invalidate_inode_pages2_range")
    unmap = body(tr, "folio_unmap_invalidate")
    remove = body(fm, "__filemap_remove_folio")
    delete = body(fm, "page_cache_delete")
    defer = body(pa, "__free_pages_prepare")

    punch = body(ei, "ext4_punch_hole")
    block_range = body(ei, "ext4_truncate_page_cache_block_range")
    fallocate = body(ee, "ext4_fallocate")
    setattr = body(ei, "ext4_setattr")
    truncate = body(tr, "truncate_pagecache")
    truncate_range = body(tr, "truncate_pagecache_range")
    truncate_pages = body(tr, "truncate_inode_pages_range")
    batch_remove = body(fm, "delete_from_page_cache_batch")
    rcu_batch = body(fm, "filemap_read_rcu_batch")

    return {
        "ext4_write_dispatches_direct": "ext4_dio_write_iter(iocb, from)" in write,
        "ext4_direct_uses_iomap": "iomap_dio_rw(iocb, from" in dio,
        "ext4_direct_fallback_to_buffered": "ext4_buffered_write_iter(iocb, from)" in dio,
        "iomap_invalidates_before_dio_submission": before(
            iomap, "kiocb_invalidate_pages(iocb, iomi.len)", "inode_dio_begin(inode)"),
        "iomap_dio_invalidation_error_propagated": (
            "ret = kiocb_invalidate_pages(iocb, iomi.len);" in iomap and
            "if (ret)" in iomap and "goto out_free_dio;" in iomap),
        "kiocb_to_filemap_invalidates": "filemap_invalidate_pages(mapping" in invalid,
        "filemap_waits_then_invalidates": before(
            invalid_range, "filemap_write_and_wait_range(mapping, pos, end)",
            "invalidate_inode_pages2_range(mapping"),
        "invalidate2_calls_folio_unmap": "folio_unmap_invalidate(mapping, folio, GFP_KERNEL)" in invalidate2,
        "folio_unmap_under_xa_lock": before(
            unmap, "xa_lock_irq(&mapping->i_pages)", "__filemap_remove_folio(folio, NULL)"),
        "folio_unmap_puts_last_ref_after_removal": before(
            unmap, "__filemap_remove_folio(folio, NULL)",
            "folio_put_refs(folio, folio_nr_pages(folio))"),
        "remove_folio_deletes_slot": "page_cache_delete(mapping, folio, shadow)" in remove,
        "delete_slot_before_null_mapping": before(
            delete, "xas_store(&xas, shadow)", "folio->mapping = NULL"),
        "deletion_preserves_rcu_tag_except_eviction": (
            "folio_test_rcu_free(folio) && mapping_exiting(mapping)" in delete and
            "folio_clear_rcu_free(folio)" in delete),
        "page_allocator_defers_marked_last_free": before(
            defer, "__folio_clear_rcu_free(folio)", "rcu_free_defer(folio)"),
        "punch_calls_pagecache_range_truncate": (
            "ext4_truncate_page_cache_block_range(inode, offset, end)" in punch),
        "punch_pagecache_block_to_range": "truncate_pagecache_range(inode, start, end - 1)" in block_range,
        "range_uses_truncate_inode_pages": (
            "truncate_inode_pages_range(mapping, lstart, lend)" in truncate_range),
        "truncate_full_batch_deletion": "delete_from_page_cache_batch(mapping, &fbatch)" in truncate_pages,
        "truncate_full_single_deletion": "truncate_inode_folio(mapping, folio)" in truncate_pages,
        "batch_deletion_reaches_page_cache_delete_batch": (
            "page_cache_delete_batch(mapping, fbatch)" in batch_remove),
        "fallocate_punch_calls_ext4_hole": (
            "ext4_punch_hole(file, offset, len)" in fallocate),
        "fallocate_takes_invalidate_lock": before(
            fallocate, "filemap_invalidate_lock(mapping)", "ext4_punch_hole(file, offset, len)"),
        "setattr_shrink_calls_pagecache_truncate": "truncate_pagecache(inode, inode->i_size)" in setattr,
        "truncate_pagecache_deletes_cache": "truncate_inode_pages(mapping, newsize)" in truncate,
        "no_ref_reader_does_not_lock_invalidate": (
            "rcu_read_lock();" in rcu_batch and
            "filemap_invalidate_lock(" not in rcu_batch and
            "filemap_invalidate_lock_shared(" not in rcu_batch),
    }


def main():
    baseline = gates(SOURCES)
    assert len(baseline) == 25, len(baseline)
    missing = [key for key, good in baseline.items() if not good]
    if missing:
        raise SystemExit("G666 source checks failed: " + ", ".join(missing))
    mutations = {
        "disable_iomap_prewrite_invalidation": (
            "fs/iomap/direct-io.c", "ret = kiocb_invalidate_pages(iocb, iomi.len);"),
        "disable_strong_invalidate_remove": (
            "mm/truncate.c", "ret2 = folio_unmap_invalidate(mapping, folio, GFP_KERNEL);"),
        "disable_punchhole_eviction": (
            "fs/ext4/inode.c", "truncate_pagecache_range(inode, start, end - 1);"),
        "disable_direct_pagecache_delete": (
            "mm/filemap.c", "page_cache_delete(mapping, folio, shadow);"),
        "disable_tagged_free_defer": (
            "mm/page_alloc.c", "rcu_free_defer(folio);"),
    }
    rejected = {}
    for name, (path, term) in mutations.items():
        assert term in SOURCES[path], (name, term)
        changed = dict(SOURCES)
        changed[path] = changed[path].replace(term, "", 1)
        failure = [key for key, ok in gates(changed).items() if not ok]
        if not failure:
            raise SystemExit("G666 negative mutant escaped detection: " + name)
        rejected[name] = failure

    report = {
        "project": "SM-T311",
        "step": "G666",
        "classification": "SOURCE GRAPH ONLY; S1 ACTIVE; S2 BLOCKED",
        "positive_checks": baseline,
        "positive_count": len(baseline),
        "rejected_mutations": rejected,
        "host_xarray_g665_binary": "NOT RUN, missing liburcu-dev headers and package indices",
        "limitations": [
            "Source graph is only selected ext4 + iomap + MM call sites, not a complete call graph.",
            "The no-ref RCU reader does not use filemap invalidate_lock, so that lock alone is not the lifetime proof.",
            "iomap DIO invalidates cache before writing. A read already inside the removed folio still relies on RCU grace to avoid reuse.",
            "Direct I/O fallback invalidate_mapping_pages is best effort, not all pages necessarily evicted.",
            "No guaranteed safety for partial folio writes, hole edge zeroing, outstanding mmap, weak ARMv7 ordering or old unmarked ABA.",
            "No change to production runtime sources, physical G650 stays ref-free RCU OFF."
        ],
    }
    OUT.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
    print("G666 ext4/iomap/MM graph:", len(baseline), "PASS;",
          len(rejected), "source mutations rejected; safety S2 BLOCKED")


if __name__ == "__main__":
    main()
