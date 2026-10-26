# G377 — private GNSS Binder must be initialized outside vendor GPS domain

**Date:** 2026-10-16. **Hardware:** Samsung SM-T311 BCM4752 + proprietary stock GPS daemon in an Android 11 mount namespace. **Status:** offline architecture finding; no phone flash.

## Direct stock + kernel evidence
Stock-era GNSS needs an isolated Binder service manager and must not access Android 11's global binder context. The current GPL-derived GNSS compatibility launcher mounts its own Binder filesystem at rootfs/dev/binderfs and starts the legacy servicemanager, which keeps independent binder devices. Linux documents that every newly mounted binderfs has independent binder IPC context and a binder-control device: https://kernel.org/doc/html/v5.12/admin-guide/binderfs.html

Android 11 system/sepolicy/private/domain.te contains an explicit NEVERALLOW block for mounting non-sdcard filesystems by virtually every domain, **including lt013g_gps_legacy**. The only exempt domains include init, apex and other tightly controlled privileged Android services. Line ~344 of the actual local policy. A narrow allow on binderfs:filesystem mount/unmount in lt013g_gps_legacy **FAILS build and violates this upstream neverallow**. Do not add such allow, modify neverallow, disable global Enforcing or grant blanket permissions.

## G377 negative build test
Temporarily staged a **T311-specific**, otherwise narrowly scoped allow binderfs:filesystem { mount unmount } and compiled m -j3 sepolicy sepolicy_neverallows. Job **1b3de5786f9c EXIT1** with exact upstream report:

libsepol.report_failure: neverallow on line 344 of system/sepolicy/private/domain.te violated by allow lt013g_gps_legacy binderfs:filesystem { mount }

Removed only the experimental, uncommitted rule and temporary BoardConfig inclusion. Normal source tree was restored and original Android11 neverallow maintained. Added permanent source guard device/samsung/lt013g/tools/test_g377_binderfs_neverallow_contract.py which asserts the policy block and rejects added vendor GPS binderfs mount access. Follow-on compile job **f4f88f5f2b31 EXIT0** for sepolicy + sepolicy_neverallows, physical previous policy SHA **2131d3f039b4c1c554d8a38a428b9a8ae5208f3f439ea7dd3d6d28f55f9ec2c2** unchanged; four permissible domains remain su, backuptool, rild, lt013g_gps_legacy.

## Candidate correct architecture, subject to detailed validation
The new private Binder filesystem must be created by **a service with mount permission allowed by upstream Android** (ideally a carefully scoped init-time action), not by the GNSS vendor application. Keep the binderfs mount private to the GPS jail rather than reusing Android's global servicemanager. A possible future approach is to provision the distinct binderfs instance in a sealed dedicated directory at init time, then pass a bind reference into the GPS service's private mount namespace before chroot; or restructure the stock bridge to avoid running a second binder context entirely if OEM behavioral evidence allows it. The trusted init action, DAC permissions, SELinux mountpoint labels, SELinux transitions, propagation, binder-control ioctl and cleanup on restart must all be individually verified **before** replacing the current service.

Do not assume a command in a vendor init RC runs in init's privileged domain: Android vendor init can use a restricted subcontext. The candidate must prove exact actor and namespace from Android 11 init source and VINTF context, and pass all neverallow checks.

## Known remaining steps
- G376 inherited /proc + /sys (including selinuxfs) via recursively bind-mounted host filesystems, already compiled ARM32 and tested host-side, but not physically installed. That removes three attempted newly created filesystem mounts from stock GNSS launcher.
- Private binderfs mount/unmount remains the GNSS-vendor-domain blocker. RIL is separate and cannot be switched to strict mode until CS voice, SMS ACK, MMS and Plus safety are accepted.
- Userdebug AOSP su and Lineage backuptool permissive are debug-variant-only; public user build must be compiled and verified separately.
- No permission broadening, no physical SIM/IMEI/EFS, no blind TWRP flash in G377.

## Relevant project proof
Canonical prior G376 handoff: chat-history/handoffs/SM-T311-G376-STOCK-GNSS-SELINUX-MOUNT-RELEASE-QA-20261016.md.
Source compiled GPL Linux kernel G373, protected Samsung RIL G368, Android11 G374 generic CarrierConfig still offline, master matrix HARDWARE_FUNCTION_MATRIX.md remains authoritative.
