# G680 SM-T311 — staged writable mmap RCU publication guard

Kernel commit `e7e3e9fca` introduces an ARMv7-specific source fix for a source-identifiable cache alias race: in `mm/filemap.c`, mapping_writably_mapped is now checked *inside* rcu_read_lock; in `mm/vma.c:mmap_region()`, after mapping_map_writable increments writable count and before new VMA publication, tagged-inode shared writable mmap waits for synchronize_rcu under CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT.

4 actual LKMM herd7 models: correct grace + reader lock Never 0/3; remove grace Sometimes 1/3; substitute smp_mb Sometimes 1/3; count precheck outside RCU Sometimes 1/3. Source test 15/15 PASS and 7/7 modified C source controls REJECT. ARM32 compile mm/filemap.o and mm/vma.o PASS. Kernel status gate commit `4f4849aeb`.

**STAGED, NOT INSTALLED.** Real device G650 source a2fca4558. Exactly three staged runtime paths vs G650: mm/filemap.c, mm/page_alloc.c, mm/vma.c. RCU no-ref default OFF. Additional GP occurs in mmap_region while mmap_lock held; lock-order risk and latency NOT assessed, full mmap/mprotect/fork/remap coverage unproven. S0 PASS, S1 ACTIVE, S2–S6 BLOCKED, S7 PARTIAL, S8 ACTIVE. No flash, backups, night calls/audio/camera/PIN/OTG. Github owner only.
