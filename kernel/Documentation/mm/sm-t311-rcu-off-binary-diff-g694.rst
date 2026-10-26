SM-T311 G694 — compare ACTUAL G650 and release-OFF ARM32 instructions
=========================================================================

Purpose
-------
Previous G687 and G688 source changes omitted a call to the always-zero
filemap_read_rcu() stub in the CONFIG_PAGE_RCU_FREE=n profile. Calling
an always-zero stub in C is not proof that a compiled ARM32 binary
contains that call or that runtime throughput improves.

G694 independently compiled actual physical G650 source commit
a2fca4558 in a detached test-only worktree, using the same tracked
sm_t311_android11_defconfig and Debian ARM32 EABI GCC 16.2.0-3
compiler as the current G687/G688 release-OFF profile. Byte-by-byte
comparison confirms BOTH generated .config files are identical.
Both kernelrelease strings equal 7.3.0-pjarczak.

Baseline source and build protocol (NO flash)
---------------------------------------------
Under kernel root `upstream-v7.3`:

  git worktree add --detach ../comparison/g694-g650-source a2fca4558

  export KBUILD_BUILD_TIMESTAMP=2026-10-21T00:00:00Z
  export KBUILD_BUILD_VERSION=1
  export KBUILD_BUILD_USER=smt311
  export KBUILD_BUILD_HOST=reproducible

  SOURCE="$(pwd)/../comparison/g694-g650-source"
  BUILD="$(pwd)/../comparison/g694-g650-build"

  make -C "$SOURCE" \
    O="$BUILD" ARCH=arm \
    CROSS_COMPILE=arm-linux-gnueabihf- sm_t311_android11_defconfig

  make -j8 -C "$SOURCE" \
    O="$BUILD" ARCH=arm \
    CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= \
    mm/filemap.o mm/shmem.o

The actual G694 build used absolute paths so Kbuild output lives at
`../comparison/g694-g650-build` relative to the current kernel root.
After verifying results, the redundant G650 source worktree was
removed using `git worktree remove`. Its two compiled baseline
ARM32 objects remain under the independent build directory for
repeatable G694 tests without retaining a source-tree copy.

Current matched release-OFF .o files are from
`../build/g687-release-rcu-off/mm/{filemap,shmem}.o`.
G694 uses ARM `nm -S` and `objdump -dr --disassemble=<symbol>`
on the objects to compare exact 32-bit instruction words, function
size, and R_ARM_CALL relocation targets. These are actual compiled
results, not C-level guesswork or generated pseudo-benchmarks.

Results
-------
With compiler/config held constant:

  Symbol / source                         G650          Release OFF
  -------------------------------------------------------------------
  filemap_read, function bytes             1,236          1,236
  filemap_read, ARM instruction words         309            309
  filemap_read, direct RCU-stub calls           0              0
  filemap_read, identical instruction words?  YES

  shmem_file_read_iter, function bytes       976            920
  shmem_file_read_iter, ARM instructions      244            230
  shmem_file_read_iter, RCU-stub calls          1              0

Therefore the G687 C simplification (avoid disabled RCU stub in
filemap_read) produces NO measured change in those ARM instruction
words against G650: the compiler already optimized it away. It
must not be marketed as a demonstrated machine-code throughput gain.

The G688 change in mm/shmem.c DOES remove the R_ARM_CALL
`filemap_read_rcu` from shmem_file_read_iter and reduces its size by
**56 bytes / 14 ARM instructions** in this matched-profile build.
The referenced shmem read machinery (folio_unlock, copy_page_to_iter,
folio_put) remains. This is a genuine static code-generation
optimization, **not** a benchmark showing any number of percent
faster wall time, CPU power savings or cache-miss reduction.

G694 runnable evidence
----------------------
  python3 tools/testing/selftests/smt311/rcu_off_object_diff_g694.py

The validator checks actual GCC string, byte-identical .config,
kernelrelease, nm symbol sizes, full filemap_read instruction word
equality, R_ARM_CALL targets, source guards and preservation of the
reference-counted copy path. It rejects seven deliberately altered
test inputs to prevent a false "G687 saved cycles" or false "G688
changed nothing" regression.

The compact machine report is
`Documentation/mm/sm-t311-rcu-off-object-diff-g694.json` including
object SHA256 hashes. The baseline test .o files were independently
built from G650 source and are NOT on the physical tablet. A fresh
setup can reconstruct those object files from the pinned commit.
The large temporary detached source worktree can be cleaned up
without losing the test-generated baseline objects.

Boundaries and outcome
----------------------
No executable kernel or Android source was changed in G694.
This comparison never ran on a physical Cortex-A9; only the older
G650 image is running on SM-T311, so physical A/B speedups cannot be
asserted. No Samsung boot.img or TWRP installation was produced,
and no backups or nighttime audio/camera/phone/PIN/OTG tests occurred.

G692's fail-closed no-reference RCU ON interlock remains necessary:
the old UNTAGGED->new TAGGED PFN-generation/ABA concern is unresolved,
as are HighMem/VIPT, Normal-zone pressure and global mmap GP costs.
S2/S3 stay blocked. The source diff vs physically installed G650
still consists of four executable source files STAGED NOT INSTALLED:
mm/filemap.c, mm/page_alloc.c, mm/shmem.c, mm/vma.c plus one
separate opt-in Kconfig overlay file.

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
