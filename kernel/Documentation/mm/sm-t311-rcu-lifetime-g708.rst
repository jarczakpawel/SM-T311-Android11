G708 - SM-T311 ARMv7 no-reference RCU lifetime path audit
=======================================================

2026-10-21, Linux 7.3.0-pjarczak, staged G692 RCU experiment;
G700 release-OFF candidate remains host-only.

New executable host-side source validator:

  python3 tools/testing/selftests/smt311/rcu_lifetime_path_graph_g708.py

Its exact machine-readable evidence is in
Documentation/mm/sm-t311-rcu-lifetime-g708.json.
25/25 source, config, release and path-order predicates passed,
and 12/12 specifically mutated unsafe source inputs were detected.
This gate deliberately does NOT replay G659/G691 older LKMM or ABA
models and does NOT compile or install a new kernel.

The audit follows these source paths rather than isolated strings:

1. The experimental filemap_read_rcu_lookup() loads a RAW XArray
   pointer, reads PG_rcu_free, reads size, reloads the slot, and
   checks mapping identity, page order and Normal-zone eligibility.
   It does not call folio_try_get().
2. filemap_read_rcu_batch() enters rcu_read_lock before checking the
   enabled key and writable mmap count; pagefault_disable is inside
   RCU. Copy is bounded by FILEMAP_RCU_READ_MAX. The caller limits
   this optional path to ext4 and user-backed iterators.
3. filemap_mark_hot() requires an enabled static key, the expected
   address_space and suitable ext4/Normal folio. The experimental
   static key starts FALSE; the staged debugfs setter refuses
   nonzero activation with -EOPNOTSUPP.
4. page_cache_delete() and page_cache_delete_batch() preserve the
   RCU tag on experimental inode exit. __free_pages_prepare() tests
   that tag, clears it and defers recycling before normal free.
   UNTAGGED old folios are NOT unconditionally deferred.
5. rcu_free_start() uses queue_rcu_work; expedited reclamation waits
   synchronize_rcu_expedited. rcu_free_sync waits for RCU work
   completion as well as conventional workqueue completion.
6. mmap_region() increments writable mapping count, then waits for
   synchronize_rcu on the experimental ext4 maywrite path, before
   publishing the VMA.
7. The ordinary filemap_get_entry() path obtains folio_try_get(),
   validates the XArray slot and drops the ref on mismatch. With
   PAGE_RCU_FREE absent G700 filemap/shmem readers skip the
   unavailable no-ref call entirely.

Mutation controls deliberately remove the page flag test, XArray
reload, mapping identity, Normal zone, try-get fallback, protected
inode-exit condition, allocator defer, RCU callback queue, grace
period, RCU-work completion, mmap grace and fail-closed activation.
All 12 deviations were rejected by the validator.

What is STILL UNPROVEN:

* The RAW pointer may originate from an UNTAGGED old folio and be
  recycled immediately. The new TAGGED occupant can reuse the same
  physical address, mapping and index. This G659/G691 old/new PFN
  ABA problem remains. It is not solved by these rechecks.
* A new-generation grace period does not retroactively protect the
  old unmarked generation. No independent generation token or
  universally deferred lifetime has been implemented.
* ARMv7 HighMem/VIPT cache aliases, writer concurrency, LKMM
  end-to-end proof, and mmap/RCU lockdep progress are not signed off.
* G692 fail-closed protection exists only in STAGED source/build;
  the physical G650's runtime key value was not verified and must
  not be assumed to use the new interlock.

The preferred G700 release-OFF host image has the exact existing
8MiB BOOT SHA256
aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f.
It is NOT installed. Current physical p9 BOOT and p10 RECOVERY SHA
and reliable original TWRP return route remain unknown.
No backup, reboot, TWRP, flash, ringing/call/PIN, kernel rebuild
or Android executable modifications occurred in G708.

S0 PASS; S1 ACTIVE; S2-S6 BLOCKED; S7 PARTIAL; S8 ACTIVE.
Next G709: focus narrowly on an independently safe identity/lifetime
protocol for initially unmarked folios, evaluate memory pressure and
CPU overhead BEFORE considering implementation. Do not remove the
fail-closed interlock or turn no-ref RCU ON to test an unproven fix.
