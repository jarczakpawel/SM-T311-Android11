G711 - order-0 folio migration and RCU lifetime exposure
=======================================================

SM-T311, Linux 7.3.0-pjarczak, 2026-10-21.

Reproducible host-only checker::

  python3 tools/testing/selftests/smt311/rcu_migration_tag_lifetime_g711.py

Source and deterministic report:
Documentation/mm/sm-t311-rcu-migration-g711.json.

24/24 checks passed; 6/6 actual modified-source counterfactuals detected.
The G711 model consists of a fixed source-to-destination migration
prefix, followed by all 252 order-preserving interleavings of five
reader and five destination eviction/reuse milestones.

Concrete C source evidence:
- mm/migrate.c __folio_migrate_mapping() freezes the old folio's
  refcount before XArray replacement, then unfreezes after publication.
  This discipline counts normal references, not the speculative
  no-reference reader in optional mm/filemap.c.
- The successful migrate path invokes folio_migrate_flags(dst, src),
  which transfers ordinary flags but does NOT explicitly transfer
  PG_rcu_free. The old source is finally released via folio_put(src).
  A tagged old source can thus be followed by an initially UNTAGGED
  destination. This is a source-level finding, not a device crash.
- mm/page_alloc.c __free_pages_prepare() defers only tagged folios.
  Under the hypothetical RCU-enabled experiment, later removal and
  recycling of the untagged destination could expose the already-known
  G659/G691 old-untagged/new-tagged same-PFN ABA problem.
- mm/huge_memory.c copies __PG_RCU_FREE for large-folio splitting,
  but the optional SM-T311 reader rejects large folios; ordinary
  order-0 migration remains a separate issue.
- The normal reference-backed filemap_get_entry/folio_try_get path
  remains the production choice.

In the sequentially consistent model:
- As-built migration, destination UNTAGGED: 252 complete schedules,
  ONE accepted same-destination-PFN ABA witness.
- Hypothetically preserving a tag on the destination with a grace
  period: 42 complete schedules and ZERO accepted ABA under this
  simplified model.
- Normal reference-backed fallback: 206 complete schedules and ZERO
  accepted ABA under model assumptions.
- A hypothetical copied tag WITHOUT the grace period: 252 complete
  schedules and TEN accepted ABA schedules.

The difference in number of completed schedules is caused by
guarded reuse while an RCU reader is active, NOT measured speed.
These counts do NOT prove an actual Linux UAF, a complete fix,
or correct ARMv7 weak-memory semantics.

Merely copying the tag in folio_migrate_flags() is NOT authorized.
The migration function publishes the new XArray pointer and then
copies flags; any proposed code must establish publication and reader
ordering, test all filesystem callbacks and source/destination
lifetime rules, memory pressure, PFN reuse and cache alias safety.
No runtime MM C change was made.

New passive S6 facts:
Android ADB is uid=2000 (shell), kernel 7.3.0-pjarczak.
The actual partition links are BOOT => /dev/block/mmcblk0p9 and
RECOVERY => /dev/block/mmcblk0p10. Both block nodes are root:root
with mode 0600 and shell cannot read either. Current live p9/p10
SHA256 cannot be verified through this ADB shell. This is a
read-only finding, NOT permission to change SELinux or gain root,
and NOT validation of remote TWRP recovery.

G700 full host BOOT remains 8MiB, SHA256
aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f,
HOST ONLY / DO NOT FLASH.
No backups, physical writes, reboot, TWRP, PIN/PUK, calls or sounds.

S0 PASS; S1 ACTIVE; S2-S6 BLOCKED; S7 PARTIAL; S8 ACTIVE.
G712: evaluate migration-time PG_rcu_free transfer ordering and
alternate safe slowpath designs. Continue S6 only with confirmed
owner-attended recovery and current physical partition identity.
