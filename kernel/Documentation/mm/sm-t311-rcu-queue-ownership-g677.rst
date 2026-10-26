SM-T311 G677 — two-CPU deferred page-free queue ownership and GP
==================================================================

Purpose
-------
Following G674 (cross-inode PG_rcu_free lifetime) and G676 (wait via
flush_rcu_work()), audit the interaction of rcu_free_defer(),
rcu_free_start(), rcu_free_workfn(), expedite and rcu_free_drain()
when both Cortex-A9 CPUs retire tagged page-cache folios.

This is an executable *small abstract state-machine* for ownership of
four previously tagged folios across two per-CPU queues. It is not a
LKMM model, kernel scheduler run or stress test on the physical device.

Real source path binding
------------------------
The model's transitions are derived from the spinlocked list operations
in mm/page_alloc.c and queue_rcu_work() contract in kernel/workqueue.c:

* a deferred folio is accounted in NR_FREE_RCU_PAGES before being
  appended to the per-CPU pending list under r->lock;
* if no callback is queued, pending is moved into batch and
  queue_rcu_work() registers the callback;
* its callback waits for a grace period before workfn consumes batch;
* worker takes batch under r->lock, marks queued=false, optionally
  starts the next batch, then frees its detached list;
* expedite takes pending and batch under the same lock, then waits
  for synchronize_rcu_expedited before freeing;
* drain takes lists from all possible CPUs and likewise must wait
  for RCU grace before returning their folios to the allocator;
* rcu_free_sync(), corrected in G676, now waits with
  flush_rcu_work() rather than plain flush_work().

Accounting invariant: no bit representing a folio may be held by two
owners or disappear. Owned locations are per-CPU pending and batch,
per-CPU expedite-detached, drain-detached or freed; their union equals
all issued folios and their intersections are empty.

Model method and result
-----------------------
Run::

  python3 tools/testing/selftests/smt311/rcu_queue_interleavings_g677.py

The BFS explores all reachable abstract states under four fixed folio
IDs, divided between two CPUs. Results:

* 10,886 distinct reachable states;
* 27 fully-freed reachable terminal-like states (callbacks may still
  have empty work in flight);
* maximum four simultaneously outstanding folios;
* zero duplicate/lost ownership assertions;
* 13/13 source binding checks PASS;
* 4/4 deliberately altered actual C source expressions rejected;
* 3/3 deliberate omissions of RCU grace before worker, expedite or
  drain free produced a nonempty unsafe-free witness and event trace.

A GP mutant is classified unsafe only when it actually releases a
NONEMPTY detached batch without the mandatory grace period. This
avoids false failures for a harmless no-op callback with zero folios.

What the test does NOT prove
----------------------------
The SC abstraction treats spinlocked operations and callback stages
as atomic, and grants the RCU GP event on demand. It does not model:

* weak-order ARMv7 load/store semantics, pointer generations, kmap,
  cache aliasing, HighMem, or the speculative no-ref filemap reader;
* all kernel RCU/wq scheduling cases or exact callback pending bits;
* memory pressure, OOM, starvation or unbounded backlog above four
  folios; concurrent new frees after a drain snapshot;
* a physically reproduced UAF, crash, deadlock or speedup.

The model found NO additional source-accurate runtime correction
beyond the prior G674 and G676 changes. Do not add speculative locks
or barriers to the fastpath without a proven need.

Physical deployment
-------------------
BOOT G650 remains installed on SM-T311. Source differences G674
mm/filemap.c and G676 mm/page_alloc.c are STAGED NOT INSTALLED.
RCU no-ref stays DEFAULT OFF. No flash, backup, camera, audio or
telephone test was performed as part of G677. The real device must
not be used as an unsafe concurrency stress target.

Status: S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
