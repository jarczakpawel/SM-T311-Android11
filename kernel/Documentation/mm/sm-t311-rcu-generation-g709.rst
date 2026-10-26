G709 - evaluating PFN-generation protocols without enabling no-ref RCU
===================================================================

SM-T311 dual Cortex-A9, Linux 7.3.0-pjarczak, 2026-10-21.

Executable host-only analytical tool::

  python3 tools/testing/selftests/smt311/rcu_generation_protocol_g709.py

Machine-readable reproducible output:
Documentation/mm/sm-t311-rcu-generation-g709.json

The script checks five actual code/Kconfig anchors, preserves the G708
previous source evidence, and exhaustively enumerates schedule order
preserving interleavings of a SINGLE reader with a SINGLE writer,
ONE page-cache slot, and TWO generations reusing the SAME physical
struct page/PFN. It is an explicit sequentially consistent toy model.
It is NOT the LKMM/herd7 model from G659, NOT Linux allocator
verification, and NOT an actual RCU proof.

Under the abstract assumptions implemented by the script:

* Existing conditional defer, initially UNTAGGED old folio: 462
  completed interleavings, ONE schedule accepts a new tagged
  generation as the original. A concrete witness is:

  R_enter -> R_load(old PFN) -> W_remove -> W_retire(old untagged,
  immediate recycle) -> W_reuse(same PFN) -> W_insert ->
  W_mark(new PG_rcu_free) -> R_guard(new flag) -> R_reload(same
  pointer) -> R_copy(new occupant as old) -> R_exit.

  This is another analysis of the known G659/G691 issue, not a
  claim to have observed a hardware memory corruption.

* Existing conditional defer with already TAGGED old folio: 56
  complete schedules; zero ABA in the tiny model. Its writer cannot
  recycle while a tracked reader is active.
* Candidate universal physical lifetime defer for every potentially
  sampled old folio, even one that was initially UNTAGGED: 56
  complete schedules, zero ABA and zero raw generation-mismatch
  reads in the model. However, global coverage of every relevant
  allocator/free path, memory pressure and Linux RCU correctness
  have NOT been proven; implementing this speculatively would
  risk latency or OOM on this 32-bit tablet.
* Candidate independent atomic XArray slot (pointer, epoch) token:
  462 complete schedules, zero accepted ABA **if** the model
  assumes that pair is atomic and versioned. Yet 15 schedules
  read an already reused generation's folio flags BEFORE the
  final version validation, and 20 dereference after old retirement
  but before its physical replacement. This candidate is NOT a
  complete memory-lifetime protocol; an epoch field inside the
  reused struct page would be even weaker. It is not implemented.
* The ordinary reference-backed filemap_get_entry() pattern:
  385 completed schedules, zero ABA and no unpinned raw flag
  reads of retired objects in the simplified model. A speculative
  folio_try_get may acquire a NEW current incarnation, which the
  slot reload validates while the acquired reference pins it.
  Keep this supported production behavior.

Three explicitly BROKEN versions (universal defer without RCU grace,
slot epoch without compare, and refcount fallback without reference)
each accept an ABA schedule. Together with source/model assertions
the G709 gate is 21/21 PASS. This is a bounded schedule explorer:
a different number of readers/writers, memory reordering, folio
migration/splitting, weak ARMv7 ordering and all allocator entrypoints
remain outside the model.

Read-only physical memory context:
the installed G650 /proc/zoneinfo Normal zone held 203153 managed
4KiB pages and 75863 free 4KiB pages at the sample. /proc/meminfo
reported MemTotal 1483332kB and MemAvailable around 896760-899708kB
as sampled; zone free is not the same as MemAvailable. Normal
high watermark was 1350 pages. No benchmark or write was performed.

Hypothetically deferring 1024 / 4096 / 16384 physical folios at 4KiB
per folio would retain 4 / 16 / 64 MiB, i.e. 1.35 / 5.40 / 21.60
percent of THAT SNAPSHOT's Normal free pages. The real RCU grace
duration, page-retirement rate and contention were NOT measured.
The report also includes explicitly assumed rate*grace envelopes.
These are sizing exercises, not observed overhead.

DECISION: no reference-free RCU ON, no new physical allocator
recycling semantics, no independent epoch protocol patch. Keep the
working reference-backed path, G692 staged fail-closed setter and
G700 HOST ONLY release-OFF as already built (8MiB SHA256
aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f).

No kernel or Android runtime C changes, new kernel image,
flash/reboot/TWRP, backups or audible/PIN/phone tests in G709.
Physical BOOT p9 and RECOVERY p10 SHA and reliable recovery remain
unverified, even though ADB USB reports a live device.

S0 PASS, S1 ACTIVE, S2-S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
G710: test a broader memory-lifetime design against weak ordering,
multiple slot replacement/free paths, migration/split and actual
normal-zone pressure budget; obtain physical release-safe p9/p10
identity only with owner-attended, verified TWRP fallback.
