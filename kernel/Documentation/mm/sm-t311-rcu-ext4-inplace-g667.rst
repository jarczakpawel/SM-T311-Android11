SM-T311 G667: ext4 in-place folio mutations versus reference-free copy
=========================================================================

Purpose
-------
Audit the difference between data mutability of a LIVE file-cache folio
and safety of its lifetime when a reader copies without a folio refcount.
Do not treat all concurrent old/new-data reads as allocator use-after-
free. No production source is changed in this audit.

Direct evidence from the local Linux 7.3 tree
---------------------------------------------
* ext4_fallocate() acquires inode lock, waits outstanding DIO, then takes
  the mapping invalidate lock for extent-changing operations and
  dispatches FALLOC_FL_ZERO_RANGE, FALLOC_FL_COLLAPSE_RANGE and
  FALLOC_FL_INSERT_RANGE.
* ext4_zero_range() can call ext4_zero_partial_blocks() BEFORE
  ext4_truncate_page_cache_block_range() evicts the full aligned page
  cache range. ext4_block_zero_range() selects the non-journalled or
  journalled helper; both call folio_zero_range() on an existing folio
  obtained via ext4_load_tail_bh() while holding its lock and reference.
  The data of a still-live folio can change; the folio lock protects the
  writer but does not serialize an unreferenced reader's direct copy.
* ext4_collapse_range() writes back and waits for dirty pages, calls
  truncate_pagecache(), and then shifts extents. ext4_insert_range()
  similarly waits, truncates page cache before changing file size and
  extent positions. Deletion still relies on last-free RCU protection
  for any reader already inside the removed folio.
* ext4_buffered_write_iter() holds inode lock and dispatches to
  generic_perform_write(). ext4_page_mkwrite() uses shared invalidate
  lock, folio lock and an explicit folio->mapping identity check in
  the relevant branch. It does not lock a normal file read.
* filemap_read_rcu_batch() checks mapping_writably_mapped(mapping)
  before entering its RCU critical section on ARM configurations
  requiring explicit dcache flushing. It does not acquire folio lock,
  mapping invalidate_lock or a folio refcount while copying.
  filemap_read_rcu_copy() directly uses kmap_local_page() and
  _copy_to_iter(), not an atomic snapshot. The existence of this
  writable-mapping check is not proof it remains true across a
  concurrent transition that enables a writable mapping.

A live folio concurrently written and read can exhibit normal partial
visibility, including mixed old/new bytes, without implying a security
bug. However, if the folio is physically freed and repurposed for an
unrelated object during the same read, this is a separate and stronger
safety violation. RCU and object-identity verification must prevent
that regardless of data mutability.

Source gate
-----------
Run::

    python3 tools/testing/selftests/smt311/rcu_ext4_inplace_g667.py

Observed: 26/26 source conditions PASS; five independently mutated
input variants are rejected by the same check set. Inputs cover
zero_range, normal/journalled partial zeroing, collapse/insert,
buffered write, page_mkwrite and the no-ref reader. The result is
retained in Documentation/mm/sm-t311-rcu-ext4-inplace-g667.json.

These are source structural conditions, NOT a C memory-race proof,
ARMv7 LKMM proof, a filesystem consistency or correctness test, or
an executable Android regression test. Whole-program ordering with
mmap writable alias/cache invalidation, old unmarked folio ABA,
inode eviction, HighMem and live access under ext4 writer mutations
remains unproven.

Deployment
----------
Physical G650 reference-free RCU stays default OFF. No kernel mm/fs/
arch runtime changes were introduced, no flash or backups.

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL.
