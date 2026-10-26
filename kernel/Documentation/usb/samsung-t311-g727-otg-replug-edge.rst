G727 — coalesced MAX77693 USB-HOST unplug/replug recovery, SM-T311
===============================================================

Actual hardware finding
-----------------------

While running G725 (Android11, Linux 7.3.0-pjarczak) a physical OTG
adapter + USB flash drive did not enumerate. The tablet's root-only
charger/PHY diagnostics proved:

* EXTCON_USB_HOST=1, EXTCON_USB=0, PHY0_DEVICE owners=0.
* PHY1_HOST owner=1, both EHCI/OHCI HCD ready mask=0x6,
  SYSREG MODE_SWITCH=HOST, external input VBUS absent.
* MAX77693 OTG current selector bit7 present (stock), bit6 absent.
* Interlock: blocked=1, armed=0, BOOST=0, PMIC safe_off=1.
* No external USB device, no SCSI USB disk or vold USB mount.

The owner physically unplugged/reinserted the adapter on 2026-10-21.
MUIC kernel log recorded **both actual detach and attach**:
`external connector is detached (adc:0x00,...)` followed by
`external connector is attached (adc:0x00,...)`.
However, charger interlock *remained blocked=1*. Do not again ask the
owner to reseat solely to "clear" a state that the driver ignored.

Source-level root-cause candidate
---------------------------------

The MAX77693 MUIC calls `extcon_set_state_sync` immediately.
`max77693_charger_extcon_event` previously performed ONLY a
`schedule_work(&chg->extcon_work)`. Workqueues coalesce repeated work:
a detach and later reattach can be delivered to the charger worker
as a single final HOST=1 snapshot, without the HOST=0 branch running.
That branch is the only normal place where otg_user_blocked was cleared.
Thus even a physically verified replug cannot guarantee that state
machine progressed through its safe-off/latched-reset branch.

Fix: observe the HOST falling edge in the extcon notifier and save a
monotonic atomic counter, AFTER having seen at least one HOST=1.
The notifier itself does not access PMIC, clear any safety latch or
enable BOOST. The worker is allowed to consume a stored detach when
HOST has already reattached, ONLY AFTER fresh PMIC OFF/readback,
no upstream VBUS, no EXTCON_USB gadget, no charger current, BYP IRQ
unmasked/registered, and no active BOOST/pending cleanup, overcurrent
latch or uncertain I2C recovery. It then clears the generic user block,
and LEAVES `otg_user_armed=false`: a separate, privileged 3-second
lease is still required to source VBUS.

Fault states (BYP_I overcurrent, unreadable regmap, I2C cleanup, active
BOOST, input power) remain blocked. Preserving an overcurrent latch
even when notifier coalesces events is intentionally conservative:
one must NEVER use a diagnostic to bypass that latch.

Diagnostics extended
--------------------

Read-only `t311_otg_interlock` now additionally exposes:

* `detach_edges` and `detach_ack` to prove a captured/consumed edge.
* `fault_latched` combining BYP overcurrent and uncertain orphan BOOST.
* `block_reason`: 0 clear, 1 PMIC/I2C unknown, 2 USB/charger conflict,
  3 pending cleanup, 4 source-enable reject, 5 lease watchdog,
  6 manual stop, 7 BYP fault.

The USB/charger conflict branch now logs its reason with rate limiting.
This makes it possible to distinguish a coalesced HOST detach from
a *new* on-attach charging conflict or real hardware fault, without
ever reading private phone or storage data.

Verification and deployment state
---------------------------------

Cross-compilation with explicit localversion 7.3.0-pjarczak:
ARMv7 `vmlinux`, `zImage`, SM-T311 3g8 DTB PASS.
`tools/testing/selftests/smt311/g727_otg_replug_edge.py`:
21/21 source-anchored toy-model checks PASS.
Previous G724 source fail-closed checks: 17/17 PASS.
Neither set of tests proves physical VBUS voltage, output-current
limits, data transfer, hub enumeration, or short-circuit reliability.

Test-only Android BOOT v0, original G650 ramdisk and original known
DTB, exact 8 MiB:
`artifacts/G727-OTG-ARM32-TEST-ONLY/BOOT-G727-OTG-EXPERIMENTAL-8MiB-DO-NOT-FLASH.img`
SHA256 `dcc7aafe81bb947e9afb4c8d1c4d49bbbeb413f65164c014cb02d392ae57f9bc`.

This image has been transferred ONLY as an INERT file to
`/data/local/tmp/BOOT-G727-OTG-EXPERIMENTAL-8MiB-DO-NOT-FLASH.img`
and verified on-device, mode 0600.
**Active BOOT remains G725**, SHA256
`56249bdd0fe20235d2ebe4c3da027b9c13c45ddc7f66eb28a5f1e1c06501a063`,
and continues to boot Android with SELinux Enforcing.
No BOOT/RECOVERY flash, USB 5V, phone/SIM/PIN/GPS test, backup or
reboot took place during G727.

Next: only after safe return path for local USB and rescue and a
supervised electrical VBUS test, install G727 and retest once, reading
`detach_edges`, `detach_ack`, `block_reason`, external enumeration,
USB storage and vold mount. If the conflict latch recurs, the specific
cause now appears in read-only telemetry rather than being guessed.
