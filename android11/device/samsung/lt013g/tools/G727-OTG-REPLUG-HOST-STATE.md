# G727 — USB OTG replug edge: actual kernel still G725, source fix pending activation

**21 Oct 2026.** Samsung SM-T311 physical adapter/pendrive detached and reinserted. Root ADB Wi-Fi `192.168.2.238:5555` logs verify both MAX77693 MUIC ID GROUND detach and attach. **Do not demand the owner redo the same reseat simply to prove he did it.**

Physical G725 active: `extcon_usb=0 extcon_host=1`, USB PHY0 device owners0, PHY1 host owner1, EHCI/OHCI 0x6, mode switch HOST, `otg_ilim_900ma=1 otg_ilim_unqualified=0`. But `blocked=1 armed=0 pmic_boost=0 safe_off=1` stays latched. No external USB drive, no SCSI disk or vold USB mount. Only disk `179,96` MMC. Not a formatting or MTP issue.

New isolated G727 kernel branch commit `68d0456de`, project `G727_OTG_REPLUG_EDGE_FIX_20261021.md`, adds atomic capture of true HOST falling edges in MAX77693 extcon notifier. This prevents a rapid unplug/replug from being hidden by coalesced `schedule_work`; resetting a generic nonfault latch occurs ONLY on fresh verified PMIC safe-off, no charger/USB/VBUS, no BYP/I2C fault, no cleanup pending, no active BOOST. It cannot bypass an overcurrent latch and never automatically powers the port. A new diagnostic reports `detach_edges`, `detach_ack`, `block_reason`, `fault_latched`.

Full ARM32 build G727 `vmlinux`, `zImage`, 3g8 DTB PASS, 21 source/model checks + 17 previous safety guards PASS. Exact 8MiB G727 BOOT SHA `dcc7aafe81bb947e9afb4c8d1c4d49bbbeb413f65164c014cb02d392ae57f9bc` physically copied and verified **ONLY as inactive** `/data/local/tmp/BOOT-G727-OTG-EXPERIMENTAL-8MiB-DO-NOT-FLASH.img`. Active BOOT p9 STILL G725 SHA `56249bdd0fe20235d2ebe4c3da027b9c13c45ddc7f66eb28a5f1e1c06501a063`. No actual G727 hardware VBUS test, NO OTG fix PASS yet.

**Next:** supervised USB-PC connectivity for a G727 install when owner ready; reboot without laptop currently loses manual Wi-Fi ADB and USB port occupied by adapter. On G727 boot check new read-only reason telemetry, then electrical/overcurrent-qualified VBUS activation/USB flash enumeration. Do not write 5V PMIC registers while tablet remains connected to PC; do not silently create backups; do not disturb user-confirmed working calls, SMS, MTP, ADB, GNSS, UI.

## S0–S8
S0 PASS Android11 G725; S1–S3 RETIRED old RCU; S4 REMOVED; S5 PARTIAL G725 boots but G727 not deployed; S6 PARTIAL recovery; S7 ACTIVE OTG not powered/enumerated, other subsystems user PASS; S8 ACTIVE sources, test, Git.

## FIZYCZNIE NA TABLECIE
G725 running, OTG adapter present, root Wi-Fi ADB running, interlock blocked=1 and safe_off=1, no disk. G727 test BOOT on /data only. No reboot/flash/battery/PMIC changes in G727.
