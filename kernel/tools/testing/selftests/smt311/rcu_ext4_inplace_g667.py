#!/usr/bin/env python3
"""G667: ext4 in-place folio updates versus no-ref page-cache read.

Structural source audit only, not a C/ARMv7 concurrency proof. This
distinguishes a live-data race (ordinary overlapping writes) from
folio last-free / XArray ABA safety.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
PATHS = (
    "fs/ext4/inode.c", "fs/ext4/extents.c", "fs/ext4/file.c",
    "mm/filemap.c", "mm/truncate.c", "include/linux/pagemap.h"
)
FILES = {name: (ROOT / name).read_text() for name in PATHS}
OUT = ROOT / "Documentation/mm/sm-t311-rcu-ext4-inplace-g667.json"


def function(text, name):
    for match in re.finditer(r"\b" + re.escape(name) + r"\s*\(", text):
        level = 0
        close = 0
        for i in range(text.find("(", match.start()), len(text)):
            level += (text[i] == "(") - (text[i] == ")")
            if level == 0:
                close = i + 1
                break
        start = re.match(r"\s*\{", text[close:])
        if start:
            begin = close + start.end()
            end = text.find("\n}", begin)
            if end < 0:
                raise ValueError(name)
            return text[begin:end]
    raise ValueError("Missing function " + name)


def order(text, *tokens):
    return all(token in text for token in tokens) and all(
        text.index(tokens[i]) < text.index(tokens[i + 1])
        for i in range(len(tokens) - 1))


def check(src):
    ext = src["fs/ext4/extents.c"]
    ino = src["fs/ext4/inode.c"]
    fil = src["fs/ext4/file.c"]
    mm = src["mm/filemap.c"]
    tr = src["mm/truncate.c"]
    falloc = function(ext, "ext4_fallocate")
    zero = function(ext, "ext4_zero_range")
    collapse = function(ext, "ext4_collapse_range")
    insert = function(ext, "ext4_insert_range")
    partial = function(ino, "ext4_zero_partial_blocks")
    block_zero = function(ino, "ext4_block_zero_range")
    plain_zero = function(ino, "ext4_block_do_zero_range")
    journal_zero = function(ino, "ext4_block_journalled_zero_range")
    mkwrite = function(ino, "ext4_page_mkwrite")
    buffered = function(fil, "ext4_buffered_write_iter")
    rcu = function(mm, "filemap_read_rcu_batch")
    copy = function(mm, "filemap_read_rcu_copy")
    truncate = function(tr, "truncate_inode_pages_range")
    return {
        "fallocate_wait_dio_before_invalidate": order(
            falloc, "inode_dio_wait(inode);", "filemap_invalidate_lock(mapping);"),
        "fallocate_lock_before_mode_dispatch": order(
            falloc, "filemap_invalidate_lock(mapping);",
            "ext4_zero_range(file, offset, len, mode)"),
        "fallocate_zero_range_dispatch": "ext4_zero_range(file, offset, len, mode)" in falloc,
        "fallocate_collapse_dispatch": "ext4_collapse_range(file, offset, len)" in falloc,
        "fallocate_insert_dispatch": "ext4_insert_range(file, offset, len)" in falloc,
        "zero_partial_first_then_pagecache_invalidate": order(
            zero, "ext4_zero_partial_blocks(inode, offset, len,",
            "ext4_truncate_page_cache_block_range(inode, offset, end)"),
        "zero_uses_partial_zero_helper": "ext4_zero_partial_blocks(inode, offset, len," in zero,
        "partial_zero_calls_block_zero": (
            "ext4_block_zero_range(inode, lstart, length," in partial and
            "ext4_block_zero_range(inode, lstart, sb->s_blocksize," in partial),
        "block_zero_dispatches_normal_and_journal": (
            "ext4_block_do_zero_range(inode, from, length" in block_zero and
            "ext4_block_journalled_zero_range(inode, from, length" in block_zero),
        "plain_zero_locked_before_folio_unlock": order(
            plain_zero, "folio_zero_range(folio, offset_in_folio(folio, from), length);",
            "folio_unlock(folio);"),
        "journal_zero_locked_before_folio_unlock": order(
            journal_zero, "folio_zero_range(folio, offset_in_folio(folio, from), length);",
            "folio_unlock(folio);"),
        "plain_zero_releases_reference": "folio_put(folio);" in plain_zero,
        "journal_zero_releases_reference": "folio_put(folio);" in journal_zero,
        "collapse_wait_writeback_before_truncate": order(
            collapse, "filemap_write_and_wait_range(mapping, start, offset)",
            "truncate_pagecache(inode, start)"),
        "collapse_truncate_before_shift": order(
            collapse, "truncate_pagecache(inode, start)",
            "ext4_ext_shift_extents(inode, handle, end_lblk,"),
        "insert_wait_writeback_before_truncate": order(
            insert, "filemap_write_and_wait_range(mapping, start, LLONG_MAX)",
            "truncate_pagecache(inode, start)"),
        "insert_truncate_before_size_change": order(
            insert, "truncate_pagecache(inode, start)", "inode->i_size += len;"),
        "buffered_write_passes_generic_writer": (
            "generic_perform_write(iocb, from)" in buffered and
            "inode_lock(inode);" in buffered),
        "mmap_page_mkwrite_has_invalidate_shared": (
            "filemap_invalidate_lock_shared(mapping)" in mkwrite and
            "filemap_invalidate_unlock_shared(mapping)" in mkwrite),
        "mmap_mkwrite_locks_folio": "folio_lock(folio);" in mkwrite,
        "mmap_mkwrite_checks_mapping_identity": (
            "folio->mapping != mapping" in mkwrite),
        "normal_mmap_alias_guard": (
            "ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE &&" in rcu and
            "mapping_writably_mapped(mapping)" in rcu),
        "rcu_read_no_folio_lock": (
            "rcu_read_lock();" in rcu and
            "folio_lock(folio)" not in rcu and
            "folio_trylock(folio)" not in rcu),
        "rcu_reads_usercopy_direct_without_snapshot": (
            "kmap_local_page(page)" in copy and
            "_copy_to_iter(kaddr + offset, len, iter)" in copy),
        "truncate_full_folios_still_removed": (
            "delete_from_page_cache_batch(mapping, &fbatch)" in truncate),
        "rcu_copy_has_no_invalidate_lock": (
            "filemap_invalidate_lock(" not in rcu and
            "filemap_invalidate_lock_shared(" not in rcu),
    }


def main():
    base = check(FILES)
    assert len(base) == 26, len(base)
    bad = [name for name, passed in base.items() if not passed]
    if bad:
        raise SystemExit("G667 positive source check failures: " + ", ".join(bad))
    mutations = {
        "drop_partial_zero_before_invalidate": (
            "fs/ext4/extents.c", "ext4_zero_partial_blocks(inode, offset, len,"),
        "drop_unlocked_copy_guard": (
            "mm/filemap.c", "mapping_writably_mapped(mapping)"),
        "drop_plain_zero": (
            "fs/ext4/inode.c",
            "folio_zero_range(folio, offset_in_folio(folio, from), length);"),
        "drop_collapse_cache_invalidate": (
            "fs/ext4/extents.c", "truncate_pagecache(inode, start);"),
        "drop_mmap_invalidate_shared_lock": (
            "fs/ext4/inode.c", "filemap_invalidate_lock_shared(mapping);"),
    }
    negative = {}
    for name, (file, token) in mutations.items():
        assert token in FILES[file], name
        changed = dict(FILES)
        changed[file] = changed[file].replace(token, "", 1)
        missing = [key for key, passed in check(changed).items() if not passed]
        if not missing:
            raise SystemExit("Negative test escaped: " + name)
        negative[name] = missing

    result = {
        "stage": "G667",
        "kind": "Source-only ext4 in-place data mutation reader/writer matrix",
        "positives": base,
        "positive_pass": len(base),
        "rejected_source_mutations": negative,
        "key_distinction": (
            "A live mapped folio may be written while normal buffered or "
            "reference-free reads copy it. No atomic file read snapshot is promised "
            "for concurrent writers; this is distinct from freed-folio UAF or "
            "cross-inode data exposure."
        ),
        "unresolved": [
            "The mmap writable-alias guard is checked before RCU lock, and the "
            "audit does not prove a concurrent mmap enable cannot race it.",
            "The folio lock held by ext4 partial zeroing does not serialize "
            "the no-reference fast read's direct _copy_to_iter.",
            "Complete ARMv7 cache alias, lifetime and weak-memory proof is absent.",
            "G665 C XArray binary UNRUN due missing liburcu development headers.",
        ],
        "stages": {
            "S0": "PASS", "S1": "ACTIVE", "S2": "BLOCKED",
            "S3": "BLOCKED", "S4": "BLOCKED", "S5": "BLOCKED",
            "S6": "BLOCKED", "S7": "PARTIAL"
        },
        "production_runtime_change": False
    }
    OUT.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n")
    print("G667 ext4 in-place writer graph", len(base), "PASS;",
          len(negative), "mutated-source controls rejected; S2/S3 BLOCKED")


if __name__ == "__main__":
    main()
