SM-T311 G697 — static built-in drivers and symbols before any RCU-OFF flash
============================================================================

Context
-------
G696 created a complete, format-verified Android boot v0 RCU-OFF
8MiB image on the Debian HOST, but it has NEVER been installed.
The physical tablet still runs G650 Linux 7.3.0-pjarczak and
requires dependable modem XMM6262, GPS BCM4752, audio, camera,
USB and Android interfaces.

The key question for G697 is narrower than global kernel ABI:
does removing the experimental no-ref code and CONFIG_DEBUG_FS
accidentally remove vital built-in driver feature gates or create
a new dependency on external kernel modules?

Physical PASSIVE evidence
-------------------------
An ADB shell, without writing or rebooting the tablet, observed:

- `/proc/modules` contained zero lines at the time of sampling;
- `CONFIG_MODULES=y`, `CONFIG_MODULE_UNLOAD=y`, and
  `CONFIG_MODVERSIONS` was disabled on physical G650;
- no .ko files were found in the accessible /vendor, /system,
  /odm or /product trees in that observation;
- Android remained boot_completed=1, RIL running, SIM LOADED.

This is a single observation under an unprivileged shell:
files in inaccessible private locations may exist, and future
loadable modules might be introduced. Directory names under
/sys/module often refer to **built-in** modules and are NOT
equivalent to a loaded external .ko.

Host configuration and binary comparisons
-----------------------------------------
The two full ARMv7 EABI5 host builds compared are:

- `../build/g686-reproducible-exp-on`: matching physical G650
  feature profile, but *new* source with G692 debugfs activation
  explicitly rejected;
- `../build/g687-release-rcu-off`: experimental no-ref reader
  and DEBUG_FS compiled OUT, candidate BOOT G696 built with it.

Both profiles have `CONFIG_MODULES=y` but **zero Kconfig
options with the =m value**, `CONFIG_MODVERSIONS=n`, and no .ko
objects in their build output. Twelve explicitly checked platform
feature switches all remain `=y`, including GNSS BCM4752,
XMM6262 modem, DWC2 USB, DesignWare MMC, ext4, FUSE, CMA,
ZRAM, ALSA core/SoC, HighMem and PREEMPT_RCU.

Independent global defined-symbol inventories from actual
`arm-linux-gnueabihf-nm -g --defined-only vmlinux` report:

  Experimental build defined globals: 26,742
  RCU-OFF build defined globals:      26,614
  Shared names:                       26,614
  Removed in release OFF:                128
  Newly introduced in release OFF:         0

All 26,614 globally defined symbol NAMES in release OFF are
present in the experimental comparison. This says NOTHING about
equal addresses, function layout, data structure size or actual
Android userspace ioctl contracts.

Among shared names:

  XMM6262-related (tab3_xmm6262):  29
  USB DWC2 (dwc2_):              112
  DesignWare MMC (dw_mci_):        8
  Broadcom WiFi (brcmf_):        183
  ALSA SoC (snd_soc_):           295
  Exynos-related (exynos):       151

No names from the XMM6262, Broadcom WiFi, DW MMC or Exynos
families were lost by the new release OFF comparator.

The 128 missing names include many debugfs functions and
subsystem-debugging entry points, with additional timekeeping
diagnostic helpers. Losing debugfs may impair diagnostics, even
if regular modem/USB/storage/audio functionality still links;
it would be incorrect to claim all 128 removed names are
irrelevant or that userspace ABI is fully compatible.

Executable regression
---------------------
Run:

  python3 tools/testing/selftests/smt311/rcu_builtin_driver_abi_g697.py

It reconstructs the two global linker symbol inventories,
compares actual .config and the G696 complete 8MiB candidate,
and records the prior passive device observation in the output.
It has 17 actual source/build/driver assertions, and deliberately
modifies nine simulated config/symbol/image/observation inputs;
all nine are rejected by the assertions.

Machine results:
`Documentation/mm/sm-t311-builtin-abi-g697.json`.

Crucial limits and deployment policy
------------------------------------
- No new executable kernel C is introduced by G697.
- The physical G650 live BOOT p9 hash has not been freshly read,
  and the G696 offline 8MiB image has not booted the tablet.
- Exported symbol names are not a verified module version
  CRC interface; CONFIG_MODVERSIONS is disabled anyway.
- GNSS and RIL are partly userspace services; equal Kconfig and
  selected kernel functions do NOT validate firmware bootstrap,
  device node permissions, SELinux, audio routing or modem wake.
- No signed TWRP installation, recovery preflight, rollback
  validation, backup, flash or Android reboot has occurred.
- Do not turn G692 staged experimental RCU ON back on until S2/S3
  are proven safe. Physical G650's existing runtime key remains
  unconfirmed because the shell cannot read debugfs.
- The project user's Android makefile still pins historical G541;
  do not silently redirect the Android build to G696.

Four earlier executable source changes remain STAGED NOT INSTALLED:
mm/filemap.c, mm/page_alloc.c, mm/shmem.c and mm/vma.c.
This result is STATIC COMPATIBILITY EVIDENCE, not approval to flash.

S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED;
S5 BLOCKED; S6 BLOCKED; S7 PARTIAL; S8 ACTIVE.
