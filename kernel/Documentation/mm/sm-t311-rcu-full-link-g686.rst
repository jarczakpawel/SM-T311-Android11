SM-T311 G686: fully linked ARMv7 experiment and activation-drain decision
=========================================================================

Goal
----
Advance from independently compiled mm/filemap.o, mm/page_alloc.o and
mm/vma.o to a complete host-linked ARM32 kernel, while preserving the
exact kernel release string `7.3.0-pjarczak` and tracing its inputs
back to Git for eventual publication.

This is a HOST BUILD ONLY. The resulting zImage is NOT a Samsung BOOT
partition or a signed TWRP OTA. It has not been flashed to the tablet.
The runtime no-ref RCU ON correctness proof remains incomplete.

Reproduce from tracked source
-----------------------------
The ordinary tracked arch/arm/configs/sm_t311_android11_defconfig
does not enable the optional RCU experiment. That is intentional for
ordinary SM-T311 builds.

The tracked experimental overlay
arch/arm/configs/sm_t311_rcu_experiment_g686.fragment enables
CONFIG_DEBUG_FS, CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT and
CONFIG_PAGE_RCU_FREE and pins CONFIG_LOCALVERSION="-pjarczak".
The build script starts with the tracked standard defconfig, applies
this overlay using scripts/config, resolves Kconfig dependencies using
olddefconfig and compiles the entire kernel in an independent host
build directory.

Run inside the Debian workspace::

  bash tools/testing/selftests/smt311/build_verify_no_flash_g686.sh

It performs a real GNU make ARM32 zImage build, verifies the active
Kconfig options and ELF32/EABI5 vmlinux and the linked ARM zImage,
checks every symbol required by the experiment, checks that exactly
THREE executable source paths differ from the physical kernel
source a2fca4558, and then runs all G660-G685 regression gates
and the S0-S8 stage gate.

Fresh tracked-defconfig build result
------------------------------------
The independent build directory ../build/g686-reproducible-exp-on was
initialized by the G686 script from tracked sm_t311_android11_defconfig
plus tracked sm_t311_rcu_experiment_g686.fragment. It successfully
compiled and linked the complete ARMv7 kernel from scratch (not merely
an incremental link of older objects), then passed all 17 regressions.

Verified fresh-build SHA256:

  vmlinux: c0a36fb9716c93f44ea4cbba5d370699e2e58f5bf8913baa96fdf1ee22c9786b
  zImage:  bea602b0cb12d2b8c096e703594211eb0627b5ba1af976dbd4ccb80255f02104

The linker produced ELF32 ARM EABI5 vmlinux and ARM compressed zImage;
both report UTS_RELEASE 7.3.0-pjarczak. The exact three changed mm
objects were checked fresh relative to their source timestamps and
included in the linked image. The new output manifests are recorded
in the G686 JSON and script; the earlier incremental G633 SHA values
in the project report are separately identified as historical build
artifacts, NOT the final clean rebuilt image.

Artifacts and SHA256 from the completed run live in
Documentation/mm/sm-t311-rcu-linked-image-g686.json. The build
directory and compiled binaries are host artifacts and are not
committed to Git (rebuild from source instead).

Correcting the kernel release suffix
------------------------------------
The first G686 host link gave UTS_RELEASE 7.3.0-pjarczak+ even though
CONFIG_LOCALVERSION_AUTO was not set. Linux's
scripts/setlocalversion adds a trailing "+" for SCM state when the
LOCALVERSION shell/make variable is UNSET and the source is not on an
exact suitable annotated tag. With LOCALVERSION= (set to the empty
string), Kbuild omits that suffix while retaining the committed
CONFIG_LOCALVERSION="-pjarczak" string.

The G686 host image now reports exactly
UTS_RELEASE "7.3.0-pjarczak" in the generated header and
include/config/kernel.release. The reproducible shell recipe always
uses explicit LOCALVERSION=.

The attestation source gate contains 5/5 positive predicates and
7/7 input mutations rejected (lost linker symbols, wrong config
profile, incorrect release name and wrong staged-runtime list).
All three current ARM32 runtime objects must have modification
times no older than their source; both the final vmlinux and zImage
must be newer than the objects they contain. It also checks that the
current three runtime source paths are clean in Git.

Publication/lock decision: G686 does NOT add a global rwsem
-----------------------------------------------------------
For the speculative optimization of ext4 writable VMA publication,
a writer cannot simply observe the current runtime static key OFF
and omit its GP. G682/G683/G684 established separate abstract
publication hazards involving different mmap_lock instances,
AS_RCU_READ eligibility and a concurrent OFF->ON transition.
G685 added a late reader key recheck under its RCU critical section.

A safe prospective solution for the off-path cost needs an
admission/drain mechanism with ALL of the following obligations:

1. New ext4 shared-maywrite writers must register BEFORE choosing
   their OFF/ON mode. That registration must not race with a toggle.
2. OFF->ON must hold off publication of newly enabled no-ref
   readers until every admitted old OFF writer has completed or
   rolled back its VMA publication. A toggle-side RCU GP alone does
   NOT drain those writers.
3. Writers admitted after ON must publish writable count before
   their protection grace and VMA visibility, accounting for ALL
   in-flight writers in different mm_structs.
4. ON->OFF must stop admitting readers, then safely drain old and
   delayed readers; a pre-RCU caller gate alone is insufficient.
   G685's inner RCU check closes one modelled late-entry case, but
   the global weak-memory proof remains incomplete.
5. The drain protocol must remain correct across fork, VMA split,
   mmap failure and callback paths; maintain reference lifetimes
   and cache coherency.
6. A new rwsem or similar admission gate in mmap_region would be
   acquired while the caller holds mmap_lock. Before implementing,
   its complete lockdep and callbacks/RCU interactions must be
   verified. Introducing a global sleepable lock blindly risks
   contention or lock inversion on the two Cortex-A9 cores.

There is NO complete verified implementation of these six
obligations. G686 intentionally does not introduce a global lock,
remove G683's mandatory ext4 GP, or alter RCU runtime behavior.

Two defensible directions therefore remain: prove and measure an
activation-drain protocol; or exclude the experimental RCU config
from the normal production defconfig (already the tracked default).
The G683 GP cost with runtime key OFF is still an unresolved
performance blocker for an experiment-enabled production build.

Safety boundary and physical state
----------------------------------
S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.

Exactly THREE executable kernel source files remain changed versus
physically booted G650 source a2fca4558:

  mm/filemap.c    G674/G680/G685
  mm/page_alloc.c G676
  mm/vma.c        G680/G683

These are STAGED NOT INSTALLED. Physical SM-T311 remains on
7.3.0-pjarczak G650. No backups, OTA/BOOT repack, flash,
TWRP reboot, nighttime audio/camera/calls or PIN automation
were performed. GitHub publication is the owner's task.


G688 scope update (historical G686 hashes retained)
--------------------------------------------------
The G686 manifest's earlier three paths and hash values represent
its *historical build*, not the G688 current runtime. G688 adds
mm/shmem.c to the STAGED NOT INSTALLED list. New host output hash
values are available under sm-t311-rcu-shmem-stub-g688.json, and the
G686 attestation/rebuild recipe now recognizes four runtime paths.
