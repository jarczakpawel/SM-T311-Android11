# SM-T311 G666 - ext4 direct I/O, punch-hole and XArray eviction source audit

Kernel commits `ec772526f` and `77f57439f`; the exact executable audit script is `tools/testing/selftests/smt311/rcu_ext4_invalidation_g666.py`. All 25 structural source checks pass, and five deliberately broken versions of the input sources are rejected by the same 25-condition gate.

Verified direct write chain: `ext4_file_write_iter -> ext4_dio_write_iter -> iomap_dio_rw -> kiocb_invalidate_pages -> filemap_invalidate_pages -> invalidate_inode_pages2_range -> folio_unmap_invalidate -> __filemap_remove_folio -> page_cache_delete`, then final folio put and allocator RCU deferral for tagged pages. Verified hole-punch chain: `ext4_fallocate -> ext4_punch_hole -> ext4_truncate_page_cache_block_range -> truncate_pagecache_range -> truncate_inode_pages_range`.

**Critical remaining caveat:** reference-free `filemap_read_rcu_batch()` doesn't take `invalidate_lock`. The writer's lock does not by itself protect a lockless reader's folio lifetime. Actual correctness must derive from flag publication, XArray coordination and RCU grace, none fully proven for ARMv7. Direct-I/O fallback's `invalidate_mapping_pages()` is best-effort. No-ref RCU remains OFF.

Prior G665 native XArray regression `smt311-xas-g665` remains **UNRUN**, since `liburcu-dev` headers and host APT indexes are absent. Installed `liburcu.so.8` runtime is not a substitute for development headers.

Stages: S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED, S5 BLOCKED, S6 BLOCKED, S7 PARTIAL. No Android executable code changes or new BOOT. Physical baseline G650, kernel `7.3.0-pjarczak`; BOOT SHA last verified `2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3`.

No backups, no PIN automation, no audio/camera testing while owner sleeps. GitHub publication by owner only.
