SM-T311 G678 — cross-inode ABA and RCU grace in actual LKMM/herd7
=================================================================

Aim
---
Continue the Linus/Kiryl no-ref filemap reader review, moving from the
G674/G677 sequentially-consistent toy explorers to real herdtools7
using the kernel's memory model.

The four C litmus files in tools/memory-model/litmus-tests are:

- SMT311+CrossInodeRecycledBeforeGraceG678.litmus
- SMT311+CrossInodeRecycledAfterGraceG678.litmus
- SMT311+CrossInodeBarrierNotGraceG678.litmus
- SMT311+CrossInodeReaderNoRCUG678.litmus

Each models a reader of inode M holding numeric XArray slot address P
and the writer's A (unmarked/M), B (hot/tagged/N), C (unmarked/M)
generations reusing the *same* pointer address. The extra scalar
"generation" is a testing oracle and is NOT present in the real Linux
folio or XArray fastpath.

The writer models removing A from the slot under xa_lock, allocating B
for another inode, setting B's hot tag and clearing it during final
put, then reallocating C at the same physical address/index. The
reader checks the tag, barriers, unchanged file size and reloaded slot
as in mm/filemap.c, and the mapped inode identity; the test-only
generation oracle reveals a mixed-epoch observation. Note that the
actual reader has no generation comparison; moreover the abstract
variables do not represent real PFN reuse or allocator object
publication. The LKMM exercise is diagnostic, NOT proof of physical
UAF or complete correctness.

Four actual observations on herd7 v7.58
---------------------------------------
Run::

  python3 tools/testing/selftests/smt311/rcu_cross_inode_lkmm_g678.py

Runner uses::

  herd7 -I tools/memory-model \
    -conf tools/memory-model/linux-kernel.cfg \
    tools/memory-model/litmus-tests/<NAME>.litmus

The test invokes real herd7 and verifies the actual Observation,
Positive and Negative witness counts; it does not merely inspect the
litmus syntax or infer the answer from Python's own model.

+--------------------------------+--------------------+----------+----------+
| Litmus variant                 | Outcome            | Positive | Negative |
+================================+====================+==========+==========+
| Recycle B before grace         | Sometimes          |        6 |      318 |
+--------------------------------+--------------------+----------+----------+
| Recycle B after grace          | Never              |        0 |       71 |
+--------------------------------+--------------------+----------+----------+
| Replace grace with smp_mb()    | Sometimes          |        4 |      233 |
+--------------------------------+--------------------+----------+----------+
| Remove reader rcu_read_lock()  | Sometimes          |        4 |      233 |
+--------------------------------+--------------------+----------+----------+

Source-anchored validations: 12/12 PASS for rcu_read_lock, XArray
read/reload, flag-before-reload ordering, two barriers, inode mapping
check, both G674 inode-final clear guards and page_alloc's tagged
deferred free, and actual queue_rcu_work grace implementation.
herd7 output tails, input SHA256, normalized stdout SHA256 and
observations are retained in Documentation/mm/sm-t311-rcu-cross-inode-lkmm-g678.json.
Only the volatile wall-clock ``Time`` footer is removed from the
stored/hashable output, so rerunning an identical litmus does not dirty
Git. Outcome, witness counts, model hashes and all other output remain.

The substantive distinction is that a full memory barrier cannot
replace a grace period: smp_mb orders memory accesses but does NOT wait
for old RCU readers to finish using a recycled address. The reader's
RCU critical section is an equally important precondition.

Remaining gaps / deployment
---------------------------
* The model has a deliberately abstract numeric pointer and test-only
  generation; does not encode actual XArray nodes, per-CPU allocator,
  physical free/alloc latency, PREEMPT_RCU callback scheduling or all
  writer/interleaving paths.
* A herd7 Never observation under the model's additional oracle reads
  does not by itself prove the real no-ref filemap path UAF-free.
* LKMM is a portable Linux concurrency specification, NOT the full
  ARMv7 Cortex-A9 D-cache/VIPT/HighMem, TLB, microarchitecture and
  perf/temperature model.
* The G674 fix prevents an investigated cross-inode final-eviction
  early-tag-clear bypass; G676 fixes RCU work completion. Other
  unmarked-folio ABA paths, migration and memory-pressure costs are
  not resolved. S1 ACTIVE, S2/S3 BLOCKED.
* No additional executable kernel patch was justified by G678.
  Host Git executable source differs from the deployed G650 only in
  the previously recorded G674 mm/filemap.c and G676 mm/page_alloc.c.
  Both remain STAGED NOT INSTALLED. Do NOT flash or enable no-ref RCU ON
  without additional proof and safe fallback; no backups or nighttime
  audio/camera/voice tests.

Status S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED,
S4 BLOCKED, S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
