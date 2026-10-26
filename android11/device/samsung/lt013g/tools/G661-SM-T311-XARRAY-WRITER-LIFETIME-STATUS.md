# SM-T311 G661 - lockless XArray folio reader/writer safety status

- Kernel branch: `sm-t311-port-linux-7.3`, audit commits `6228b1268` and `473b98987`.
- Kernel audit source: `tools/testing/selftests/smt311/rcu_writer_lifetime_g661.py`, machine result `Documentation/mm/sm-t311-writer-lifetime-g661.json`.
- 24/24 reader/writer source anchors PASS; 3 negative source mutation controls PASS.
- The writer uses XArray locking during deletion, replacement, addition and migration, but speculative `filemap_read_rcu_lookup()` does not acquire the XArray lock. A reused same-address folio initially unmarked remains an unresolved provenance scenario.
- Single and batch deletion have different mapping-null vs XArray-slot update order; final truncate, migration, tagged last-free and hwpoison routes were explicitly inspected.
- The source checks do not demonstrate a real UAF or prove correctness on ARMv7. Keep S1 ACTIVE, S2 and S3 BLOCKED.
- The source change affects kernel tests and documentation only, not Android device HAL or ROM; it is not a reason to flash BOOT, recovery or SYSTEM.
- Running target remains Android 11, Linux `7.3.0-pjarczak`, physical BOOT G650 last known SHA256 `2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3`, SELinux Enforcing, RIL running, SIM LOADED, no-ref RCU OFF.
- Stages: S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED; S5 BLOCKED; S6 BLOCKED; S7 PARTIAL.
- Do not recreate backups, touch PIN/PUK, perform audible tests during owner absence, or publish GitHub. Owner publishes.
