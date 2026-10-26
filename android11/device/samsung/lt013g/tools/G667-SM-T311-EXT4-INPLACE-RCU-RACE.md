# SM-T311 G667 - live folio writes, ext4 partial zeroing and RCU no-ref read

Kernel diagnostics commits: `cf3f5b27e` (source gate, JSON, RST) and `4f337d853` (machine-stage ledger). Executable source-only check `tools/testing/selftests/smt311/rcu_ext4_inplace_g667.py` passed 26/26 anchors and correctly rejected five modified-source negative examples.

Verified: ext4_zero_range -> ext4_zero_partial_blocks -> ext4_block_zero_range -> folio_zero_range can modify a still-live folio before page-cache invalidation. Both ext4_block_do_zero_range and ext4_block_journalled_zero_range write while holding a folio lock/reference. In contrast, the no-reference reader `filemap_read_rcu_batch` holds RCU but no folio lock and copies with `_copy_to_iter`. A concurrent ordinary file write can produce mixed old/new bytes; this alone is NOT evidence of memory reuse/UAF. It still needs proper lifetime and cache coherency.

ext4 collapse/insert wait on writeback and truncate affected cache before extent shifting; mmap page_mkwrite takes shared invalidate_lock, but no-ref reader does not. The ARMv7 `mapping_writably_mapped` test precedes entry into the RCU section; safety against concurrent writable mapping enable and alias/cache coherency is **NOT PROVEN**. Keep both the guard and safe default OFF.

G665 native XArray host binary remains UNRUN without liburcu development headers (only runtime liburcu installed); do not report a pass. S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED, S5 BLOCKED, S6 BLOCKED, S7 PARTIAL.

No Android executable changes. Physical G650, Android11 Linux 7.3.0-pjarczak Enforcing RIL running, SIM loaded, no-ref RCU default OFF. BOOT last verified SHA256 `2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3`. No BOOT/TWRP flash or backup, no audible/camera/PIN actions. GitHub upload owner only.
