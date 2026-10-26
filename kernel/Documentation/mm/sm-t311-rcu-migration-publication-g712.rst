G712 - folio migration flag publication order and ext4 callbacks
================================================================

Samsung SM-T311 Linux 7.3.0-pjarczak, 2026-10-21.

New reproducible host source+model gate::

  python3 tools/testing/selftests/smt311/rcu_migration_publication_g712.py

Machine report: Documentation/mm/sm-t311-rcu-migration-publication-g712.json.
28/28 genuine checks PASS, 6/6 deliberately damaged real-source
variants caught. No recompilation of physical BOOT or changed runtime C.

Source evidence
---------------

* mm/migrate.c __migrate_folio() calls folio_mc_copy(dst, src)
  before __folio_migrate_mapping(), then calls folio_migrate_flags()
  only after successful XArray replacement. In
  __folio_migrate_mapping(), xas_store(&xas, newfolio) occurs under
  XArray lock before refcount unfreeze and unlock. folio_migrate_flags
  is NOT inside this XArray transaction.
* folio_migrate_flags() does not explicitly copy PG_rcu_free, so a
  destination which replaces a tagged source can be published without
  that tag. The ordinary code holds references; speculative no-ref
  readers are not accounted for by source refcount freeze.
* ext4_aops and ext4_da_aops use buffer_migrate_folio, while
  ext4_journalled_aops uses buffer_migrate_folio_norefs.
  With buffers they use filemap_migrate_folio(); without buffers
  they fall back to migrate_folio(). Both reach the same common
  migration machinery. Buffer locking and refcount checks matter.
* mm/migrate_device.c and fs/aio.c also invoke folio_migrate_flags
  only after mapping publication, in their own callback protocols.
  A blind patch to the shared flag-copy function risks other users.
* G692 staged experimental setter still denies RCU ON with -95.
  The production G700 RCU-OFF host image bypasses the optional
  unreferenced reader entirely.

Sequentially consistent publication window toy model
----------------------------------------------------

For one reader and one migration writer with five ordered steps
apiece, 252 order-preserving schedules were enumerated. The
reader's tag check was simplified to whether it had loaded the new
XArray destination and whether that destination was marked.

================================ ========== ====================
Variant                          NEW untagged NEW tagged
================================ ========== ====================
Existing code (never transfer)          66                  0
Hypothetical tag transfer LATE          24                 42
Hypothetical tag transfer pre-publish    0                 66
================================ ========== ====================

All scenarios copy data before publishing new folio. A missing tag
at the reader's check means the speculative fastpath rejects and
tries the normal referenced fallback. It is NOT an immediate unsafe
read and these table entries are NOT crash counts or likelihoods.
The separate G711 scenario of subsequent UNTAGGED destination
eviction and same-PFN reuse is the long-term lifetime hazard.

A prepublication tag would remove the untagged publication window
within this toy model, but it is NOT a patch recommendation or a
Linux proof: all metadata, buffer_head ownership, page content,
memory barriers, XArray locking, destination/source accounting,
and concurrent readers would need verification first.
Tagging also changes allocator RCU deferral costs.

Conclusions and release gate
----------------------------

No new runtime MM changes, no RCU ON. Keep G700 RCU-OFF production
candidate (8MiB host BOOT SHA256
aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f).
It remains HOST ONLY / DO NOT FLASH: current physical BOOT p9 and
RECOVERY p10 SHA are inaccessible to uid2000 ADB shell, and a reliable
TWRP return path is unverified. Do not reboot unattended.

These are source-anchored SC window checks, NOT weakly ordered ARMv7
LKMM or real memory-safety proof. S0 PASS, S1 ACTIVE, S2-S6 BLOCKED,
S7 PARTIAL, S8 ACTIVE.

NEXT G713: decide whether a strictly gated, reference-backed
fallback without ever publishing no-ref reader access is preferable
to further experimental tag propagation. Enumerate all migration
callbacks and complete S1-S3 before proposing any kernel C patch.
