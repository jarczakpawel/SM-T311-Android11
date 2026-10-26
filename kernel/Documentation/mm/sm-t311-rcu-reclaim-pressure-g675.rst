SM-T311 G675: PG_rcu_free lifetime callers and reclaim pressure
================================================================

Scope and result
----------------
This is a *host-side source audit* of the G674 staged inode-exit
lifetime fix and its pending/grace-period queue behavior.

The 22 source gates in
tools/testing/selftests/smt311/rcu_reclaim_pressure_g675.py pass,
and seven damaged-source variants are rejected when the same gates
are rerun. The code makes NO further runtime kernel modifications.
It does not establish full weak-memory correctness, nor test actual
RCU ON under memory pressure or an allocation race.

Known flag-clearing paths
-------------------------
A repository-wide search of C/H source for the exact folio clear and
test functions found the only explicit PG_rcu_free clear sites in:

* mm/filemap.c:page_cache_delete() and page_cache_delete_batch()
  (now gated to retain the bit on inode exit on SM-T311),
* mm/page_alloc.c:__free_pages_prepare() (the tagged folio enters
  rcu_free_defer() before its physical pages enter the allocator).

The macro definitions in include/linux/page-flags.h generate
accessors. Audit scope is specifically explicit calls and cannot
exclude every indirect flag alteration or all call paths.

Other scrutinized source paths:
* mm/migrate.c:folio_migrate_flags() does NOT migrate PG_rcu_free
  onto the new folio. The old folio remains protected by its tag,
  provided its final free goes through the deferred allocator path.
* mm/migrate.c:__folio_migrate_mapping() protects source migration
  with folio_ref_freeze() and handles the XArray replacement while
  holding its lock.
* mm/memory-failure.c uses synchronize_rcu() on relevant tagged
  hwpoison and memory_failure paths before sensitive reuse/unmap.
* fs/fuse/dev.c explicitly avoids stealing a pipe folio already
  carrying the RCU free tag.
* mm/truncate.c sets AS_EXITING ahead of final cache truncation;
  in G674, this transition does not cancel the tag on SM-T311.
These observations do NOT complete S1/S2: normal page flags and
XArray identity must still be validated against real PREEMPT_RCU ARMv7
memory ordering, all migrators, split and allocator transitions.

The distinction between soft mark limit and queue hard cap
----------------------------------------------------------
The two source macros in mm/internal.h are:

  SM_T311_RCU_PENDING_SOFT_LIMIT = 2 MiB = 512 4KiB folios
  SM_T311_RCU_FREE_RESERVE = 4 MiB = 1024 4KiB folios

The expedited candidate threshold in mm/page_alloc.c is 512KiB,
128 4KiB folios; expedited grace is scheduled only when the Normal
zone is also down to its high watermark plus reserve. The local
tagging check prevents setting the PG_rcu_free bit on NEWLY hot folios
if the global deferred count is too large.

It cannot revoke tags ALREADY attached to live, referenced folios.
For example, after 1536 previously-tagged folios across files are
removed close together, as much as 6MiB can be pending until grace
completes, even though the mark soft limit is only 2MiB. That is a
bounded *hypothetical scenario*, not a real measured queue overflow
or proof that all 1536 frees land in the same batch. Queueing can
be spread over per-CPU pending, batch and in-flight work.

rcu_free_start() allows only one grace callback in flight per CPU;
rcu_free_defer() accounts pending/batch and queue-driven expedite.
Under allocator shortage, reclaim logic can drain waiting folios
via rcu_free_drain()/rcu_free_sync(). Their presence helps forward
progress but is not a guarantee that reclaim latency, UI p99,
cgroup pressure or OOM risk are unchanged.

Physical, read-only memory snapshot
-----------------------------------
The G675 passively read SM-T311 /proc/zoneinfo and /proc/meminfo
(with no synthetic load, no RCU switch activation):

  Normal free 76850 4KiB pages ~= 300.20 MiB
  Normal low / high watermarks 1125 / 1350 pages
  Experimental pressure trigger <= 1350 + 1024 = 2374 pages
  HighMem free 294 pages, low 314, high 500
  nr_free_rcu = 0, pgfree_rcu* counters = 0

These values are momentary observations with G650 RCU no-ref
default OFF. HighMem free falling below low is one snapshot,
NOT evidence that memory pressure can be blamed on the staged G674
patch (which is not installed). The observed Normal headroom says
nothing about the worst case of bulk inode eviction after RCU ON.

Decision and next work
----------------------
* G674 inode-exit tag retention remains a STAGED source correction.
* No new kernel image has been built, linked, signed, or flashed for
  G674/G675; physical BOOT is still stable G650.
* S1 ACTIVE, S2/S3 BLOCKED; S4-S6 BLOCKED; S7 PARTIAL, S8 ACTIVE.
* Next: separately investigate allocator/RCU in-flight pending
  accounting, other generational ABA and worst-case memory pressure
  on a safe host model. Do not run RCU ON on the only working tablet.
* Audio, camera, modem and voice tests remain deferred. No backups.

Reproduce source check:

    python3 tools/testing/selftests/smt311/rcu_reclaim_pressure_g675.py

Raw results:
Documentation/mm/sm-t311-rcu-reclaim-pressure-g675.json.
