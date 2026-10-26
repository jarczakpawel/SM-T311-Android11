SM-T311 G687: compare full release RCU-OFF kernel against experimental kernel
===========================================================================

Motivation
----------
The SM-T311 experimental G683 writer protects EXT4 VM_SHARED|MAYWRITE
mmap() with synchronize_rcu() even when the runtime static key is
OFF. It is therefore incorrect to assume switching the runtime key
OFF eliminates the experiment's mmap admission latency.

G687 evaluates a third path: do not compile the experiment into the
normal release at all. The tracked
arch/arm/configs/sm_t311_android11_defconfig already leaves
CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT, CONFIG_PAGE_RCU_FREE and DEBUG_FS
disabled for this ARM32 target.

Host-only reproduction
----------------------
G686's separately linked experimental comparator can be built using::

    bash tools/testing/selftests/smt311/build_verify_no_flash_g686.sh

The ordinary release profile is built in a NEW and separate ARM32
output directory from the tracked tablet defconfig::

    bash tools/testing/selftests/smt311/build_verify_release_off_g687.sh

The release gate tools/testing/selftests/smt311/rcu_release_off_g687.py
requires both linked outputs and validates Kconfig, exact
UTS_RELEASE "7.3.0-pjarczak", ARM EABI5 ELF and compressed zImage.

Proof of absence of this *particular* experimental code
-------------------------------------------------------
The ARM32 release vmlinux does not link either lockless_read_key or
lockless_read_set. It DOES retain the exported filemap_read_rcu
function, which in this configuration compiles to exactly two ARM
instructions (mov r0, #0; bx lr): a stub which always returns zero.

G687 discovered a real out-of-line call to this empty stub in the
ordinary filemap_read() hot path. The G687 mm/filemap.c change uses
an explicit #elif defined(CONFIG_PAGE_RCU_FREE) branch and sets
fast_read=0 directly if that option is disabled. An actual ARM
objdump of release filemap_read() proves this unnecessary call was
removed. The compiled experiment-on filemap_read() still calls
filemap_read_rcu() as designed. This is a bounded small performance
optimization, NOT a measurable percent gain without a device benchmark.

The shared-memory shmem_file_read_iter() in mm/shmem.c still has
a call to that release OFF stub. It is deliberately NOT changed by
G687; document it as a separate G688 optimization candidate.

The gate additionally checks that release mm/vma.o contains no
relocation to synchronize_rcu while experiment-on mm/vma.o has one,
proving G683's *extra* ext4 mmap GP call is excluded from release.
Do not conclude the upstream kernel never invokes synchronize_rcu
elsewhere; its symbol remains linked for unrelated users.

The ordinary mmap_region symbol is retained. The G687 gate checks
16 source/config/link conditions, seven ARM disassembly/callsite
conditions and rejects eight modified-source-symbol inputs plus
three altered-disassembly inputs. All output and sha256 hashes live
in Documentation/mm/sm-t311-rcu-release-off-g687.json.

Do NOT compare a runtime-OFF experimental kernel and the ordinary
release OFF kernel as if both had identical overhead. The former
still carries G683's per-new-ext4-mmap GP; the latter compiles it
out completely.

Lockdep scope and remaining blockers
------------------------------------
The source file mm/filemap.c describes the nested paths
mmap_lock -> i_mmap_rwsem / invalidate_lock -> page_table_lock
and i_rwsem -> mmap_lock on fault. In the experiment the new
synchronize_rcu() is performed while holding the current mm's
mmap_lock (in mmap_region), before publishing the new VMA.

The currently proposed no-ref filemap_read_rcu_batch takes
rcu_read_lock + pagefault_disable and, in the inspected function
body, does not explicitly acquire mmap_lock or sleep through
a page fault. This only excludes one obvious direct dependency.
Indirect callbacks, other RCU readers, global lockdep chains and
latency across all cores require separate proof. The G686/G687
normal and experimental configs both leave PROVE_LOCKING disabled;
no actual lockdep runtime analysis was performed on the tablet.

No global writer-admission rwsem or unverified GP skip is added in
G687. The G682/G683/G684/G685 LKMM tests show why counter, eligibility,
runtime OFF and late readers require more than naive predicates.

No new executable source modifications, no kernel flash/BOOT image,
no audio/camera/phone, no backup. Exactly three runtime source paths
(mm/filemap.c, mm/page_alloc.c, mm/vma.c) remain staged relative to
the physically running G650; the experimental fragment is build
configuration only. Future release decision must also account for
PFN ABA, ARMv7 VIPT/HighMem aliases, Normal-zone pressure and actual
end-user performance.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED;
S5 BLOCKED; S6 BLOCKED; S7 PARTIAL; S8 ACTIVE.


G688 supersession (new independent performance patch)
----------------------------------------------------
The historical note above saying shmem's release OFF zero-stub
call remains is now obsolete: G688 changes mm/shmem.c to skip
the release OFF stub while preserving upstream/experimental
config branches. See sm-t311-rcu-shmem-stub-g688.rst and JSON.
FOUR runtime source paths are now STAGED NOT INSTALLED, not three.
