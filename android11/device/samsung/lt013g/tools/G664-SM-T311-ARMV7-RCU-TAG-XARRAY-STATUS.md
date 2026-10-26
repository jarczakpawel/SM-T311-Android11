# SM-T311 G664 - ARMv7 ref-free RCU tag source-gate evidence

Kernel source-only commits `6f884edba`, `d83f74913`, associated with `tools/testing/selftests/smt311/rcu_armv7_publication_g664.py`, and `Documentation/mm/sm-t311-rcu-armv7-publication-g664.rst`.

The local Exynos4212/Cortex-A9 SMP config uses `CONFIG_SMP=y`, `CONFIG_PREEMPT_RCU=y`. The folio tag uses `test_and_set_bit`, with ARMv7 SMP `_test_and_set_bit` path and an assembly `testop` macro carrying DMB before and after its RMW. Reader ordering, cached XArray slot reread and tagged last-free queue_rcu_work/expedited grace were structurally audited. **23/23 source checks and 5 independent actual mutated-source negative control rejections.**

This evidence is NOT a memory-model proof, no ARMv7 live race acceptance, and no validation of allocator generation identity or detached XArray node root membership. Do not enable experimental ref-free RCU, do not remove Normal-zone or writable-mmap restrictions, and do not flash documentation-only changes.

Stages: S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED; S5 BLOCKED; S6 BLOCKED; S7 PARTIAL. Physical BOOT remains G650, Android 11 `7.3.0-pjarczak`, no-ref RCU OFF. Last verified BOOT SHA256 `2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3`. Owner alone will publish GitHub.
