# G674 SM-T311 — staged Linus/Kiryl inode-exit RCU lifetime correction

Kernel commit 5a038ac99 introduces a real mm/filemap.c change: for CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT, retain PG_rcu_free on exiting inodes in page_cache_delete and page_cache_delete_batch. It prevents the investigated cross-inode B generation from being immediately recycled after another inode's reader saw its flag. Other architectures keep upstream shortcut.

Source-linked 19-event SC model: 75,582 order-preserving interleavings, 11 illustrative problematic old paths, zero in patched conditional model. 14 source checks PASS, seven actual mutated-source guards rejected. This is NOT a full ARMv7 weak-memory proof or a physically reproduced UAF.

Kernel commit c1287e84d updates S0-S8 plan checker and machine state, retaining S2/S3 BLOCKED and recording new mm/filemap.c as STAGED NOT INSTALLED. Standalone ARM32 mm/filemap.o compilation passed but no linked BOOT built or flashed. Physically SM-T311 still boots G650, kernel 7.3.0-pjarczak, SELinux Enforcing, RIL running, SIM LOADED, default no-ref RCU OFF. All sound, camera, PIN/call and dangerous USB tests omitted.

No GitHub action; owner publishes. No backups.
