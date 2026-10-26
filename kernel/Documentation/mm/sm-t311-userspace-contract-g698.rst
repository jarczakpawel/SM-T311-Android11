SM-T311 G698 — Android/kernel RIL, GNSS, device nodes and SELinux
=================================================================

Purpose and test scope
----------------------
G696 produced a full, structurally verified but NOT INSTALLED Android
boot v0 image with optional no-reference RCU disabled. G697 compared
static built-in driver symbols and Kconfig. G698 instead follows the
actual Android 11 device tree's init, GNSS, radio, ueventd, DT and
SELinux paths back to the current kernel code, then checks a single
read-only passive physical G650 observation.

The source-bound verifier is:

  python3 tools/testing/selftests/smt311/rcu_userspace_contract_g698.py

Set `SMT311_ANDROID_DEVICE` to the Android device Git checkout
path when it differs from the original Debian workspace layout.
It reads real files in:
- kernel `drivers/misc/samsung-tab3-bcm4752.c`,
  `arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dts`,
  and both ARM32 .config profiles;
- Android `device/samsung/lt013g` radio/zz.t311-platform.rc,
  gps-v195/init.lt013g.gps-legacy.rc, gps/gps.xml,
  gps/manifest.gnss.xml and t311-custom-bootimg.mk;
- common `device/samsung/lt01-common` ueventd and SELinux
  genfs_contexts, file_contexts, lt013g_gps_legacy.te and
  lt013g_rild_access.te.

It asserts **20 current source + recorded device contracts**, and
runs **nine actual deliberate source/record mutations**, each
detected. The output is:
`Documentation/mm/sm-t311-userspace-contract-g698.json`.

Kernel / DT / GPS contract
--------------------------
Physical Exynos4212 DTS creates `gps-power` with compatible
`samsung,tab3-bcm4752`, enable GPIO and `lpo` clock. The
built-in `drivers/misc/samsung-tab3-bcm4752.c` attaches
`GPS_PWR_EN` and exposes `value` with mode 0664, specifically
documenting sysfs
`/sys/class/sec/gps/GPS_PWR_EN/value`. It also documents UART
`/dev/ttySAC1`.

Android GNSS legacy init and `gps/gps.xml` use the same sysfs path;
the legacy init changes /dev/ttySAC1 to owner root, group system,
mode 0660. Android ueventd and file_contexts name the same serial
device, SELinux grants `lt013g_gps_legacy` rw to the exact
`lt013g_gps_serial_device` chr_file type. A dedicated
`genfscon sysfs /devices/virtual/sec/gps/GPS_PWR_EN/value` labels
the power attribute `sysfs_lt013g_gps_power` and the same legacy
domain has an explicit allow for this sysfs file. The GPS launcher
uses a mount namespace, `u:r:lt013g_gps_legacy:s0` and stock
BCM4752 runtime, with Android GNSS 1.0 HIDL service separately.

At the physical G650 observation, `/dev/ttySAC1` was:
`crw-rw---- root system u:object_r:lt013g_gps_serial_device:s0`.
The legacy GPS service and GNSS HAL were both `running`.

The unprivileged ADB shell **could not read** the GPS power sysfs
attribute (permission denied). That is NOT evidence the
`lt013g_gps_legacy` domain is denied: shell and legacy GPS have
different SELinux contexts. Source contract cannot prove a GPS
fix or successful power-control transaction. Do not globally chmod
or relax SELinux just because shell cannot read an interface.

XMM6262 / radio ABI contract
----------------------------
Device tree contains `samsung,tab3-xmm6262` modem control;
both full ARM32 configs have `CONFIG_SAMSUNG_TAB3_XMM6262=y`.
Android `radio/zz.t311-platform.rc` initializes and labels
`/dev/umts_boot0` and `/dev/umts_ipc0` with DAC system:radio
0660, while common ueventd and file_contexts use the
`radio_device` SELinux type for /dev/umts*. It launches
cpboot-daemon and rild with `/vendor/lib/t311-ril/libsec-ril.so`,
and `/dev/ttyS0` plus scoped `u:r:rild:s0`.
On restart of userspace RIL, it stops only t311-audio-ril
without tearing down the modem cpboot service.

Physical device observation: both modem endpoints present as
system:radio `crw-rw----`, with `radio_device` type; ttyS0 is
owned by root with `serial_device` type. Boot, RIL, audio-RIL
and GPS services were running and SIM was `LOADED`.
No voice call, SMS, modem-firmware reload or active packet-data
test was performed.

Binder/USB and other boundaries
-------------------------------
Both kernel profiles retain serial Samsung, DWC2, BCM4752,
XMM6262, SOUND/ALSA and BINDERFS configs. Android uses
BinderFS with physical `/dev/binderfs/binder` labeled
`binder_device`. G696 RCU-OFF candidate leaves device DTB
byte-identical with G650.

This is not yet an end-to-end USB PC/OTG test or full Android
kernel ioctl, SELinux denial-log and uevent lifetime audit.
The Android makefile still pins historical G541, not G696.
The currently running G650 has optional no-ref code compiled in;
G692 fail-closed only exists in UNINSTALLED new sources.

Do not mistake this source comparison and one passive G650
observation for an RCU-OFF physical boot. The entire G696 8MiB
raw image remains a HOST-ONLY DO-NOT-FLASH artifact. No backup,
TWRP, reboot, OTA, radio/audio/GPS activity or physical p9
read/write occurred in G698.

Four executable kernel source paths STAGED NOT INSTALLED:
mm/filemap.c, mm/page_alloc.c, mm/shmem.c, mm/vma.c.
S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
