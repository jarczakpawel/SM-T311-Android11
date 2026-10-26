SM-T311 G658: actually executed LKMM RCU grace and missing-grace controls
==========================================================================

Target: Samsung SM-T311 lt013g, 32-bit ARMv7 SMP Cortex-A9 HighMem.
Production reference-free pagecache reader remains OFF. Physical BOOT is
still G650, source runtime from a2fca4558; source-only G658 work does NOT
change mm/fs/drivers/arch or require any TWRP flash.

Seven actual Linux kernel memory-model litmus test observations verified
with herdtools7 7.58, -I tools/memory-model, -conf linux-kernel.cfg:

- SMT311+PubWmbRmb+FlagData: Never, 0/3.
- SMT311+NoWmb+FlagData: Sometimes, 1/3.
- SMT311+AddrABA+FlagSizeReload: Sometimes, 6/66.
- SMT311+IsizeSeqcount+32Smp: Never, 0/18.
- SMT311+IsizeNoSeqRetry+32Smp: Sometimes, 3/9.
- SMT311+RCUGraceLastFree: Never, 0/2.
- SMT311+NoRCUGraceLastFree: Sometimes, 1/2.

The two new RCU tests use actual synchronize_rcu(), RCU read-side
critical sections and typed pointer dereference as recommended by
the upstream Linux memory model examples (RCU+sync+free). With a grace
period between removing a pointer and reusing its old storage, a reader
that still loaded that pointer must not see reused content (Never).
Deliberately OMITTING the grace period makes that outcome possible
(Sometimes). This is a true LKMM result for this ABSTRACT test, not
proof that all no-ref folio frees in the real kernel are safe.

Source-level paired coverage (not formal callgraph proof):
  - mm/page_alloc.c rcu_free_start queues RCU work only after pending
    tagged folios have been moved to the batch;
  - rcu_free_workfn uses to_rcu_work() and frees its completed batch;
  - rcu_free_flush() uses synchronize_rcu_expedited() BEFORE recycling
    a pending/deferred folio in expedite/drain paths;
  - rcu_free_sync drains and flushes work, and the tag-aware allocator
    returns false rather than immediately recycling the folio.
The script tools/testing/selftests/smt311/rcu_grace_source_gate.py
verifies 15 source properties and 3 deliberately altered negative
controls. These controls catch accidental regressions in the defer
mechanism; they do NOT close all unsound possible writer/reader
interleavings or prove eventual grace completion under every emergency
path.

Outstanding safety obligations before refcountless RCU ON:
  1. Unmarked source folio freed/reused at same address just before
     a racing speculative reader's mark test; actual XArray generation
     ABA and contents, not just illustrative generation counters.
  2. Atomic PG_rcu_free publisher ordering and XArray xa_lock writer
     synchronization, specifically ARMv7 32-bit SMP i_size seqcount.
  3. Migration, split, collapse, invalidate and inode exit clearing
     of PG_rcu_free during a reader; hwpoison special free.
  4. HighMem kmap alias/caching/TLB lifetime and pressure/power.
  5. Real-world SM-T311 app tail latencies and ON/OFF matching AFTER
     safety is established.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED; S5 BLOCKED;
S6 BLOCKED; S7 PARTIAL. These stages MUST NOT be silently promoted.
Reference-free RCU defaults OFF on physical BOOT G650. Never treat
a saved source-tree HEAD as a newly flashed BOOT.

User's mandatory progress protocol for every subsequent SM-T311
project report: DONE / BLOCKED / NEXT / PHYSICAL TABLET and all S0-S7.
End each report with instruction to repeat the same next time.

Full raw herd7, derived JSON, physical checks and one-step followup are
kept in Debian linux-7.3-port/G658_* report artifacts in ledger Git.
Source: https://docs.kernel.org/dev-tools/lkmm/docs/litmus-tests.html
