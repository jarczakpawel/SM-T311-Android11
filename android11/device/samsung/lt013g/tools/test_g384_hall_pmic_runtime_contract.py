#!/usr/bin/env python3
"""G384: fail-closed source/physical pin ownership proof, not a Hall test.

The stock 3G8 Samsung source aliases GPX1(5) for Hall, MAX77693 and an
UNSELECTED BCM47521 GPS variant. This cannot be copied verbatim: the running
SM-T311 binds GPX1(5) to MAX77693 and a separate GPX2(4) to SW_LID.
Never claim physical lid open/closed response from input enumeration alone.
"""
import argparse
import json
import re
from pathlib import Path

def require(cond, why):
    if not cond:
        raise ValueError(why)

def verify(cfg, gpio, stock_button, dts, live):
    require(re.search(r"^CONFIG_TARGET_TAB3_3G8=y$", cfg, re.M), "not stock SM-T311 3G8")
    for flag in ("CONFIG_SENSORS_HALL=y", "CONFIG_GPS_BCM4752=y", "CONFIG_SAMSUNG_MUIC=y"):
        require(re.search("^"+re.escape(flag)+"$",cfg,re.M), "missing original 3G8 " + flag)
    require("# CONFIG_GPS_BCM47521 is not set" in cfg, "wrong GPS variant")
    require(re.search(r"#define GPIO_HALL_INT_N\s+EXYNOS4_GPX1\(5\)", gpio),
            "stock Hall GPIO source changed")
    require(re.search(r"#define GPIO_IF_PMIC_IRQ\s+EXYNOS4_GPX1\(5\)", gpio),
            "stock PMIC GPIO reference changed")
    require("SW_FLIP" in stock_button and "GPIO_HALL_INT_N" in stock_button,
            "stock Hall switch not described in board input")
    # Only parse the actual two DTS nodes; other references may legitimately
    # contain identical strings, including pinctrl state or comments.
    hall = re.search(r"switch-hall-sensor\s*\{(.*?)\};", dts, re.S)
    pmic = re.search(r"pmic@66\s*\{(.*?)\};", dts, re.S)
    require(hall is not None and pmic is not None, "missing modern Hall or PMIC node")
    require("gpios = <&gpx2 4 GPIO_ACTIVE_LOW>;" in hall.group(1),
            "Hall is no longer safely separated from PMIC GPIO")
    require("linux,input-type = <EV_SW>;" in hall.group(1) and
            "linux,code = <SW_LID>;" in hall.group(1),
            "Hall is not exported as Linux lid switch")
    require("wakeup-source;" in hall.group(1), "Hall wakeup capability lost")
    require("interrupt-parent = <&gpx1>;" in pmic.group(1) and
            "interrupts = <5 IRQ_TYPE_EDGE_FALLING>;" in pmic.group(1),
            "MAX77693 GPIO edge mapping changed")
    require(re.search(r"^\s*\d+:.*\bgpx1\s+5\s+Edge\s+max77693-", live, re.M),
            "live MAX77693 not owning gpx1-5")
    require(re.search(r"^\s*\d+:.*\bgpx2\s+4\s+Edge\s+hall effect sensor", live, re.M),
            "live Hall IRQ not separately registered on gpx2-4")
    require('N: Name="gpio-keys"' in live, "live gpio-keys missing")
    match = re.search(r'N: Name="gpio-keys".*?(?=\nI: Bus=|G384_INPUT_END)', live, re.S)
    require(match is not None and re.search(r"^B: SW=1\s*$", match.group(0), re.M),
            "live input lacks advertised SW_LID capability (switch code bit0)")
    require(re.search(r"G384_DEVICE_START\s+lt013g\s+1\s+Enforcing\s+G384_DEVICE_END",live),
            "live source is not booted enforcing lt013g")
    return dict(status="PASS_SOURCE_AND_PHYSICAL_DEVICE_ENUMERATION",
                stock_model="SM-T311 3G8", stock_hall="SW_FLIP legacy GPIO alias GPX1(5)",
                stock_gps="BCM4752 selected; BCM47521 disabled",
                modern_hall="SW_LID GPX2(4)", modern_pmic="MAX77693 IRQ GPX1(5)",
                hall_magnetic_action_tested=False,
                strict_gnss_selinux_tested=False,
                kernel_reboot_recovery_tested=False,
                no_hardware_writes=True)

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--stock",type=Path,required=True)
    p.add_argument("--kernel",type=Path,required=True)
    p.add_argument("--live",type=Path,required=True)
    p.add_argument("--json-output",type=Path)
    a=p.parse_args()
    st=a.stock/"arch/arm"
    config=(st/"configs/tab3_3G8_01_defconfig").read_text()
    gpio=(st/"mach-exynos/include/mach/gpio-rev00-tab3.h").read_text()
    btn=(st/"mach-exynos/tab3-input.c").read_text()
    dts=(a.kernel/"arch/arm/boot/dts/samsung/exynos4212-tab3.dtsi").read_text()
    live=a.live.read_text()
    report=verify(config,gpio,btn,dts,live)
    negatives={
        "stock_alt_gps_enabled":(config.replace("# CONFIG_GPS_BCM47521 is not set","CONFIG_GPS_BCM47521=y"),gpio,btn,dts,live),
        "modern_hall_on_pmic_gpio":(config,gpio,btn,dts.replace("gpios = <&gpx2 4 GPIO_ACTIVE_LOW>;","gpios = <&gpx1 5 GPIO_ACTIVE_LOW>;"),live),
        "modern_hall_missing_lid_code":(config,gpio,btn,dts.replace("linux,code = <SW_LID>;","linux,code = <KEY_HOME>;"),live),
        "live_wrong_pmic_pin":(config,gpio,btn,dts,live.replace("gpx1   5 Edge","gpx1   4 Edge")),
        "live_hall_missing":(config,gpio,btn,dts,live.replace("hall effect sensor","not a hall sensor")),
        "live_hall_capability_missing":(config,gpio,btn,dts,live.replace("B: SW=1","B: SW=0")),
        "live_not_booted":(config,gpio,btn,dts,live.replace("lt013g\n1\nEnforcing","lt013g\n0\nEnforcing")),
    }
    for name,x in negatives.items():
        try: verify(*x)
        except ValueError: continue
        raise AssertionError("FAIL_OPEN negative test: "+name)
    report["negative_controls_passed"]=len(negatives)
    if a.json_output:
        a.json_output.parent.mkdir(parents=True,exist_ok=True)
        a.json_output.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))

if __name__=="__main__":
    main()
