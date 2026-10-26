#!/usr/bin/env python3
"""G724 Linux T311 USB-host fail-closed release gate (SOURCE verification only).

Not a test of physical 5V; never calls ADB, I2C, sysfs writes or flashes.
"""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[4]
C=(ROOT/"drivers/power/supply/max77693_charger.c").read_text()
MUIC=(ROOT/"drivers/extcon/extcon-max77693.c").read_text()
HDR=(ROOT/"include/linux/mfd/max77693-private.h").read_text()
KCONF=(ROOT/"drivers/power/supply/Kconfig").read_text()
DTS=(ROOT/"arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dts").read_text()
BUILD=Path("/home/debian/Projekty/samsung/linux-7.3-port/build/g724-otg-boost-gated-host-only/.config")
CFG=BUILD.read_text() if BUILD.exists() else ""

def segment(t,beg,end):
    return t.split(beg,1)[1].split(end,1)[0]

ENABLE=segment(C,"static int max77693_t311_otg_enable(", "/* Run BEFORE normal charger current decisions. */")
HANDLE=segment(C,"static bool max77693_t311_otg_handle(", "/*\n * The Samsung 3.0.31 board charger")
MUIC_HOST=segment(MUIC,"static int max77693_muic_adc_ground_handler(", "static int max77693_muic_jig_handler(")
MUIC_ADC=segment(MUIC,"static int max77693_muic_adc_handler(", "static int max77693_muic_chg_handler(")
CLAIM=segment((ROOT/"drivers/phy/samsung/phy-exynos4x12-usb2.c").read_text(),
              "static int exynos4x12_t311_host_claim(", "static int exynos4x12_t311_power_on(")

CHECKS={
"experimental_only_default_off":'default n' in KCONF and 'config CHARGER_MAX77693_T311_OTG_EXPERIMENTAL' in KCONF,
"only_T311_with_explicit_DTS_optin":'of_machine_is_compatible("samsung,t311")' in C and "t311,otg-boost-experimental;" in DTS,
"optin_build_never_modifies_main_release":'CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL=y' in CFG,
"no_active_no_ref_RCU":'CONFIG_PAGE_RCU_FREE=y' not in CFG,
"MUIC_path_error_propagated": "ret = max77693_muic_adc_ground_handler(info)" in MUIC_ADC and "if (ret < 0)" in MUIC_ADC,
"EXTCON_host_publish_error_propagated": "ret = extcon_set_state_sync(info->edev, EXTCON_USB_HOST" in MUIC_HOST and "if (ret < 0)" in MUIC_HOST,
"strict_HOST_PHY_not_gadget": "drv->instances[EXYNOS4x12_DEVICE].ext_cnt != 0" in CLAIM and
    "drv->t311_hcd_ready_flags" in CLAIM and "EXYNOS_4x12_MODE_SWITCH_HOST" in CLAIM,
"no_boost_without_explicit_user_arm": "!chg->otg_user_armed" in ENABLE and "otg_user_armed = true" in C and "max77693_t311_otg_host_refresh" not in ENABLE,
"usb_upstream_power_guard": "MAX77693_STATUS2_VBVOLT_MASK" in ENABLE and "EXTCON_USB" in ENABLE,
"no_unqualified_otg_current_enabled": "CHG_CNFG_02_OTG_UNQUALIFIED_MASK" in HDR and
    "current_limit_mode & CHG_CNFG_02_OTG_UNQUALIFIED_MASK" in ENABLE and
    "return -EOPNOTSUPP;" in ENABLE,
"current_limit_inspection_no_set": "otg_ilim_known=%u otg_ilim_900ma=%u otg_ilim_unqualified=%u" in C,
"charger_interlock": "chg->regulator_enabled" in HANDLE and "regulator_disable(chg->charger_regulator)" in HANDLE,
"pmic_on_readback": "max77693_t311_otg_mode_is_on(conf)" in ENABLE and "chg->otg_cleanup_pending = true" in ENABLE,
"overcurrent_latched": "otg_overcurrent_latched" in HANDLE and "max77693_t311_otg_bypass_irq" in C,
"no_unsafe_auto_restart": "otg_user_blocked = true" in C and "otg_lease_deadline" in C and "otg_watchdog_work" in C,
"no_boost_during_REBOOT": "max77693_t311_otg_reboot_notify" in C and "SYS_RESTART" in C,
"fail_closed_to_off_before_release": "max77693_t311_otg_disable(chg)" in HANDLE and
    "samsung_usb2_phy_t311_host_release" in C
}
for n,ok in CHECKS.items():
    print(("PASS" if ok else "FAIL")+" "+n)
print(f"G724 SOURCE HOST-GUARD PASS={sum(CHECKS.values())}/{len(CHECKS)}")
if not all(CHECKS.values()):
    raise SystemExit(2)
