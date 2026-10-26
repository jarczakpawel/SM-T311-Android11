SM-T311 G695: physical G650 config versus RELEASE-OFF and experimental hosts
=============================================================================

Why an independent physical-config check is essential
-----------------------------------------------------
G694 used a byte-identical BASE RCU-OFF defconfig to compare the G650
SOURCE commit against the current RCU-OFF SOURCE. That comparison was
correct **for matched RCU-OFF host configurations**, but the *deployed*
G650 BOOT uses a different configuration. G694 was not an on-tablet A/B
or a reproduction of the exact G650 deployed binary.

G695 directly inspected the running tablet without modifying it:

  zcat /proc/config.gz | sha256sum
  zcat /proc/config.gz | grep -E 'CONFIG_...'
  cat /proc/version

Physical observed results:
- `uname -r`: `7.3.0-pjarczak`.
- `/proc/version`: GCC Debian 16.2.0-3, link GNU ld 2.47,
  UTS_VERSION `#9 SMP PREEMPT Tue Oct 20 18:36:31 CEST 2026`.
- 6,794 config lines, 183,367 uncompressed bytes.
- Uncompressed config SHA256
  `ef893db0ed858fc5a03b4ca3d25a820c55bd599dc53f4fafc116de150408d496`.
- `CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y`,
  `CONFIG_PAGE_RCU_FREE=y`,
  `CONFIG_DEBUG_FS=y`, `CONFIG_PREEMPT_RCU=y`,
  `CONFIG_ARCH_EXYNOS4=y`, `CONFIG_HIGHMEM=y`.

The experiment was therefore **compiled in** to physical G650.
Compiled in does not mean ON! The uid=2000 ADB shell could not
read the debugfs static-key file, and even zero RCU vmstat counters
do NOT establish the dynamic key's actual value. The historical
default was static key FALSE. The physical G650 setter also predates
G692's fail-closed interlock; do not assert the guard is deployed.

Exact 6,794-line reproducible configuration reconstruction
----------------------------------------------------------
The full matching host experimental `.config` has SHA256
`e6ead0004554ea21cd6d00e10debb66cb751cea16a0b58581090adc0f419a60f`.
The direct ADB output and this host config were compared line by
line. Exactly THREE physical differences exist:

  Line 14: CONFIG_RUSTC_VERSION=109500         (host: 109701)
  Line 15: CONFIG_RUSTC_LLVM_VERSION=210108    (host: 220108)
  Line 16: CONFIG_RUSTC_LLVM_MAJOR_VERSION=21  (host: 22)

G695's executable validator reconstructs the complete 183,367-byte
physical config by applying those three substitutions to the
versioned host experimental config. The reconstructed SHA256
**matches the actual running device fingerprint exactly**. Therefore
all other configuration lines match byte-for-byte; the three
differences are Rust toolchain metadata and do not establish a
different ARM C kernel Kconfig feature selection.

The physical config observation is preserved compactly as
`Documentation/mm/sm-t311-physical-profile-g695.json`.
This is NOT a BOOT backup or proof the linked host ELF matches the
actual physical BOOT image.

Independently cross-compiled G650 experiment source
---------------------------------------------------
The exact physically deployed kernel SOURCE commit `a2fca4558`
was recreated as a temporary detached Git worktree and four real
ARM EABI5 object files were compiled with the **host experimental**
configuration (byte-identical to the current host experimental
comparison), using the same GCC 16.2.0-3 toolchain. Four output
objects were retained at:

  ../comparison/g695-g650-experiment-build/mm/filemap.o
  ../comparison/g695-g650-experiment-build/mm/shmem.o
  ../comparison/g695-g650-experiment-build/mm/page_alloc.o
  ../comparison/g695-g650-experiment-build/mm/vma.o

Matching objects in `../build/g686-reproducible-exp-on` contain the
current experimental source, with G692 ON activation refused.
The normal release OFF objects remain in
`../build/g687-release-rcu-off`.

