# G711 SM-T311 — order-0 migration RCU lifetime and physical BOOT identity

- New kernel source checker `tools/testing/selftests/smt311/rcu_migration_tag_lifetime_g711.py` passes **24/24 source/SC model gates, 6/6 actual source mutations caught**. Migration XArray replacement and copying of normal flags occur, but folio_migrate_flags does not explicitly transfer PG_rcu_free. Later freeing a destination that remains untagged reintroduces the known untagged->new-tagged same-PFN lifetime problem in a bounded model; NOT proof of a real crash. Do not patch by blindly copying a tag after XArray publication.
- G700 RCU-OFF is still HOST ONLY. No runtime Android/kernel C change and no physical flash. G692 fail-closed no-ref RCU remains staged, not deployed to G650.
- Physical read-only ADB confirms /dev/block/by-name/BOOT -> mmcblk0p9 and RECOVERY -> mmcblk0p10, both root:root mode 0600. ADB is uid 2000 shell, cannot compute present BOOT/recovery SHA or verify remote TWRP. No raw partition read, root escalation or SELinux changes.
- G700 pinned full host BOOT SHA `aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f`, DO NOT FLASH.
- S0 PASS, S1 ACTIVE, S2-S6 BLOCKED, S7 PARTIAL, S8 ACTIVE. NEXT G712 migration publication and safe fallback review. Each future report and handoff: ZROBIONE/W TOKU-BLOKADY/DALEJ/FIZYCZNIE NA TABLECIE plus all S0-S8. GitHub user only.
