# G694 — exact G650-versus-RCU-OFF ARM32 binary comparison (HOST ONLY)

The actual deployed G650 source commit `a2fca4558` was independently cross-compiled with the **same byte-identical defconfig and same ARM EABI5 GCC 16.2.0-3 compiler** as the current release OFF. Kernel Git `7c761810b` adds source+nm+objdump validator `tools/testing/selftests/smt311/rcu_off_object_diff_g694.py`, JSON and RST.

**Important correction of prior speed interpretation:**
- G687 `filemap_read`: 1236 bytes and 309 **identical instruction words** in G650 and release OFF; zero `filemap_read_rcu` relocations in both. GCC already optimized the no-op call away in G650. No demonstrated machine-code improvement.
- G688 `shmem_file_read_iter`: 976 bytes, 244 ARM instructions and one real R_ARM_CALL to zero-return stub in G650; current release OFF 920 bytes, 230 instructions and zero such calls. **56 bytes / 14 ARM instruction words genuinely removed**. This is code-size evidence, NOT physical benchmark percentage or system speedup.
- Validator **17/17 real C/config/ARM32 assertions and 7/7 deliberately corrupted input controls** PASS. Full host kernel and release independent runner **26/26 regressions G659–G694**, S0–S8 plan PASS. Temporary G650 worktree cleaned; retain independently built two old object files as reproducible comparison outputs.

**No G694 executable kernel/Android code change**; four earlier executable files STAGED NOT INSTALLED versus physical G650: mm/filemap.c, mm/page_alloc.c, mm/shmem.c, mm/vma.c. One optional config overlay separate. G692 experimental RCU ON remains -EOPNOTSUPP; S2/S3 unsafe/blocked. Physical G650 Android 11 `7.3.0-pjarczak`, Enforcing, RIL running, SIM LOADED, RCU counters zero. No boot flashing, reboot, TWRP, backups or nighttime tests. S0 PASS S1 ACTIVE S2-S6 BLOCKED S7 PARTIAL S8 ACTIVE. Next G695. GitHub owner only.
