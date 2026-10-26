SM-T311 G682: concurrent mmap creators cannot skip RCU grace
==============================================================

Question
--------
Could G680's synchronize_rcu() while holding mmap_lock be optimized
to run only when the writable mmap count transitions from zero to one?

**Not with the current protocol.** The writable count is associated
with the FILE'S mapping, but mmap_lock belongs to each PROCESS mm.
Two processes can concurrently create shared writable mappings of the
same file. The first increments the mapping count to positive and
then waits in synchronize_rcu(), with its VMA still unpublished.
The second process can observe the first process's positive count,
skip its own GP based on that count, and publish its VMA before the
first process finishes its GP. An earlier ref-free reader which saw
zero could still be copying while the second VMA becomes accessible.

The positive count is therefore a reservation, NOT proof that any
writer has already finished its pre-publication grace period.
A hypothetical atomic "old count was zero" optimization is likewise
insufficient unless additional in-flight reservation state and a
properly ordered synchronization protocol are introduced.

Reproduce actual Linux Kernel Memory Model validation
------------------------------------------------------
The following three C tests use REAL herdtools7 and the kernel's
tools/memory-model/linux-kernel.cfg:

* SMT311+MmapTwoWritersSkipGraceG682.litmus
* SMT311+MmapTwoWritersEachGraceG682.litmus
* SMT311+MmapTwoWritersOnlyBarrierG682.litmus

Run::

  python3 tools/testing/selftests/smt311/rcu_two_mmap_writers_g682.py

Results:

+-------------------------------------------+------------+----------+----------+
| Two independent writers                   | Outcome    | Positive | Negative |
+===========================================+============+==========+==========+
| Second writer skips GP on positive count  | Sometimes  |        1 |        5 |
+-------------------------------------------+------------+----------+----------+
| Both writers wait GP before VMA publish   | Never      |        0 |        5 |
+-------------------------------------------+------------+----------+----------+
| Second writer substitutes smp_mb for GP   | Sometimes  |        1 |        5 |
+-------------------------------------------+------------+----------+----------+

The diagnostic is an abstract LKMM count / VMA publication model,
NOT a reproduction of a real corruption on the tablet. An allowed
condition is a counterexample to the proposed shortcut, not a claim
that current G680 is broken. The bounded absence of the condition
with two GPs is not a full lockdep, VM or ARMv7-cache correctness proof.

Source gates
------------
13/13 source checks PASS, including:

* mmap_region requires the current mm's mmap_lock for writing;
* file-level mapping_map_writable atomically increments a GLOBAL
  address_space count, and G680 waits for GP independent of whether
  the old writable mapping count was zero or positive;
* GP is before __mmap_region publication, count is unwound after;
* no-ref filemap reader enters RCU before checking the count,
  uses pagefault_disable, does not take mmap_lock;
* AS_RCU_READ is set after marking a hot folio and checked by the
  fastpath; fork duplicates existing VMAs.

6/6 actual mutated C source variants were rejected. In particular
a deliberately wrong new mapping_writably_mapped() count guard,
omitting GP, replacing it with a barrier, moving the read-side check
outside RCU, removing count publication, and dropping the SM-T311
eligibility bit are rejected. Inputs, normalized herd outputs and
checks are in sm-t311-rcu-two-mmap-writers-g682.json.

Decision and open risks
-----------------------
G682 makes NO executable kernel changes. Retain G680's conservative
per-new-map GP candidate while S2/S3 are BLOCKED. In particular do
not implement a first-mapper-only GP shortcut.

Global RCU/mmap_lock dependency analysis and the latency of waiting
in mmap_region with mmap_lock held remain unproven. A future safe
design may require additional in-flight reservation state or a
different publication protocol, but neither is implemented here.

Physical kernel G650 (7.3.0-pjarczak) unchanged; no-ref RCU remains
default OFF. Three executable source differences are STAGED NOT
INSTALLED: mm/filemap.c (G674/G680), mm/page_alloc.c (G676),
mm/vma.c (G680). No experimental BOOT or flash, backup, audio,
camera, voice call, PIN or USB OTG action.

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED, S5 BLOCKED,
S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.


G683 supersession: G682 proves that a positive counter is not a
shortcut for omitting grace when two writers reserve concurrently.
G683 additionally identified a distinct marker AS_RCU_READ-bit vs
writer-counter publication race. The current staged writer waits GP
for all EXT4 shared writable mappings, independently of both
counter previous state AND AS_RCU_READ previous state.
