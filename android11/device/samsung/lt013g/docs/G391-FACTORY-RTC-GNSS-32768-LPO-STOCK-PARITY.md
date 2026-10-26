# G391 Samsung SM-T311 3G8: original S5M RTC and GNSS 32.768kHz LPO

Date 2026-10-17. Read-only audit of FACTORY stock, current Linux6.12 sources and running G379. GPS remains P1-11 first; no physical GPS fix proven.

## Exact stock comparator
Factory kernel-stock-samsung/arch/arm/configs/tab3_3G8_01_defconfig selects CONFIG_TARGET_TAB3_3G8=y, CONFIG_RTC_DRV_S5M=y, CONFIG_REGULATOR_S5M8767=y, CONFIG_S5M8767_OSC=y, CONFIG_GPS_BCM4752=y and excludes BCM47521.

Linux6.12 arch/arm/boot/dts/samsung/exynos4212-tab3.dtsi S5M8767 PMIC node on I2C address 0x66 has wakeup-source, s5m8767_osc with en32khz_ap/en32khz_cp/en32khz_bt outputs. Internal broken Exynos rtc status=disabled; factory S5M RTC is the required RTC0.

Exact exynos4212-tab3-3g8.dts BCM4752 GPS uses GPL2(2) GPIO and s5m8767_osc S2MPS11_CLK_AP 32.768kHz output named lpo; linux6.12 drivers/misc/samsung-tab3-bcm4752.c uses devm_clk_get_enabled for lpo, while default GPS PWR GPIO GPIOD_OUT_LOW. Do NOT swap GPS clock to BT/CP output, power it on by default, or re-enable nonworking Exynos RTC. Never port sibling BCM47521 geofence interrupt to PMIC MAX77693 GPX1(5).

## Physical passive check G379
Device real rtc0 name s5m-rtc s5m-rtc, hctosys=1, correct current year 2026, /proc/driver/rtc 24h and alrm_pending=no. Old alarm reports year2068 without current pending interrupt; do NOT change/reset it absent distinct evidence. G379 boot_completed1, SELinux Enforcing, SIM LOADED UMTS, GNSS legacy launcher running, GPS_PWR_EN0 idle. No RTC writes, GPS state change, coordinates/NMEA, time correction or SIM/APN data captured. This proves real RTC enumeration/boot-time use, NOT RTC alarm wake from suspend, NTP source, GPS3D fix or TTFF.

## Tests and status
New source-only Python tools/test_g391_stock_s5m_rtc_gps_lpo_parity.py uses exact factory defconfig, modern DTS and driver to test selected hardware, pin, enabled 32k LPO, S5M RTC and disabled Exynos RTC. 10/10 deliberately broken source configurations rejected: wrong original GNSS/RTC/PMIC, incorrect SoC RTC reenable, PMIC address, LPO channel, GPS power pin, missing enabled LPO, default-on GPS and dropped AP oscillator. No kernel/Android source binary change and no new 1GB ROM build needed.

Physical current G379 network remains missing netd gateway route (G387 repaired in SOURCE); signed guarded full G389 OTA already on tablet but NOT INSTALLED. G390 real TWRP read-only preflight code/source PASS; owner hardware TWRP necessary for G389 installation, then verify actual cellular DNS/NTP/SUPL, GNSS native time and outdoor GPS3D. Maintain all20 immutable GPS-first task IDs; public GitHub/license LAST, NEVER reinstall custom ADB plugin.

Physical G379 debugfs en32khz_ap rate/count files not readable: no measured oscillator frequency; only exact factory/modern source topology can be asserted. Old 2068 RTC alarm currently not pending and untouched.
