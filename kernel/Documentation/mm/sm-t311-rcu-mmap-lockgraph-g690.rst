SM-T311 G690: mmap_lock / synchronize_rcu wait-for edge
======================================================

Observed code and specific concern
----------------------------------
The optional SM-T311 ref-free experiment inserts synchronize_rcu()
in mm/vma.c:mmap_region() for ext4 VM_SHARED|MAYWRITE mappings
after incrementing the mapping's writable count and *while the
calling mm_struct's mmap_lock is write-held*.

CONFIG_PER_VMA_LOCK=y is compiled into the actual SM-T311
experimental ARMv7 host profile. A candidate global wait-for cycle
would require:

  Writer W: mmap_write_lock(mm) -> synchronize_rcu()
  Reader R: rcu_read_lock() -> blocking mmap_read_lock(mm)

If the reader holds RCU until after the lock is granted, W waits
for R's quiescence while R waits for W's mmap lock: an ABBA deadlock.

Verification of actual fallback contracts
------------------------------------------
Source inspection in mm/mmap_lock.c establishes that the
read-side helper vma_start_read() **releases RCU on every error
path**, explicitly documented in its own C comment. It releases
it at err: and at err_unstable: before expensive mmdrop processing.

Consequently, mm/mmap_lock.c:lock_next_vma() behaves as follows:

* On direct goto fallback after failed vma_start_read(), that
  callee has ALREADY called rcu_read_unlock().
* On fallback_unlock after a successfully acquired VMA lock
  which must be retried, lock_next_vma() explicitly calls
  rcu_read_unlock() before vma_end_read and before the
  sleepable lock_next_vma_under_mmap_lock().
* That helper acquires mmap_read_lock_killable(mm), then releases
  it. Only AFTER return does lock_next_vma() reacquire RCU.
* /proc VMA iterator fallback_to_mmap_lock() explicitly drops RCU
  before blocking on mmap_read_lock(mm). /proc maps query first
  closes its RCU section, then falls back to mmap_read_lock(mm).

The direct goto fallback line would be misleading if examined in
isolation; vma_start_read's error-exit contract is essential.
Deleting that RCU release, swapping the lock order, or moving the
next rcu_read_lock() before the sleepable lock would invalidate
the safety argument.

Source gates and finite interleaving test
-----------------------------------------
tools/testing/selftests/smt311/rcu_mmap_lockgraph_g690.py checks
19 actual C source/config predicates including:
mmap write lock assertion, EXT4 GP placement, VMA helper error
contract, explicit release on unstable error, direct fallback,
fallback_unlock, reacquisition after mmap_read_lock, /proc fallbacks,
and no direct mmap lock acquisition inside filemap RCU copy.

Nine true modified-source mutants must all be detected. A small
explicit-state concurrent scheduler models one writer and one
/proc-style reader:

  Correct contract: reader releases RCU BEFORE waiting on mmap_lock.
  Result: 26 reachable abstract states, no deadlock, completes.

  Counterfactual: reader blocks on mmap_lock WHILE still inside RCU.
  Result: 17 reachable abstract states, reachable deadlock.
  Witness: writer has mmap write lock and waits on GP, reader has
  entered RCU and waits for the mmap read lock.

The simulator conservatively requires every reader to leave its RCU
section before a GP can finish; the real GP only waits for
pre-existing readers and has more detailed scheduling and barrier
semantics. This witness is a valid *potential inversion pattern*,
not a reproduced lockdep trace or demonstration of current device
deadlock. It does not model refcounted VMA lifetime, repeated
-EAGAIN, task cancellation, unrelated RCU callbacks or all
subsystem lock dependencies.

The exact input predicates, rejected mutants, abstract witness and
limitations are machine-recorded in
Documentation/mm/sm-t311-rcu-mmap-lockgraph-g690.json.

Boundaries, unresolved constraints and decisions
------------------------------------------------
This audits one important candidate cycle; it does NOT establish
that every RCU reader in Linux obeys this ordering, nor that
mmap_lock -> synchronize_rcu is globally safe. Production debug
lockdep is not currently enabled (CONFIG_PROVE_LOCKING=n), and
no physical lockdep or Cortex-A9 p99 mmap latency run occurred.

The G683 GP for every ext4 VM_SHARED|MAYWRITE mmap remains in
the experimental build even when its runtime static key is OFF.
A global admission rwsem or GP omission is NOT installed; previous
G680/G682/G683/G684/G685 models show naive GP bypass unsafe.
PFN lifetime/ABA, ARMv7 VIPT/HighMem alias and reclaim remain
unresolved S2/S3 blockers.

No executable source changed in G690. Exactly four executable
source files remain STAGED NOT INSTALLED versus physical G650
(a2fca4558): mm/filemap.c, mm/page_alloc.c, mm/shmem.c,
mm/vma.c. The optional test-only Kconfig fragment is separate.
No kernel/Android flash, TWRP or backup was performed.

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
