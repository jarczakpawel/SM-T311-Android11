# SM-T311 G662 - effective source-negative controls

Kernel source HEAD G662 commits `4a476cb35` and `2fdc59794`.

The G661 writer-lifetime checker `tools/testing/selftests/smt311/rcu_writer_lifetime_g661.py` originally marked its three negative controls successful by checking that a string removed with `replace()` was no longer present. G662 reruns the original full 24-condition gate against three independently mutated source inputs instead: no XArray slot reload, no expedited RCU grace, and no migration reference freeze.

Verified on Debian: 24/24 base-source checks PASS, and all 3 modified-source gates reject their targeted mutation. This validates the sensitivity of three structural source anchors only. It is NOT a runtime race test or a proof of folio lifetime, XArray ABA safety, or HighMem correctness.

Physical BOOT G650 unchanged (last-verified SHA256 `2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3`); Android 11 kernel 7.3.0-pjarczak, SELinux Enforcing, no-ref RCU OFF. No rebuilt system, reflash, backups or modem actions.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED; S5 BLOCKED; S6 BLOCKED; S7 PARTIAL.

Next: source-accurate old-unmarked same-address folio provenance and reader-copy integrity. Keep ref-free RCU disabled; publish GitHub only by project owner.
