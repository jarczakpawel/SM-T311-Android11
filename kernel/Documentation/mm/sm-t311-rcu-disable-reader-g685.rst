SM-T311 G685: recheck the runtime static key inside RCU read lock
===================================================================

Problem in proposed cheap-OFF activation designs
------------------------------------------------
The SM-T311 no-ref filemap reader initially checks lockless_read_key
in filemap_read_rcu(), before entering an RCU critical section in
filemap_read_rcu_batch(). The task can be preempted between these
two operations.

Consequently, simply disabling the static key and waiting for a
grace period would NOT necessarily drain all readers that previously
observed ON. A task which loaded ON before the disable can enter RCU
AFTER the grace period, then perform speculative page-cache access
after a subsequent OFF-mode writer has omitted its own grace period.

G685 STAGED defensive source correction
---------------------------------------
Inside mm/filemap.c:filemap_read_rcu_batch(), immediately after
rcu_read_lock() and before both the writable-mmap check and any
reference-free folio lookup, under the SM-T311 experiment Kconfig:

    if (unlikely(!lockless_read_enabled())) {
        rcu_read_unlock();
        return false;
    }

This preserves proper cleanup before falling back to the regular
referenced filemap reader and does not add an explicit RCU GP to
the fastpath. It costs an additional static key/count read per
32 KiB RCU read batch when the experiment is ON.

The initial caller-level test is retained as an early rejection
for the common OFF case, so ordinary reads do not enter unnecessary
RCU critical sections while OFF.

This is defense in depth for a possible future activation/drain
protocol; it does not by itself prove a safe writer GP bypass.

Real herd7 LKMM diagnostic
--------------------------
Two C litmus models, actual herd7 with the Linux-Kernel memory model,
have the following observations:

+--------------------------------------+-----------+----------+----------+
| Shutdown/read-side model             | Outcome   | Positive | Negative |
+======================================+===========+==========+==========+
| Caller checks ON outside reader RCU  | Sometimes |        1 |        4 |
+--------------------------------------+-----------+----------+----------+
| Reader rechecks ON inside its RCU    | Never     |        0 |        6 |
+--------------------------------------+-----------+----------+----------+

In the model a disabler stores OFF and waits for an RCU grace
period, after which an OFF-mode writer makes its VMA visible. The
unprotected reader which saw ON earlier can enter too late. A new
key check under the reader's RCU lock excludes this specific
condition.

The ACTUAL current debugfs switch-off implementation does NOT
perform synchronize_rcu() on disable, and the actual current G683
mmap writer still waits for a GP on every new EXT4 shared writable
VMA. Thus the test illustrates an additional ordering requirement
for any proposed OFF shortcut; it is NOT proof that existing G683
physically causes corruption, and it does NOT authorize removing
G683's per-map GP.

The models use scalar active/published state, not real static-key
refcounts, all VMA publication APIs, PFN generations or ARMv7 cache.
The Never result is not a complete LKMM/correctness proof.

Tests and actual ARM32 compilation
----------------------------------
Run::

  python3 tools/testing/selftests/smt311/rcu_disable_reader_g685.py

The source gate passes 16/16 actual C/config predicates, rejects 8/8
deliberately modified real C source inputs, executes both actual
herd7 models and hashes inputs/normalized outputs. Results retained
in sm-t311-rcu-disable-reader-g685.json.

The current mm/filemap.o compiled successfully as ARM EABI5 under
../build/g633-armv7-rcu-zone-guard/.config, with
CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y,
CONFIG_PAGE_RCU_FREE=y and CONFIG_DEBUG_FS=y.
Compiled file SHA256:

  0c97df92e8c15d5163ea91e7c54ad0ac20c7b71297c6a5b31f440dece0cdf9c1

The old scripts G679-G684 were updated to validate the changed
placement and unwind ordering instead of producing misleading failures.
Three executable source paths remain staged versus physical G650:
mm/filemap.c, mm/page_alloc.c, mm/vma.c. No kernel BOOT image built or
flashed and no experimental runtime ON test was attempted.

Remaining safety/performance blockers
-------------------------------------
* A new globally coordinated writer admission/activation drain
  protocol would be required to skip GP in OFF mode. It would need
  to cover writers in different mm_structs, VMA publish and failures,
  *both* ON and OFF transitions, and lockdep ordering.
* Additional read-side key recheck does not globally linearize
  static-key toggles with all in-flight mappings; no complete ARMv7
  proof is provided. The source-level narrow fix is STAGED only.
* GP cost while mmap_lock is held in G683 persists even with key OFF,
  and is not measured. The global RCU lock graph is not validated.
* Folio PFN lifetime/ABA, writable-mmap cache alias, VIPT/HighMem,
  reclaim and real Cortex-A9 performance remain open.

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
