SM-T311 G659: lockless XArray folio-generation ABA - diagnostic
================================================================

Physical baseline
-----------------
The production Samsung SM-T311 BOOT remains G650, last verified SHA256
2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3.
The kernel production code in mm, fs, kernel, drivers, arch, include and init
remains identical to G650 commit a2fca4558. G659 changes models, an offline
runner, source notes and project status only. No BOOT or Android flash.

Source review
-------------
In mm/filemap.c filemap_read_rcu_lookup(), the speculative reader uses
xas_load() under rcu_read_lock() rather than under xa_lock. It checks
PG_rcu_free, then i_size_read() (seqcount on 32-bit SMP), reloads the
XArray slot with xas_reload(), and checks folio->mapping. These are
required checks, but a raw equality comparison is not an allocator
generation comparison. include/linux/xarray.h xas_reload() returns the
current slot entry. The writer replace_page_cache_folio() uses
xas_lock_irq() for its slot store, which does not by itself serialize a
reader that never acquires xa_lock.

mm/page_alloc.c defers last-free for tagged folios using rcu_free_defer()
and queue_rcu_work()/synchronize_rcu_expedited(). The separate case in
which the first folio is untagged and the same address is reused needs
a full lifetime and publication proof before this experimental fast path
may run. Clearing a tag on mapping_exiting() during inode eviction, page
migration, split and hwpoison are also pending manual end-to-end review.

Four new diagnostic LKMM models
-------------------------------
Real herdtools7 7.58 with the in-tree linux-kernel.cfg, linux-kernel.cat
and other in-tree model resources produced:

- SMT311+XALockUnmarkedABA: Sometimes 2 / 49
- SMT311+XALockGraceABA: Sometimes 1 / 27
- SMT311+XALockOldNewGenNoGrace: Sometimes 2 / 76
- SMT311+XALockOldNewGenGrace: Sometimes 1 / 41

The denominator after "/" denotes negative witnesses, not total states.
The first pair models an abstract XArray slot plus writer lock. The
second pair additionally reads the old and then new generation using
a test-only generation variable. Their conditions can observe mixed
epochs. In particular, an allowed outcome in the synthetic grace-period
model does NOT demonstrate a failure of synchronize_rcu() in Linux.
The numeric pointer and generation variables cannot represent actual
allocator object lifetime, RCU pointer provenance or nested XArray node
reclamation faithfully. This is a useful unresolved modeling ambiguity,
not a real-kernel UAF result or a safe-ON approval.

The earlier G658 RCUGraceLastFree Never 0 / 2 and deliberately missing
grace Sometimes 1 / 2 remain valid for their *distinct*, simpler
RCU old-pointer abstraction; G659 does not replace or refute them.

Run only the new G659 models with
tools/testing/selftests/smt311/rcu_aba_g659.py, which records their exact
pinned observations and source anchors in sm-t311-rcu-aba-g659.json.

Next work
---------
S1 remains ACTIVE: build a verified reader-writer timeline of
page_cache_delete, replace, migration, truncate, eviction and allocator
last-put, including the exact pointer provenance. S2 is BLOCKED: an
object-identity-aware model must prove whether any plausible stale read
passes the ref-free eligibility check and can observe freed or unrelated
data. S3 HighMem lifetime/cache alias is BLOCKED. S4 no-ref RCU ON, S5
ABBA, S6 release selection are BLOCKED; S0 baseline PASS and S7
release/GitHub PARTIAL. Preserve G650 and keep no-ref RCU OFF.
