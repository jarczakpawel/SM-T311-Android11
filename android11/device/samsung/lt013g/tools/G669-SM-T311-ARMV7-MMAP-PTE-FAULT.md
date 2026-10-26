# SM-T311 G669 - ARMv7 writable mmap fault, PTE cache synchronization

Kernel source-only commits: `4a1058f7e` (reproducible 24/24 source-anchor checks with six rerun mutated-source rejections, JSON/RST) and `7bb6a5cd8` (machine-phase status). Audit `tools/testing/selftests/smt311/rcu_armv7_fault_g669.py`; RST `Documentation/mm/sm-t311-rcu-armv7-mmap-fault-g669.rst`.

**Architecture correction:** On actual ARMv7, `update_mmu_cache_range()` is an intentional no-op. `arch/arm/mm/mmu.c:set_ptes()` calls `__sync_icache_dcache()` before publishing a valid ordinary user PTE. The ARM flush checks cacheid/PG_dcache_clean, flushes dirty lowmem and alias colors where required. ARM alias-aware `arch_get_unmapped_area()` handles alignment and MAP_FIXED shared-color restrictions. mm/memory.c shared fault calls ext4 page_mkwrite before finish_fault; ext4 hook provides a shared invalidate lock and relevant folio checks.

**Not established:** this existing cache/PTE synchronization does NOT prove that ref-free RCU copying is immune to a new writable mmap or overlapping shared write begun after its one-time `i_mmap_writable` precheck. G668 SC check-use witness remains, but no wrong-byte ARMv7 physical output, use-after-free or confirmed bug has been reproduced. Cortex-A9 runtime cache geometry/alias behavior not yet resolved (sysfs displayed cache types but no usable sizes or ways). No source-level race fix or no-ref RCU ON is authorized.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED; S5 BLOCKED; S6 BLOCKED; S7 PARTIAL. G665 actual host C XArray regression still unrun due absent liburcu-dev headers.

No Android executable changes. Physical G650 BOOT with last-verified SHA256 `2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3`; Android11 Linux `7.3.0-pjarczak`, Enforcing, RIL running, SIM LOADED, no-ref RCU default OFF. No new flash or backup, no PIN automation, audio/camera or calls. Owner publishes GitHub.
