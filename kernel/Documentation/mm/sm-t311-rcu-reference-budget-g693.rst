SM-T311 G693: safe referenced XArray protocol and Normal-zone memory budget
===============================================================================

Motivation and actual source comparison
---------------------------------------
The optional SM-T311 no-reference RCU page-cache reader remains disabled by
the fail-closed G692 debugfs interlock because G659/G691 documented the
UNTAGGED old folio -> reused same physical struct page -> TAGGED new
folio ambiguity. A raw pointer, PG_rcu_free and xas_reload pointer equality
have no independently pinned generation.

The kernel's existing reference-counted `filemap_get_entry()` already
implements a usable protocol and should remain the production choice:

  RCU:  xas_load(&xas) returns a speculative struct folio pointer
  PIN:  folio_try_get(folio) tries atomic increment unless refcount zero
  VALIDATE: if folio != xas_reload(&xas), put ref and retry
  USE:  pagecache caller receives a *referenced* folio
  PUT:  caller eventually releases its reference

The pair of operations is important. Merely adding a generation counter
stored *inside* `struct page` and rereading it would not pin the counter's
identity if the old page struct has already been returned to buddy and
reused; an XArray slot generation would have to be coupled to its
publication, and validated without first dereferencing an unpinned page.
Similarly, acquire-a-reference without XArray recheck could return a folio
that is no longer associated with the intended mapping/index.

G693 added NO new kernel read loop: the referenced read fallback already
exists and is tested. Adding a duplicate implementation without an
invariant/performance gain would raise the surface for bugs, and
a reference-acquiring reader cannot claim the no-ref scalability win.

Source-anchored bounded interleaving
------------------------------------
The executable host-only test is
`tools/testing/selftests/smt311/rcu_reference_budget_g693.py`.

It asserts 16 actual source conditions in mm/filemap.c, mm/page_alloc.c,
include/linux/xarray.h, mm/internal.h and include/linux/page_ref.h, and
rejects ten deliberate alterations to these actual C source fragments,
including deletion of folio_try_get, omission of the slot recheck, omission
of folio_put on mismatch and removal of the G692 activation interlock.

A deliberately small two-thread sequentially consistent state explorer
represents (a) an XArray reader sampling pointer P when the page belongs
to old generation A and (b) a writer removing A, dropping the pagecache
reference, recycling the SAME physical P to generation B and reinserting
B into the SAME slot, then marking B.

  Unpinned reader: 39 abstract states, 2 invalid-recycled-state visits.
  Referenced reader: 70 abstract states, 0 invalid-recycled-state visits.

The referenced model also observes a *benign* new-generation case:
if the reader successfully takes a reference to B after P has been
recycled, then xas_reload sees B at the right slot and B is held by
the reader, copying B is not an unpinned UAF. A generation change is
not by itself memory corruption.

The finite-state model is simplified: it does not model PREEMPT_RCU
scheduling, complete ARMv7 weak memory order, all XArray mutation
paths, multipage folios, HighMem VIPT aliasing, or a production stress
test. It must not be represented as universal correctness proof or a
reproduced defect on physical SM-T311. G659's actual four herd7
synthetic "Sometimes" outcomes and G691's model remain relevant.

Physical Normal and HighMem snapshot (NO load test)
---------------------------------------------------
One passive ADB read of actual physical G650 kernel /proc/meminfo,
/proc/zoneinfo and /proc/vmstat was saved as
`Documentation/mm/sm-t311-rcu-zone-snapshot-g693.json`. This is a
single point in time, not a steady-state distribution.

  Total RAM from /proc/meminfo:   1,483,332 KiB
  Normal managed:                  203,153 pages = 793.57 MiB
  Normal free:                      74,692 pages = 291.77 MiB
  Normal high watermark:            1,350 pages = 5.27 MiB
  HighMem managed:                 167,680 pages
  HighMem free:                        308 pages = 1.20 MiB
  HighMem high watermark:              500 pages = 1.95 MiB
  nr_free_rcu:                             0 (runtime key OFF)
  nr_file_pages:                      160,524 (global, not zone-local)

G692 retained experimentally tracked limits in mm/internal.h:

  SM_T311_RCU_PENDING_SOFT_LIMIT = SZ_2M / PAGE_SIZE = 512 pages
  SM_T311_RCU_FREE_RESERVE       = SZ_4M / PAGE_SIZE = 1024 pages

These are software heuristics, not an absolute cap on queued pages.
The current Normal free capacity above the high watermark + extra
reserve is 74,692 - 1,350 - 1,024 = 72,318 pages (282.49 MiB),
with rapid changes possible under reclaim pressure. HighMem free
was already below the HighMem high watermark, a real reason not to
infer global healthy memory just from Normal free.

Hypothetical share of Normal managed pages awaiting a new RCU GP:

  1% =  2,032 pages =   7.94 MiB ( 2.7% of observed Normal free)
  5% = 10,158 pages =  39.68 MiB (13.6% of observed Normal free)
 10% = 20,315 pages =  79.36 MiB (27.2% of observed Normal free)
 25% = 50,788 pages = 198.39 MiB (68.0% of observed Normal free)

These are not experimentally observed RCU waiting pages; they are
illustrative algebraic costs calculated from the current managed zone
and compared with one free-page snapshot. The true number of hot EXT4
order-0 Normal folios eligible for tagging is unknown. It would be
incorrect to infer the same proportions from global nr_file_pages.

Decision
--------
Keep the G692 debugfs ON-fail-closed guard. Do not turn ON an
unproven reader or make all Normal-zone filecache frees wait on
`synchronize_rcu` without correct lifetime proof and live resource
measurements. The safe production default is the existing refcounted
pagecache path, not a speculative duplicate refcount implementation.

No new executable kernel code is introduced in G693. Exactly four
runtime source files remain STAGED NOT INSTALLED versus the physically
deployed G650 source (`mm/filemap.c`, `mm/page_alloc.c`, `mm/shmem.c`,
`mm/vma.c`), plus the separate optional build-time config fragment.
No Samsung BOOT assembled, no flash/TWRP, no backup or disturbance of
night-time calls/audio/camera/PIN/OTG. Owner publishes GitHub later.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED;
S5 BLOCKED; S6 BLOCKED; S7 PARTIAL; S8 ACTIVE.
