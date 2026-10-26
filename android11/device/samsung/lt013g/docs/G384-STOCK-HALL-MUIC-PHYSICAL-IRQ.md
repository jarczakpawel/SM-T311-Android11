# SM-T311 G384 — stock Hall cover vs MAX77693: real Linux6.12 input/IRQ evidence

Date 2026-10-16. **Read-only physical diagnostic, not a kernel/OTA deployment.**

## Selected Samsung stock 3G8 configuration

The genuine `kernel-stock-samsung/arch/arm/configs/tab3_3G8_01_defconfig` enables `CONFIG_TARGET_TAB3_3G8=y`, `CONFIG_SENSORS_HALL=y`, `CONFIG_GPS_BCM4752=y`, `CONFIG_SAMSUNG_MUIC=y` and does not enable `CONFIG_GPS_BCM47521`. The old `tab3-input.c` describes Hall as `EV_SW/SW_FLIP` (in its non-flip-hack branch), using `GPIO_HALL_INT_N`. Stock header `gpio-rev00-tab3.h` aliases **both** `GPIO_HALL_INT_N` and `GPIO_IF_PMIC_IRQ` to Exynos GPX1(5). Unselected `board-tab3-gps.c` also references this pin for a different Broadcom GPS host-wake feature. Do NOT blindly merge those BSP paths: Kconfig inclusion, GPIO definitions, source and actual board pin ownership must all agree. It is not established that the old binary registered all of these conflicting GPIO users simultaneously.

## Live Linux6.12 hardware facts (physical G379)

Source: `arch/arm/boot/dts/samsung/exynos4212-tab3.dtsi` separates:
- MAX77693 PMIC/MUIC IRQ: `gpx1 5 IRQ_TYPE_EDGE_FALLING`, shared by max77693-led/topsys/charger/muic.
- magnetic cover: `gpio-keys/switch-hall-sensor` at `gpx2 4 GPIO_ACTIVE_LOW`, `EV_SW`, `SW_LID`, wakeup-source and debounce 10ms.

Verified on the **actual** SM-T311 (ADB, no writes): `/proc/interrupts` IRQ96 `gpx1 5 Edge max77693-led, max77693-topsys, max77693-charger, max77693-muic`; IRQ160 `gpx2 4 Edge hall effect sensor`. `/proc/bus/input/devices` shows `gpio-keys event4` with `B: SW=1`, representing Linux input switch capability bit0 `SW_LID`. Android `getevent -S -l /dev/input/event4` returned `0000` on this occasion (current switch bits clear, but no magnet was moved), and `settings get global lid_behavior` returned `0`.

**Critical:** Previously confirmed G293 rule says existing `lid_behavior=0` is a user setting that must be **preserved** even if the overlay default is 1. DO NOT change this setting autonomously or force tablet sleep on cover events. Real closed/open magnetic response, polarity, wake from sleep and Android policy are **not** accepted just because the input and IRQ enumerate correctly. Counts observed were zero and no magnet was applied.

## Regression gate and result

Run `python3 device/samsung/lt013g/tools/test_g384_hall_pmic_runtime_contract.py --stock /home/debian/Projekty/samsung/kernel-stock-samsung --kernel /home/debian/Projekty/samsung/kernel --live /home/debian/Projekty/samsung/artifacts/SM-T311-G384-STOCK-HALL-MUIC-20261016/G384-READONLY-LIVE-IRQ-INPUT.txt`.

Result: PASS stock/DT/live contract and **7/7 negative controls**, including wrong stock GPS variant, Hall reassignment to PMIC GPIO, wrong Linux switch code, missing physical IRQ and capability, and wrong device/runtime identity. This is specifically a **source and live enumeration acceptance** test, not the magnetic functional acceptance.

Privacy/safety: source probe reads only `/proc/interrupts`, `/proc/bus/input/devices`, model, boot/SELinux, `getevent -S` and lid preference. It never opens radio, sends SMS, touches EFS, changes a GPIO, tests PMIC by pulling USB or writes a firmware partition. The G382 signed guarded OTA remains physically staged **not installed**.
