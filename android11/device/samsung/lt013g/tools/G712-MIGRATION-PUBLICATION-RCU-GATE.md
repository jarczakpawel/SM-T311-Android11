# G712 SM-T311 Android11 — RCU folio migration publication audit

- New kernel host checker `tools/testing/selftests/smt311/rcu_migration_publication_g712.py`, JSON+RST under `Documentation/mm/`: **28/28 checks PASS, 6/6 source mutations detected**. No executable kernel/Android changes.
- Actual mm/migrate.c ordering: `folio_mc_copy` then XArray slot replacement under lock, THEN `folio_migrate_flags`. The latter does not transfer `PG_rcu_free`. ext4 ordinary/delalloc migrations use `buffer_migrate_folio`; journalled ext4 uses `buffer_migrate_folio_norefs`. All require their distinct locking/refcount rules. `mm/migrate_device.c` and `fs/aio.c` also use late flag copying.
- Toy SC 252 schedules: 66 checks of new UNTAGGED destination with existing code, 24 if tag copied AFTER publish, 0 if prepublish tag. Such a check rejects no-ref copy and falls back to reference-backed reader; these are NOT device errors. Later PFN ABA lifetime remains unproven (G711).
- No blind tag migration patch, no ref-free RCU ON; retain G700 RCU-OFF host release BOOT SHA256 `aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f`, not installed. Physical p9/p10 SHA and reliable TWRP remain unknown.
- S0 PASS S1 ACTIVE S2-S6 BLOCKED S7 PARTIAL S8 ACTIVE. NEXT G713 safe reference-backed release assessment. Each report/handoff four status sections and all S0-S8, user GitHub.
