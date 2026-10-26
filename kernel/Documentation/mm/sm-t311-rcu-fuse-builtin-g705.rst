G705 - FUSE in-tree module ABI and build artifact evidence
==========================================================

Samsung SM-T311, two already-linked ARM32 host builds, 2026-10-21.

G705 compares actual G686 experiment-enabled and G700 RCU-off full
host kernel images, .config, modules.builtin, and all tracked kernel
C/H files referring to struct fuse_conn or fuse_conn_init.

Results: 15/15 source/binary/config checks PASS, four independent
negative controls correctly detected altered inputs. Repeat with::

  python3 tools/testing/selftests/smt311/rcu_fuse_builtin_module_abi_g705.py

The machine-readable result is in sm-t311-rcu-fuse-builtin-g705.json.

Both profiles have CONFIG_FUSE_FS=y and CONFIG_FUSE_PASSTHROUGH=y.
Neither has any CONFIG_...=m symbols, nor a .ko file in the
corresponding build output. Both modules.builtin files list the path
kernel/fs/fuse/fuse.ko. This is a BUILT-IN object inventory marker,
not evidence that a separate fuse.ko binary was generated or loaded.

A full source-tree search found 20 C/H files mentioning
struct fuse_conn or fuse_conn_init; they all live inside fs/fuse.
Actual constructor calls occur in inode.c (active built-in FUSE),
cuse.c (CONFIG_CUSE=n), and virtio_fs.c (CONFIG_VIRTIO_FS=n).
The declaration is in fuse_i.h. The FUSE Makefile selects the
extra components only under the corresponding Kconfig choices.
This inventory is tied to the examined source revision; a future
external module or new driver is not covered.

fuse_conn_init is EXPORT_SYMBOL_GPL and its associated ksymtab
and string records are present in both linked ELF files. Both
profiles have CONFIG_MODULES=y and CONFIG_MODVERSIONS=n. Despite
no module files in these builds, it is NOT safe to claim binary ABI
compatibility for separately compiled third-party modules:
struct fuse_conn has a different conditional diagnostic field and
the internal layout differs, as G703 verified using ARM instructions.
The ELF symbols have common names but do not encode a proof of
matching struct layouts or calling context.

The explicit G704 decision to keep optional passthrough_opens
statistics disabled in G700 is preserved. No new Kconfig variant,
FUSE runtime change or linked BOOT was produced in G705.
Physical G650 still has not been replaced: live partition BOOT and
RECOVERY SHA-256 and a verified return route through TWRP are
not confirmed. G700 remains HOST ONLY, SHA-256
aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f,
DO NOT FLASH.

S0 PASS, S1 ACTIVE, S2-S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
G706: investigate any remaining release-sensitive ARM32 kernel
ABI outside FUSE without redundant global name-only checks;
continue RCU S1-S3 safety, and keep physical S6 recovery gating.
