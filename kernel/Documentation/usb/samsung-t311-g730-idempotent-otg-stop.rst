Samsung SM-T311 — G730 idempotent OTG STOP and HSIC diagnostic
===============================================================

Actual device observations (2026-10-21)
---------------------------------------

The owner inserted a USB OTG-capable flash drive directly (no external
adapter), then saw cellular modem disappear briefly while ADB-over-Wi-Fi
stopped responding on TCP/5555. They toggled Wireless Debugging and
saw a new adb TCP port 39769 as well as restored cellular service.

Connected successfully to 192.168.2.238:39769 using ADB, then to
192.168.2.238:5555 too. On the device, **same adbd PID** listens on
both ports simultaneously. This does NOT indicate that TCP/5555 has
been replaced or requires a new hardcoding fix; Android Wireless
Debugging can expose a separate dynamic listener. Merely opening
another ADB listener is NOT expected to reinitialize the HSIC modem.

Live telephony: SIM LOADED, rild running, voice+data registered UMTS,
rmnet1 UP, Cellular network CONNECTED, VALIDATED with working link and
route. This independently proves a successful modem recovery. Kernel
history includes **multiple real USB 1-2 modem disconnects and
reenumerations** of 1519:0020/Comneon HSIC device 3,4,5 at kernel
uptime approximately 529,535,545 seconds. This provides evidence of
a physically bouncing modem USB path, but not proof that a specific
OTG/PHY operation or the later ADB Wi-Fi toggle caused it. Do NOT
restart/flash modems, send SMS/calls or touch SIM PIN as diagnosis.

Live OTG: extcon_usb=0, extcon_host=1, DEVICE PHY0 owners 0, HOST
PHY1 owners 1, both HCDs ready mask 0x6, MODE_SWITCH HOST,
MAX77693 input VBUS 0. But interlock blocked=1, safe_off=1,
fault_latched=0, **block_reason=6**, detach_edges=0, detach_ack=0,
PMIC BOOST0. Only 1519:0020 internal HSIC modem listed in Android
UsbHostManager, **no external USB flash and no /sys/block/sd?**.
This is not an Android filesystem mount issue. Block reason 6 in
source maps to explicit t311_otg_host_stop sysfs store; the current
kernel does not record WHICH process requested it or when. Do not
incorrectly infer the user pressed STOP.

Actual source bug addressed in G730
-----------------------------------

`drivers/power/supply/max77693_charger.c`
`t311_otg_host_stop_store()` previously unconditionally latched
`otg_user_blocked=true` and `otg_block_reason=6` for every
STOP request, even if no BOOST had ever been started and PMIC
was already fully verified OFF. A cleanup trap following an
unsuccessful or never-armed host session could therefore permanently
block future OTG. Repeated STOP also overwrote more valuable
existing reason codes (overcurrent, charger conflict, I2C fault).

G730 now treats STOP as **idempotent only when**:
 - I2C read of CHG_CNFG_00 succeeds, PMIC BOOST/OTG/DIS_MUIC_CTRL OFF;
 - no active BOOST, armed lease, host PHY1 lease or cleanup pending;
 - no unknown orphan boost, overcurrent latch, previous user block,
   or kernel shutdown.
If any criterion fails, old emergency STOP semantics remain, and
the watchdog/worker must verify that PMIC is actually OFF.
A prior fault reason is preserved across repeated STOP requests.

Source test:
`tools/testing/selftests/smt311/g730_otg_stop_idempotent.py`
25/25 source/model checks, plus prior G727 21/21 and G724 17/17.
Cross-compile changed MAX77693 charger ARM EABI5 PASS, full ARM32
zImage/vmlinux/DTB build and 8MiB test image packaged separately.

G730 does NOT rewrite physically active G727's interlock in RAM.
Its fix needs a new kernel, but physical installation, battery/5V
measurement, OTG source current verification and actual USB external
storage enumeration are NOT implied by a successful build.
Do not flash while the drive occupies the sole wired rescue path.
Preserve existing functioning cellular modem, USB PC ADB/MTP,
GNSS and telephony.
