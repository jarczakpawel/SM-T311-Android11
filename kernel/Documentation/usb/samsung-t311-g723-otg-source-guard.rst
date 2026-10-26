SM-T311 G723 — MAX77693 grounded-ID USB OTG mux errors
=======================================================

Scope
-----

An actual physically inserted OTG adapter was inspected over ADB Wi-Fi
(192.168.2.238:5555), without USB PC tether. The active G650 kernel
reports `CONFIG_USB_STORAGE=y`, `CONFIG_EXTCON_MAX77693=y` and
`# CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL is not set`
from physical `/proc/config.gz`. MUIC kernel log contains three
`adc:0x00` attached events and two detached events during the
observed interval. MAX77693 ADC ground indicates OTG ID grounded,
but this alone does not prove stable, published EXTCON_USB_HOST.

Android UsbHostManager reports only the internal HSIC XMM6262 modem;
no external USB device. No USB SCSI block disk. The `disk:179,0`
is an MMC device, not evidence of a USB flash drive. `vold` runs.

The source already provides a gated experimental MAX77693 OTG boost
coordinator with preflight, explicit user arming, PHY lease, BYP fault
handling, charger exclusion and watchdog. Production G716 config
intentionally leaves it OFF. USB DWC2 DTS is explicitly peripheral.
Enabling PMIC 5V here without a confirmed recovery path and current
limiting could damage hardware, especially if host-role edges bounce
or a charger/PC is connected. **Do NOT enable it automatically**.

Concrete code bug fixed
-----------------------

`max77693_muic_adc_handler()` previously discarded the return from
`max77693_muic_adc_ground_handler(info)`. On a failed MAX77693
AP_USB mux I2C write, the caller returned zero, misleading higher
level MUIC IRQ reconciliation as successful processing. G723
propagates the negative return; the successful path remains
identical. This is a real source error-propagation fix but does NOT
switch on VBUS/BOOST or make unpowered OTG functional by itself.

Isolation and verification
--------------------------

G723 is a **separate Git worktree and branch**:
`experimental/G723-otg-muic-return-guard`,
`smt311-g723-otg-muic-return-guard`.

The G716 branch `upstream-v7.3` and its SHA-pinned clean BOOT
artifact are unchanged. The changed G723 `drivers/extcon/
extcon-max77693.c` compiled successfully as an ELF32 ARM EABI5
object under the copied G716 kernel configuration at
`build/g723-muic-otg-return/drivers/extcon/extcon-max77693.o`.
This is NOT a linked/booted kernel and is not physical acceptance.

Next
----

1. Keep USB Wi-Fi accessible; do not repeat functional tests that the
   user has declared working (voice calls, SMS, MTP, ADB, GNSS, UI).
2. With an OTG cable connected, the read-only Android helper
   `tools/g723-otg-passive-intake.sh` (in device repository) reports
   actual MUIC ID logs, rootless UsbHostManager device inventory and
   SCSI disks. Shell cannot read some SELinux-protected descriptors,
   so do not equate permission-denied with no device.
3. For complete OTG: prepare an independent supervised electrical
   qualification (VBUS 5V, current limit, overcurrent, charging
   exclusion, PHY1 host/mux ownership, HUB/pendrive enumeration,
   USB PC-mode and modem exclusion, detach and I2C-fault rollback)
   before enabling any experimental boost in a test-only kernel.
4. No data wipe, recovery reboot, SIM/PIN, modem reset, SYSTEM or BOOT
   partition write, nor backup was performed in G723.
