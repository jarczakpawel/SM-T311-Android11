# SM-T311 stock-first parity ledger - G383 (2026-10-16)

Reference hardware: **SM-T311 / lt013g / Exynos4212 / 8-inch 3G**. NEVER treat source code for SM-T310, T315, Tab3 LTE, or 10-inch Tab3 as evidence that the SM-T311 binary uses it. An original stock source file is not evidence that it was selected by the original 3G8 defconfig. This ledger complements (not replaces) the **canonical** `chat-history/HARDWARE_FUNCTION_MATRIX.md`, which owns physical PASS/FAIL status.

## Exact Samsung factory selection - confirmed

`kernel-stock-samsung/arch/arm/configs/tab3_3G8_01_defconfig` explicitly enables `CONFIG_TARGET_TAB3_3G8=y`, `CONFIG_GPS_BCMxxxxx=y`, `CONFIG_GPS_BCM4752=y` and `CONFIG_SAMSUNG_MUIC=y`, but explicitly **disables** `CONFIG_GPS_BCM47521`.

The Samsung BSP Makefile selects `sec_gps_bcm4752.o` (factory GPS power/GPIO and serial) for this exact board. A tempting alternate file, `board-tab3-gps.c`, contains a `/dev/gps_geofence_wake` misc device, host-wake IRQ and `GPIO_GPS_HOST_WAKE=EXYNOS4_GPX1(5)`, but this is **not** the original chosen BCM4752 driver. The same stock GPIO header also assigns `GPX1(5)` to `GPIO_IF_PMIC_IRQ`. Android 11/6.12 currently uses GPX1(5) for the MAX77693 interrupt. Blindly importing the unselected geofence-wake IRQ could collide with PMIC/MUIC/USB charging. In the proprietary local GNSS executable, no literal `gps_geofence_wake` pathname was found during the bounded strings scan; this is corroborating context, **not** proof of every possible dynamic open. DO NOT add the wrong-model IRQ driver absent new board-specific physical proof.

Run: `python3 device/samsung/lt013g/tools/test_g383_stock_sm_t311_contract.py --stock /home/debian/Projekty/samsung/kernel-stock-samsung --modern-kernel /home/debian/Projekty/samsung/kernel` (PASS).

## Original Samsung function to modern Linux / Android implementation

All source paths are relative to either `/home/debian/Projekty/samsung/kernel-stock-samsung` (STOCK), `/home/debian/Projekty/samsung/kernel` (KERNEL) or `/home/debian/android/lineage-18.1-sm-t311` (ANDROID). Source presence/DT description does **not** prove hardware acceptance.

