SM-T311 G662: fail-closed negative-control repair
===================================================

Earlier G661 recorded 24 source anchors and three negative controls.
The original negative-control checks only verified that a string was absent
after replacing that same string. They did not rerun the corresponding
source checks against the altered source. A positive result therefore did
not independently establish that the source gate would reject a mutation.

G662 makes source_checks() accept alternative filemap, migration and page
allocator source text. Its three negative controls now evaluate the full
24-condition gate with targeted mutations of the input sources:

* Remove XArray slot reload from filemap source.
* Remove synchronize_rcu_expedited() from page allocator source.
* Remove all folio_ref_freeze(folio, expected_count) occurrences in migration.

Each modified input must make at least one original condition false, while
the unmodified source must pass all 24. Missing required tokens must not
pass a source-order test merely because an index expression was evaluated.

Run tools/testing/selftests/smt311/rcu_writer_lifetime_g661.py. These
source-level mutation checks do not establish allocator generation
provenance, kernel concurrency correctness or ARMv7 LKMM safety.

Physical BOOT G650 and production runtime remain unchanged. RCU no-ref OFF,
S0 PASS, S1 ACTIVE, S2/S3/S4/S5/S6 BLOCKED, S7 PARTIAL.
