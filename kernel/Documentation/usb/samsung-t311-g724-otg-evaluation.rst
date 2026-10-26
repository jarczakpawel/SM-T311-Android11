Samsung SM-T311 — G724 MAX77693 OTG host evaluation (TEST ONLY)
==============================================================

Status: SOURCE BUILT, NOT PHYSICALLY ENABLED.
------------------------------------------

The user verified laptop USB ADB and MTP both work on the currently booted
Linux 7.3.0-pjarczak G650. Previously connected OTG pendrive yielded
MAX77693 MUIC ADC GND `0x00` events, but no host enumeration or USB
mass-storage disk. The running `/proc/config.gz` explicitly has
`# CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL is not set`, while
`CONFIG_USB_STORAGE=y`, `CONFIG_EXTCON_MAX77693=y`, EHCI/OHCI on.

A source-level baseline for MAX77693 is Artur Weber's upstream patch series
developed during work on the *Galaxy Tab 3 8.0*:

* RFC cover: https://lkml.rescloud.iu.edu/hypermail/linux/kernel/2405.3/06812.html
* v4 06/10: https://lkml.rescloud.iu.edu/2408.2/00364.html
* v2 current-limit discussion:
  https://lkml.iu.edu/hypermail/linux/kernel/2407.3/01784.html

The kernel sequence for OTG power is **OTG | BOOST | DIS_MUIC_CTRL**
in MAX77693 CHG_CNFG_00. The v2 patch explicitly uses bit7 of
CHG_CNFG_02 to select 900mA OTG source current; this code DOES NOT
blindly enable that higher level. Current G724 kernel source performs
a *read-only* OTG current-limit preflight, refuses to source VBUS if
bit7 is set, and exposes bit7 in diagnostic sysfs. Actual VBUS and
overcurrent limits have still NOT been physically measured.

Source changes relative to G716 / G723
---------------------------------------

1. MUIC grounded-ID handler's I2C mux failure is propagated by
   `max77693_muic_adc_handler()`, not misreported as successful.
2. Failure from `extcon_set_state_sync(EXTCON_USB_HOST,...)` is now
   propagated; successful path unchanged.
3. MAX77693 charger explicitly rejects an existing higher-current
   OTG source bit, any charger input, external VBUS, stale HOST extcon,
   missing BYP IRQ, PHY0 gadget ownership, missing HCD root-hub
   readiness, wrong SYSREG MODE_SWITCH, and an armed timeout.
4. Read-only `t311_usb_role_diag` also reports
   `otg_ilim_known` and `otg_ilim_900ma`.
5. Existing fail-closed/overcurrent/reboot/cleanup machinery and a
   short root-only lease watchdog remain; there is no automatic
   VBUS boost from cable attach alone.
6. The G724 `sm_t311_g724_otg_eval_defconfig` explicitly selects
   CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL=y **ONLY FOR THE
   EXPERIMENTAL BRANCH**. G716 production defconfig and source unchanged.
7. The Android-side `tools/g724-otg-lab-supervisor-NOT-AUTORUN.sh`
   is a manual test tool only, *never* installed as a startup service.
   It refuses to arm while connected to a PC, without Wi-Fi ADB,
   without privileged access, unknown extcon/PHY/HCD/PMIC status,
   or without an explicit laboratory/electrical confirmation token.
   Once armed it renews the 3s lease; on exit/stop it requests verified
   OFF. It requires independently qualified physical VBUS and rescue
   checks to be used. **Do not execute its arm mode on the current tablet.**

Verified locally (not hardware acceptance)
------------------------------------------

* CONFIG=y ARMv7 cross-compiled MAX77693 charger, MAX77693 MUIC and
  Exynos4x12 USB PHY object files.
* Entire ARMv7 vmlinux, zImage and 3g8 DTB linked, valid ELF32 ARM.
* `python3 tools/testing/selftests/smt311/g724_otg_host_guards.py`:
  17/17 source guards PASS. A structural source check cannot prove
  hardware-safe timing or electrical output.
* Reuse the exact original G650 BOOT v0 header and ramdisk, known
  3g8 DTB, and 8MiB limit; isolated BOOT packer creates an artifact
  under `artifacts/G724-OTG-ARM32-TEST-ONLY/`. It MUST NOT overwrite
  G716 release BOOT nor Android's `out/boot.img`.

What is not implemented/qualified
----------------------------------

* Independent meter confirmation of 5 V output and 500mA current limit,
  short-circuit/BYP status, temperature, and load transients.
* The PC->OTG->PC transition must disable DWC2 device PHY0 before
  host-lease claim, without touching EHCI-HSIC XMM6262 or RIL/SIM.
  The current kernel gating intentionally refuses boosting while
  gadget PHY0 is occupied. Full automatic Android role handling is
  NOT YET QUALIFIED; do not pretend an isolated test boot is a full
  user-facing OTG implementation.
* Full TWRP entry and rescue path after flash are not reproducible.
  Without that, boot partition remains unchanged. The test artifact
  can only be staged inertly under /data/local/tmp.
* Physical USB drive enumeration, SCSI sdX and safe vold filesystem
  mounts, USB MTP reversion, reboot during host supply, unplug/short
  response, and sustained copy reliability remain not tested.

The next actual user-facing step is to unplug the laptop and plug
the OTG adapter and pendrive back in **only when asked**, maintaining
ADB Wi-Fi for read-only role-state capture. Never attach laptop and
a USB VBUS-sourcing OTG adapter at the same time. Do not silently
toggle MAX77693 BOOST while PC USB is attached.
