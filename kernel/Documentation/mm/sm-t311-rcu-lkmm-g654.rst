SM-T311 G654: ARMv7 RCU/LKMM and zone guard audit
===================================================

CAUTION: source-analysis and prepared model files, NOT a proof of
reference-free filemap safety or a reason to enable RCU ON. The actual
physically installed BOOT is still G650 (sha256
2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3).

Board-specific findings:

- On 32-bit SMP Cortex-A9, include/linux/fs.h i_size_read() uses
  read_seqcount_begin / i_size / read_seqcount_retry. The 64-bit
  generic smp_load_acquire path is NOT active. A real memory model
  MUST include the 32-bit seqcount path and matching i_size writer.
- filemap_read_rcu_lookup() reads XArray slot, PG_rcu_free flag,
  smp_rmb, i_size_read, ARMv7 smp_rmb, xas_reload, smp_rmb,
  then validates order0 / ZONE_NORMAL / mapping identity.
  Pointer equality is not an allocation generation comparison;
  folio-lifetime proof still requires matching ALL writers and
  deferred frees against reader critical sections.
- Kiryl marks HOT folio while referenced with atomic bitops. A generic
  message-passing smp_wmb/smp_rmb litmus is merely a control, NOT an
  accurate model of this whole publication sequence.
- Local XArray documentation confirms reader traversals under RCU and
  write-side xa_lock; RCU guards XArray nodes, NOT necessarily
  concurrently recycled folio data.

Source models staged at tools/memory-model/litmus-tests:

  SMT311+PubWmbRmb+FlagData.litmus   generic positive barrier control
  SMT311+NoWmb+FlagData.litmus      deliberately weakened publisher
  SMT311+AddrABA+FlagSizeReload.litmus illustrative pointer-generation
                                     ABA risk; no real folio allocator

These files have STATIC layout/source preflight only. No herd7 binary or
opam was available on Debian; actual requested --run correctly exited
status 2. Therefore no real herd7 outcome has been observed and no
LKMM proof should be claimed. Kernel tools/memory-model/README requires
compatible external herdtools7 >= 7.58, run with linux-kernel.cfg.
A successful run still needs analysis against real XArray, seqcount,
allocator and PREEMPT_RCU lifetime: a small litmus alone is insufficient.

Physical Normal/HighMem reservation (read-only sample, RCU OFF):

  Normal free          81691 pages = 319.1 MiB
  Normal high watermark 1350 pages
  Additional reserve    1024 pages = 4 MiB
  Admission guard       2374 pages = 9.27 MiB
  Headroom over guard  79317 pages = 309.83 MiB
  Normal RCU pending        0 pages
  HighMem free            306 pages, high watermark 500 pages

Thus free Normal memory was NOT blocking the hypothetical resource
guard in this single observation. HighMem free below high watermark
does NOT establish exhaustion of reclaimable file cache or ref-free
read safety. Do not remove 4MiB reserve or 2MiB pending queue guards.
Reproducible source-only analysis is in
tools/testing/selftests/smt311/zone_guard_analysis.py.
Negative controls confirmed threshold=2374 and pending-RCU=1 would
block admission, whereas actual snapshot would pass resources.

Raw physical /proc/zoneinfo and derived JSON live in the project ledger:
G654_PHYSICAL_ZONEINFO_20261020.txt and
G654_SMT311_NORMAL_HIGHMEM_HEADROOM_20261020.json.
Source validator: tools/testing/selftests/smt311/lkmm_prepare.py,
preflight G654_LKMM_STATIC_PREFLIGHT_20261020.json,
blocked herd run G654_HERD_RUN_REQUEST_20261020.log.

PROJECT STATUS: S0 PASS; S1 ACTIVE; S2/S3/S4/S5/S6 BLOCKED;
S7 PARTIAL. DO NOT ACTIVATE RCU ON. No TWRP flash, no new backup,
no auto SIM PIN/PUK or audible tests during user sleep.

NEXT: real herdtools7 install/execution, translate actual 32-bit
seqcount writer and XArray spinlock/RCU events, prove folio last-free
and no dangerous pointer ABA, then revisit HighMem.
At EACH NEXT SM-T311 PROJECT RESPONSE reread plan/progress and
repeat DONE/BLOCKERS/NEXT/PHYSICAL BOOT, S0-S7 and the instruction
to repeat this status again in the following project response.
