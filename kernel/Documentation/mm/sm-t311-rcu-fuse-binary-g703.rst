G703 - ARM32 FUSE binary provenance with conditional RCU source
==============================================================

Samsung SM-T311 host-only release qualification, 2026-10-21.

The command below reads two previously linked vmlinux ELF files,
corresponding object files, recorded build-command files, and tracked
kernel source. It neither compiles nor communicates with the tablet::

  python3 tools/testing/selftests/smt311/rcu_fuse_binary_provenance_g703.py

Results are recorded in sm-t311-rcu-fuse-binary-g703.json. All 15
independent source/config/real-disassembly predicates passed. The
checker counts executable 32-bit ARM instructions separately from
inline 32-bit literal-pool data emitted by objdump as .word.

What actually differs between G686 EXP and G700 OFF+DEBUG_FS:

* Both were compiled with CONFIG_FUSE_FS=y and CONFIG_DEBUG_FS=y.
  The two RCU switches alone differ. All five FUSE .o build command
  files match byte-for-byte across profiles, while their resulting
  machine objects differ. No fs/fuse source changes exist relative
  to the physically installed G650 kernel source commit a2fca4558.
* CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT adds an atomic64_t diagnostic
  member to struct fuse_conn. fuse_conn_init initializes 240 bytes
  under EXP and 224 bytes under OFF. In fuse_direct_io the ARM32
  load shown in the report uses offset 0x80 under EXP and 0x70 under
  OFF. Both kernels must use one internally consistent structure
  layout; equal ELF global symbol names do not prove that condition
  for every call site.
* In experimental builds only, fuse_file_io_open contains the
  inlined atomic64 increment for a successful passthrough open.
  fuse_ctl_add_conn creates an extra FUSE control file named
  passthrough_opens: seven vs six fuse_ctl_add_dentry direct calls.
  G700 intentionally loses this OPTIONAL diagnostic, even though
  DEBUG_FS itself stays enabled. This counter is in fuse control,
  not a requirement for ordinary FUSE passthrough semantics.
* fs/fuse/dev.c tests folio_test_rcu_free in fuse_copy_folio.
  CONFIG_PAGE_RCU_FREE=y gives that flag a real bit position and
  changes later page flag positions. With CONFIG_PAGE_RCU_FREE off,
  folio_test_rcu_free is defined false and page layout constants
  change. The FUSE pipe-steal path is linked for both profiles.
* fuse_conn_init, fuse_file_io_open, fuse_ctl_add_conn,
  fuse_copy_folio, and fuse_direct_io have five genuine function
  size changes established in G702. This source-level derivation
  explains direct conditionals and structural changes, but does
  not establish identical external semantics, race freedom,
  or performance on the actual tablet.

The complete G700 8MiB BOOT remains
aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f.
It has not been flashed. Original G650 physical p9/p10 hashes still
need supervised read-only identity verification in genuine TWRP.
The staged experimental RCU reader remains fail-closed; no ref-free
activation, SELinux relaxation, flash, TWRP reboot, backup, or sound
test was performed during G703.

S0 PASS, S1 ACTIVE, S2-S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
Next G704: assess whether the lost optional FUSE diagnostic needs
an independent Kconfig option or should remain omitted in G700,
and inventory structural ABI call sites without unverified runtime
claims. Restore safe physical release gates separately.