| Feature | Original factory source | Modern implementation / next bounded acceptance |
|---|---|---|
| Model, SoC, RAM banks, GPIO topology | STOCK `arch/arm/mach-exynos/mach-tab3.c`, `include/mach/gpio-rev00-tab3.h` | KERNEL `arch/arm/boot/dts/samsung/exynos4212-tab3.dtsi`, `exynos4212-tab3-3g8.dts`; preserve exact RAM hole and T311-specific GPIO ownership |
| Reset modes, recovery/charging | STOCK `arch/arm/mach-exynos/sec-reboot.c`, `cpu-exynos4.c` | KERNEL `drivers/power/reset/samsung-sec-reboot.c`: INFORM2, INFORM3, recovery=4 and 3x SWRESET aligned; software reboot recovery failed previously, investigate userspace shutdown -> kernel entry and pstore per mode, no speculative PMU register writes |
| GSM/UMTS and USB HSIC runtime | STOCK `drivers/misc/modem_if/`, `arch/arm/mach-exynos/mach-tab3.c` | KERNEL `drivers/misc/samsung-tab3-xmm6262.c`, `samsung-tab3-xmm6262-runtime.c`; Android private G368 RIL safely working for Plus data; calls/SMS/MMS incomplete, PIN/PUK and EFS untouched |
| GNSS GPS power/UART | STOCK `arch/arm/mach-exynos/sec_gps_bcm4752.c` chosen by exact 3G8 defconfig | KERNEL `drivers/misc/samsung-tab3-bcm4752.c`, T311 DTS; ANDROID `device/samsung/lt013g/gps-v195/legacy_launcher.cpp`; independent Binder mapped and GNSS processes running, SV/outdoor fix still open |
| GPS geofence host-wake | STOCK `arch/arm/mach-exynos/board-tab3-gps.c` exists for alternative board GPS implementation; **not** chosen `BCM4752` source | Do not port /dev/gps_geofence_wake or allocate GPX1(5); this pin is also the factory PMIC/MUIC IRQ |
| MAX77693 charger/MUIC and OTG | STOCK `arch/arm/mach-exynos/board-exynos4212-mfd.c`, `board-tab3-battery.c` | KERNEL T311 `exynos4212-tab3.dtsi`, `drivers/power/supply/max77693_charger.c`; verify current limits, thermal and USB accessory states from physical tests, preserve GPX1(5) |
| MAX17050 fuel gauge, battery safety | STOCK `arch/arm/mach-exynos/board-tab3-battery.c`, `drivers/battery/max17050_fuelgauge.c` | KERNEL `arch/arm/boot/dts/samsung/exynos4212-tab3.dtsi`; battery percentage alone is insufficient for charge/discharge-cycle accuracy |
| WM1811 codec / wired jack / mic-buttons | STOCK `arch/arm/mach-exynos/tab3-jack.c` (3-pole/4-pole ADC zones and 3 buttons) | KERNEL `arch/arm/boot/dts/samsung/exynos4212-tab3.dtsi` WM1811/jack ADC; 1024x4 playback previously validated; don't repeat closed test, mic/buttons still distinct final acceptance |
| MMS252 touchscreen and keys | STOCK `drivers/input/touchscreen/mms252.c`, `arch/arm/mach-exynos/tab3-input.c` | KERNEL `drivers/input/touchscreen/mms114.c`, T311 DTS; prior physical MMS252 fix validated, no unnecessary rework |
| LIS2DH12 accelerometer / YAS532 compass | STOCK `arch/arm/mach-exynos/mach-tab3.c` and sensor board GPIO descriptors | KERNEL `arch/arm/boot/dts/samsung/exynos4212-tab3.dtsi`, `drivers/iio/magnetometer/yamaha-yas530.c`; service registration does not prove calibrated heading |
| CM36653 ambient/proximity | STOCK `arch/arm/mach-exynos/mach-tab3.c`, board sensor/pinctrl | KERNEL `drivers/iio/light/cm36653.c`, T311 DTS; proximity physical near/far remains incomplete |
| MC96 infrared blaster | STOCK `arch/arm/mach-exynos/mach-tab3.c`, `drivers/irda/ir_remote_con_mc96.c` | KERNEL `drivers/media/rc/mc96-ir-tx.c`, T311 DTS; HIDL carrier capability verified, actual IR transmission needs receiver |
| Display FIMD/G2D and Mali | STOCK `arch/arm/mach-exynos/mach-tab3.c`, `drivers/gpu/drm/exynos/` | KERNEL `drivers/gpu/drm/exynos/`, T311 DTS, Android HWC/Mesa; prior frame/camera/GPU tests already accepted, no speculative plane/fence tweaks |
| Camera FIMC/ISP/MFC video | STOCK `arch/arm/mach-exynos/mach-tab3.c` plus legacy FIMC/MFC drivers | KERNEL `drivers/media/platform/samsung/exynos4-is/`; camera enumeration and H264 decode confirmed, full still/video/camera modes remain pending |
| BCM4334 Wi-Fi and Bluetooth | STOCK `arch/arm/mach-exynos/mach-tab3.c` Bluetooth registration, board SDIO GPIO | KERNEL T311 DTS regulator/SDIO UART; Wi-Fi connection accepted, BT actual pairing/profiles outstanding |
| Thermal and CPU/GPU DVFS | STOCK `drivers/thermal/exynos4_tmu.c`, board battery thermistors | KERNEL T311 DTS CPU/GPU OPP and devfreq; keep safe normal limits, long-term thermal and suspend tests pending |
| microSD/MMC, USB OTG and recovery | STOCK `arch/arm/mach-exynos/mach-tab3.c`, board SDHCI/USB definitions | KERNEL T311 DTS; microSD insert/eject and USB OTG accessories not yet verified |
| RTC/clock and suspend | STOCK `drivers/rtc/rtc-s5m.c`, Samsung pmic board setup | KERNEL T311 DTS RTC and S5M clock; deep-sleep/RTC alarm stability not established |
| Android 11 security, ART and low-RAM behavior | STOCK Android 3.x/4.x/7.1 reference implementation differs architecturally | ANDROID userdebug Lineage18.1/SDK30/ARM32, ZRAM LZ4, SELinux global Enforcing; RIL/GNSS vendor domains remain permissive, audited in canonical matrix |
| Recovery and OTA, GPL/OEM licensing | STOCK partition/fstab mapping on T311 `/boot`, `/system`, `/recovery` | G382 guarded full OTA signed and physically staged, **not installed**; TWRP rescue remains separate from broken Android reboot-recovery; never push private vendor binaries to public GitHub |

## Next priority, without repeating passed work

1. P0 release: physical G382 signed OTA installed **only from working recovery**, then confirm Android boot/audio/RIL/GNSS/SELinux. Staged ZIP and SHA are recorded in CURRENT_RELEASE and NEXT_TASK.
2. P0 reboot: correlate Android userspace log `reboot,recovery` vs kernel `T311_SEC_REBOOT_V163 cmd=recovery` and SWRESET; existing pstore cmd=shell normal reboot evidence does not prove recovery functionality.
3. P1 GNSS: outdoor SV/fix and private Binder MLS/AVC audit, then migrate vendor domain away from permissive. Do not transplant GPX1(5) geofence driver from unselected stock config.
4. P1 radio: no autonomous SIM PIN/PUK, SMS, paid calls or CP/EFS changes; owner-led telephony final.
5. P1 security/peripheral: microSD, BT, USB OTG, battery deep-sleep, jack mic/buttons and thermal acceptance remain distinct from HAL enumeration.

The canonical 20 project IDs are maintained separately in `chat-history/TASKS_CANONICAL.md` and `chat-history/HARDWARE_FUNCTION_MATRIX.md`.
