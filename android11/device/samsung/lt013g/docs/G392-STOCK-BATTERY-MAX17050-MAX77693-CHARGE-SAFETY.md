# G392 exact SM-T311 3G8 selected MAX17050/MAX77693 battery safety

Read-only source and actual physical G379 battery enumeration audit 2026-10-17. No battery/charger writes, no thermal stress and no new ROM.

## Genuine factory selection
The original kernel-stock-samsung/arch/arm/configs/tab3_3G8_01_defconfig enables TARGET_TAB3_3G8, MFD_MAX77693, FUELGAUGE_MAX17050 with COULOMB_COUNTING and CHARGER_MAX77693_BAT. It disables unrelated BATTERY_MAX77693_CHARGER option. Original board-exynos4212-mfd.c registers MAX77693 on I2C address0x66 with actual GPIO_IF_PMIC_IRQ. Original board-tab3-battery.c begins with an exact 3G8/8-inch conditional containing only empty exynos_tab3_charger_init and cable initial state. The later Samsung 7-inch sibling ADC thermistor table is in the ELSE block and does not describe selected 3G8 hardware. Do not port that table.

## Modern existing safety data
Linux6.12 tab3.dtsi MAX77693 at0x66 falling-edge wake GPX1(5) with MUIC and power supply, MAX17050 fuel at0x36 with GPX2(3) IRQ.
Charging source selected: 4.35V constant, minimum system3.60V, input threshold4.30V, USB500mA, AC1.8A, fast-charge 6h timer, top-off 250mA and40min, battery overcurrent3.5A.
Charger thermal regulation100C is its INTERNAL die thermal control, NOT a battery pack temperature safety limit. Independent fuel-gauge overheat property500 tenths C (50C) and overvoltage4500mV are source thresholds and do NOT prove hardware safety trip behavior.
Do not change these parameters without exact factory evidence and controlled safe validation. Never heat/overcharge a cell to test protections.

## Real G379 passive readings
max170xx_battery present1 health Good status Full, capacity100%, voltage4325156 microvolts (4.325V), temp268 (26.8C), charge_full4376000, charge_full_design4347000, cycle_count139. max77693-charger online1 present1 status Full health Good. Thermal zones max170xx_battery and cpu-thermal. Real software G379 boot1 Enforcing SIM LOADED GNSS power0. These are driver-reported values only, not an external battery characterization or evidence actual overheat/overcharge protection works.

## G392 source tests
tools/test_g392_stock_battery_charger_safety_parity.py selects exact factory 3G8 branch and active modern DT, rejects 15/15 deliberately wrong variants: wrong original fuel/charger/variant, sibling ADC table leakage, wrong original PMIC address, higher charge voltage or USB/AC current, removed fast timer, raised fuel overheat or overvoltage limit, wrong fuel type, PMIC and fuel IRQ and MUIC extcon. All PASS. Source/docs only, no need for another full 1GB Android ROM build. GPS3D/Internet/NTP/SUPL still await owner-controlled original TWRP entry, readonly G390 preflight, then cumulative G389 installation.

Keep immutable20 GPS-first tasks, and public GitHub/licensing LAST. Full G390 private archive transfer safety blocked, do not bypass; prior G389 full private source archive retained on tablet. No plugin reinstallation.
