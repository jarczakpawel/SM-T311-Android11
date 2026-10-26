SM-T311 G666: ext4 writer to XArray eviction source-chain audit
==================================================================

Purpose
-------
Finish another segment of the S1 reader/writer matrix, without changing
the G650 production image or enabling the reference-free RCU experiment.
This audit verifies that selected ext4 mutation operations route through
generic MM page-cache removal and that the existing last-free path
retains the delayed-free obligation for tagged folios.

Verified graph for direct I/O writes
------------------------------------
1. fs/ext4/file.c ext4_file_write_iter() dispatches IOCB_DIRECT to
   ext4_dio_write_iter(), which calls iomap_dio_rw().
2. fs/iomap/direct-io.c __iomap_dio_rw() calls
   kiocb_invalidate_pages(iocb, iomi.len) BEFORE inode_dio_begin()
   when doing a direct write. On failure it returns through
   out_free_dio without submitting that direct I/O. The ext4 caller
   may fall back to buffered I/O when the direct path cannot proceed.
3. mm/filemap.c kiocb_invalidate_pages() calls
   filemap_invalidate_pages(). The latter waits for writeback and
   calls invalidate_inode_pages2_range() for blocking invalidation.
4. mm/truncate.c invalidate_inode_pages2_range() calls
   folio_unmap_invalidate() after acquiring the folio lock and waiting
   for writeback. folio_unmap_invalidate() removes the folio via
   __filemap_remove_folio() under the XArray lock, then drops its
   folio references.
5. mm/filemap.c __filemap_remove_folio() reaches page_cache_delete().
   The latter overwrites the XArray slot before setting folio->mapping
   to NULL and normally retains PG_rcu_free when it is set.
6. mm/page_alloc.c __free_pages_prepare() defers tagged last-free
   rather than returning the folio immediately to the allocator.

Selected truncate and punch-hole graphs
---------------------------------------
* fs/ext4/extents.c ext4_fallocate() holds invalidate_lock for the
  range-changing modes, and FALLOC_FL_PUNCH_HOLE enters
  ext4_punch_hole().
* fs/ext4/inode.c ext4_punch_hole() calls
  ext4_truncate_page_cache_block_range(), then
  truncate_pagecache_range().
* mm/truncate.c truncate_pagecache_range() calls
  truncate_inode_pages_range(), which removes complete folios via
  delete_from_page_cache_batch() and single entries via
  truncate_inode_folio(), converging on the generic XArray removal.
* fs/ext4/inode.c ext4_setattr() calls truncate_pagecache() for size
  changes, which converges on truncate_inode_pages().

Limitations
-----------
The special no-ref filemap_read_rcu_batch() does not take
filemap_invalidate_lock. The writer's invalidate_lock therefore is not
a lifetime lock for such a speculative reader. Instead correctness
depends on RCU grace, tagged folio lifetime, correct flag publication
and XArray synchronization. These still require full source/ARMv7
weak-memory proof.

The direct-I/O fallback calls invalidate_mapping_pages(), a best-effort
invalidation of clean unlocked pages, not an all-pages-guaranteed purge.
Mmap races, partial-folio boundary zeroing, direct I/O completion,
inode eviction, migration, HighMem cache alias and all other writers
remain separate S1/S2/S3 obligations. This source-derived graph is not
a closed-world callgraph, runtime result, safety proof or performance test.

Reproducibility
---------------
Run::

    python3 tools/testing/selftests/smt311/rcu_ext4_invalidation_g666.py

The source gate passed 25/25 distinct checks against the local Linux 7.3
tree. Five independently altered input sources were correctly rejected
when re-evaluated by the same full source graph. Raw machine result is
Documentation/mm/sm-t311-rcu-ext4-invalidation-g666.json.

The previous G665 native userspace XArray binary remains NOT RUN:
liburcu development headers and local package index are absent on
the host. The installed liburcu runtime .so is insufficient to build
that test. Do not fabricate substitute headers or report PASS.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED; S5 BLOCKED;
S6 BLOCKED; S7 PARTIAL. No new BOOT image, flash, backup or RCU ON.