Static ARM32 findings, NOT physical elapsed-time measurements:

  Symbol                 G650 EXP      New EXP      New OFF
  ----------------------------------------------------------------
  filemap_read           314 words    314 words    309 words
  shmem_file_read_iter   230 words    230 words    230 words
  mmap_region             89 words    100 words     89 words
  rcu_free_sync           24 words     24 words      1 word
  lockless_read_set      132 words     55 words    (absent)
  filemap_read_rcu       417 words    410 words      2 words

- In G650's experimental build the `filemap_read` RCU call is
  actually linked; the synthetic release-OFF G694 build from the
  SAME G650 source did not call it. This is why the G694 conclusion
  cannot be extrapolated to the physical G650 compiler profile.
- `shmem_file_read_iter` is unchanged across these three output
  profiles: 230 ARM words, 920 bytes. G688's shmem optimization
  is a **release OFF vs G650 source compiled as OFF** change, not an
  additional compiled reduction against the actual physical G650
  experimental configuration.
- `mmap_region` has 11 EXTRA ARM instructions and a newly emitted
  `R_ARM_CALL synchronize_rcu` in the CURRENT EXPERIMENT compared
  with G650 and standard OFF. The ext4 shared-maywrite mapping path
  contains that GP even when the new G692 experimental static key
  can never be enabled via debugfs. This may add mmap latency if
  experimental code is deployed. Its frequency and cost were NOT
  measured. Never disable this barrier speculatively: the existing
  G680/G683 reader/writer safety argument depends on it if the
  experimental reader is enabled in future.
- `rcu_free_sync` still emits 24 ARM words with different workqueue
  flush targets, reflecting G676's fix to wait for pending RCU work.
  No physical end-to-end impact was measured.
- G692 reduces the debugfs setter's compiled instructions
  (132 to 55) through fail-closed optimization; that is not an
  improvement to normal file I/O because the optional reader is
  deliberately prevented from turning on.

The code under test and exact reference inputs are checked by
`tools/testing/selftests/smt311/rcu_physical_profile_g695.py`;
the machine report `Documentation/mm/sm-t311-physical-binary-g695.json`
records the actual ARM32 instruction counts, relocations, C source
guards, relevant ELF object metadata, and falsified comparator tests.

Reproduce both independent G650 baselines from local Git
-------------------------------------------------------
The tracked `tools/testing/selftests/smt311/build_g650_comparison_g695.sh`
is the build-only bootstrap for a fresh checkout whose `../comparison`
objects are absent. It requires the local G650 commit `a2fca4558`,
a working ARM32 cross-compiler and the two independently linked
current host profiles from G686/G687. From the current kernel root:

  bash tools/testing/selftests/smt311/build_g650_comparison_g695.sh

It creates a short-lived G650 detached worktree, reproduces the G650
EXPERIMENT=y objects and the G650 RELEASE-OFF comparison objects from
two byte-identical .config files, runs BOTH G694 and G695 source/ARM32
validators, then removes only the temporary worktree it created.
It retains the compiled diagnostic .o files and DOES NOT access or
back up the Android device. A preexisting source directory is NOT
overwritten. The script was actually executed and returned PASS
on both comparisons with the SAME object SHA256 as the prior builds.

Decision and known blockers
---------------------------
The safe recommendation is **not** to flash the new experimental
comparator on the sleeping tablet. It adds an ext4 mmap GP and still
does not resolve untagged old-folio PFN ABA or ARMv7 cache/HighMem
and global lockdep safety. The release-OFF build avoids this GP and
does not contain the unsafe no-ref feature, but is not yet a
verified flashable complete Samsung BOOT or measured physically.

No executable kernel source changed in G695. Four prior source paths
remain STAGED NOT INSTALLED (mm/filemap.c, mm/page_alloc.c,
mm/shmem.c, mm/vma.c) plus one separate config overlay. Physical G650
stays on the tablet. No device flash, reset, backup, night-time
camera/audio/phone/PIN/OTG test or real throughput benchmark.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED;
S5 BLOCKED; S6 BLOCKED; S7 PARTIAL; S8 ACTIVE.
