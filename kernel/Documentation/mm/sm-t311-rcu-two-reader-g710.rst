G710 - two concurrent readers, folio migration/split, and PFN reuse
=================================================================

2026-10-21, Samsung SM-T311 Android 11 / Linux 7.3.0-pjarczak.

Reproducible host-only check::

  python3 tools/testing/selftests/smt311/rcu_two_reader_migration_g710.py

The output is Documentation/mm/sm-t311-rcu-two-reader-g710.json.
Result: 22/22 source-anchored/model gates PASS; all four deliberately
BROKEN protocol controls expose the expected unsafe outcome. No new
kernel binary was built and no runtime kernel C code was modified.

Two independent readers (R0, R1), one writer, two physical folio
incarnations at the SAME PFN, and one XArray slot are explored via
breadth-first state enumeration. Each reader has enter, load, check,
reload+copy and exit milestones; the writer has remove, retire, reuse,
insert and tag. States are deduplicated, and unsafe paths carry
concrete witness event sequences. These are sequentially consistent
abstract actions, NOT actual Linux LKMM/herd7 or the full allocator.

============================================== =========== =========
Profile                                        Reachable   Unsafe end
============================================== =========== =========
Old UNTAGGED, conditionally deferred            1637         13
Old TAGGED, conditionally deferred                676          0
Defer every potentially sampled OLD folio         602          0
Independent atomic slot epoch                    1637          0
Refcount-based referenced lookup                 1337          0
============================================== =========== =========

The slot-epoch profile also reaches 24 terminal states in which
a reader has already examined fields from a physically reused
generation before the epoch comparison. This is NOT solved by
preventing subsequent ABA acceptance. The old-UNTAGGED profile has
13 unsafe distinct END STATES, NOT 13 hardware failures and NOT 13
different schedules. The universal defer and reference-backed
profiles have no accepted unsafe end states under these assumptions.
No practical Linux implementation is proven by these counts.

All four intentionally disabled protections reveal unsafe end states:
- universal deferred frees WITHOUT an RCU grace period;
- universal defer waits for R0 but incorrectly ignores active R1;
- epoch token comparison removed;
- reference pin acquisition removed.

The source inventory additionally traces migration and splitting:
- mm/migrate.c: __folio_migrate_mapping() replaces the XArray entry
  with the destination folio; successful move frees the old source.
  folio_migrate_flags() transfers several ordinary named flags but
  DOES NOT explicitly copy PG_rcu_free to the destination.
- mm/huge_memory.c: __split_folio_to_order() includes __PG_RCU_FREE
  in the mask copied to split sub-folios and publishes compound
  metadata with smp_wmb().
- The optional SM-T311 raw reader is restricted to order-0
  Normal-zone ext4 folios and rejects large folios. This lowers
  its exposure to the large-folio split path; it does NOT establish
  correct lifetime across all migration/free paths.
- The allocator only RCU-defers folios with PG_rcu_free set.
  An initially UNTAGGED source may be recycled immediately after
  the last reference drops.

G709 passive Normal-zone sample was 203153 managed pages and 75863
free 4KiB pages. A hypothetical 1024/4096/8192/16384/32768
simultaneously deferred folios would hold 4/16/32/64/128 MiB.
These are scenario envelopes compared to a one-time physical
snapshot, NOT measured RCU grace durations or runtime allocation
pressure. No new physical memory snapshot was taken for G710.

This source-anchored SC model still excludes multiple XArray slots,
multiple independent writers, detailed migration/split transitions,
actual ARMv7 weak ordering, VIPT/HighMem aliases and mmap/lockdep.
S1/S2/S3 are NOT discharged, and S4 no-ref ON remains blocked.

G700 remains the uninstalled HOST ONLY release RCU-OFF BOOT image
SHA256 aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f.
No unattended TWRP or BOOT flash: current physical p9/p10 identities
and reliable recovery route remain unverified. Do not enable the
unsafe staged debugfs RCU key on any device.

Stages: S0 PASS; S1 ACTIVE; S2-S6 BLOCKED; S7 PARTIAL; S8 ACTIVE.
Next G711: examine concrete normal-zone free paths and the feasibility
of an identity/lifetime mechanism before any implementation, and
review S6 recovery/partition identity without losing ADB.
