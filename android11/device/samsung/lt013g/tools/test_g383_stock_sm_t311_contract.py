#!/usr/bin/env python3
"""Check EXACT stock SM-T311 3G8 selections before Samsung 3.x driver ports.

The Samsung GPL tree contains drivers intended for sibling Tab 3 boards.
Existence of source != being compiled on the original SM-T311 configuration.
"""
from pathlib import Path
import argparse
import re

p = argparse.ArgumentParser()
p.add_argument("--stock", type=Path, required=True)
p.add_argument("--modern-kernel", type=Path, required=True)
args = p.parse_args()
s = args.stock
m = args.modern_kernel

def content(root, relative):
    path = root / relative
    assert path.is_file(), f"Missing reference: {path}"
    return path.read_text(errors="replace")

config = content(s, "arch/arm/configs/tab3_3G8_01_defconfig")
mk = content(s, "arch/arm/mach-exynos/Makefile")
bcm4752 = content(s, "arch/arm/mach-exynos/sec_gps_bcm4752.c")
other = content(s, "arch/arm/mach-exynos/board-tab3-gps.c")
gpio = content(s, "arch/arm/mach-exynos/include/mach/gpio-rev00-tab3.h")
stock_pm = content(s, "arch/arm/mach-exynos/board-exynos4212-mfd.c")
modern_dts = content(m, "arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dts")
modern_dtsi = content(m, "arch/arm/boot/dts/samsung/exynos4212-tab3.dtsi")
modern_gps = content(m, "drivers/misc/samsung-tab3-bcm4752.c")
modern_reboot = content(m, "drivers/power/reset/samsung-sec-reboot.c")
stock_reboot = content(s, "arch/arm/mach-exynos/sec-reboot.c")

assert re.search(r"^CONFIG_TARGET_TAB3_3G8=y$", config, re.M)
assert re.search(r"^CONFIG_GPS_BCMxxxxx=y$", config, re.M)
assert re.search(r"^CONFIG_GPS_BCM4752=y$", config, re.M)
assert re.search(r"^# CONFIG_GPS_BCM47521 is not set$", config, re.M)
assert re.search(r"^CONFIG_SAMSUNG_MUIC=y$", config, re.M)
assert "obj-$(CONFIG_GPS_BCM4752)" in mk
assert "sec_gps_bcm4752.o" in mk
assert "gps_geofence_wake" in other  # different, unselected stock source
assert "gps_geofence_wake" not in bcm4752
assert 'gpio_export_link(gps_dev, "GPS_PWR_EN"' in bcm4752
assert "CONFIG_GPS_BCM47521" in gpio
assert re.search(r"#define GPIO_IF_PMIC_IRQ\s+EXYNOS4_GPX1\(5\)", gpio)
assert re.search(r"#define GPIO_GPS_HOST_WAKE\s+EXYNOS4_GPX1\(5\)", gpio)
assert ".irq_gpio" in stock_pm and "GPIO_IF_PMIC_IRQ" in stock_pm
assert "max77693_irq:" in modern_dtsi and 'samsung,pins = "gpx1-5"' in modern_dtsi
assert "interrupt-parent = <&gpx1>;" in modern_dtsi and "interrupts = <5 IRQ_TYPE_EDGE_FALLING>;" in modern_dtsi
gps_node = modern_dts.split("gps-power {", 1)[1].split("};", 1)[0]
assert "samsung,tab3-bcm4752" in gps_node
assert "enable-gpios = <&gpl2 2 GPIO_ACTIVE_HIGH>" in gps_node
assert "host-wake-gpios" not in gps_node
assert "gps_geofence_wake" not in modern_gps
assert "SAMSUNG_REBOOT_RECOVERY" in modern_reboot
assert "REBOOT_MODE_RECOVERY" in stock_reboot
assert "SAMSUNG_REBOOT_PREFIX | 4" in modern_reboot or re.search(r"#define SAMSUNG_REBOOT_RECOVERY\s+\(SAMSUNG_REBOOT_PREFIX\s*\|\s*4\)", modern_reboot)
assert "REBOOT_MODE_PREFIX | REBOOT_MODE_RECOVERY" in stock_reboot

print("G383 STOCK SM-T311 ONLY PASS: 3G8 BCM4752, no BCM47521 geofence port, MAX77693 GPX1-5 intact, reboot recovery mode mapped")
