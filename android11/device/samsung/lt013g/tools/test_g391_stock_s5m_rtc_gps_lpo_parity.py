#!/usr/bin/env python3
"""G391 exact SM-T311 stock 3G8 S5M8767 RTC/GNSS 32.768 kHz parity.

Source-only. No RTC clock/alarm setting, no GPS power toggles or user data.
The factory RTC and GNSS oscillator topology must survive future kernel work.
"""
import argparse
from pathlib import Path
import re

def check(ok,why):
    if not ok:raise AssertionError(why)

def extract_block(src,opening):
    start=src.find(opening)
    check(start>=0,'missing DTS node '+opening)
    pos=src.find('{',start)
    check(pos>=0,'DTS opening brace')
    depth=1
    i=pos+1
    while i<len(src) and depth:
        if src[i]=='{':depth+=1
        elif src[i]=='}':depth-=1
        i+=1
    check(depth==0,'unterminated '+opening)
    return src[pos+1:i-1]

def assert_contract(stockcfg,base,variant,gpsdrv):
    check(re.search(r'(?m)^CONFIG_TARGET_TAB3_3G8=y$',stockcfg),'non-SM-T311 stock defconfig')
    for conf in ('CONFIG_RTC_DRV_S5M=y','CONFIG_REGULATOR_S5M8767=y',
                 'CONFIG_GPS_BCM4752=y','CONFIG_S5M8767_OSC=y'):
        check(re.search('(?m)^'+re.escape(conf)+'$',stockcfg),'factory source selection lost '+conf)
    check('# CONFIG_GPS_BCM47521 is not set' in stockcfg,
          'dangerous sibling BCM47521 GNSS selected unexpectedly')
    pmic=extract_block(base,'s5m8767: pmic@66')
    check('compatible = "samsung,s5m8767-pmic";' in pmic,'wrong S5M8767 PMIC type')
    check('reg = <0x66>;' in pmic,'wrong factory I2C PMIC address')
    check('wakeup-source;' in pmic,'PMIC wake source missing')
    osc=extract_block(base,'s5m8767_osc: clocks')
    check('compatible = "samsung,s5m8767-clk";' in osc,'incorrect stock RTC/GPS oscillator')
    check('clock-output-names = "en32khz_ap",' in osc and
          '"en32khz_cp",' in osc and '"en32khz_bt";' in osc,
          'loss of stock AP/CP/BT 32768 Hz clock outputs')
    internal=extract_block(base,'&rtc')
    check('status = "disabled";' in internal,
          'broken internal Exynos RTC reenabled; must use S5M RTC')
    gps=extract_block(variant,'gps-power')
    check('compatible = "samsung,tab3-bcm4752";' in gps,
          'wrong sibling GPS power controller')
    check('enable-gpios = <&gpl2 2 GPIO_ACTIVE_HIGH>;' in gps,
          'factory GPS power pin changed')
    check('clocks = <&s5m8767_osc S2MPS11_CLK_AP>;' in gps and
          'clock-names = "lpo";' in gps,
          'GPS incorrectly loses stock S5M8767 AP 32768Hz LPO reference')
    check('devm_clk_get_enabled(dev, "lpo")' in gpsdrv,
          'GPS LPO not held on by active driver')
    check('devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW)' in gpsdrv,
          'GPS must begin powered down to avoid idle battery drain')
    check('32.768 kHz LPO clock' in gpsdrv,
          'driver documentation no longer reflects selected GPS oscillator')

def main():
    pa=argparse.ArgumentParser()
    pa.add_argument('--stock',type=Path,required=True)
    pa.add_argument('--modern-kernel',type=Path,required=True)
    a=pa.parse_args()
    stock=(a.stock/'arch/arm/configs/tab3_3G8_01_defconfig').read_text()
    base=(a.modern_kernel/'arch/arm/boot/dts/samsung/exynos4212-tab3.dtsi').read_text()
    variant=(a.modern_kernel/'arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dts').read_text()
    drv=(a.modern_kernel/'drivers/misc/samsung-tab3-bcm4752.c').read_text()
    assert_contract(stock,base,variant,drv)
    fixtures=[
      ('wrong_stock_GPS_variant',stock.replace('CONFIG_GPS_BCM4752=y','# CONFIG_GPS_BCM4752 is not set')),
      ('wrong_factory_RTC',stock.replace('CONFIG_RTC_DRV_S5M=y','# CONFIG_RTC_DRV_S5M is not set')),
      ('wrong_factory_PMIC',stock.replace('CONFIG_REGULATOR_S5M8767=y','# CONFIG_REGULATOR_S5M8767 is not set')),
    ]
    for name,variant_stock in fixtures:
        try:assert_contract(variant_stock,base,variant,drv)
        except AssertionError:print('PASS rejected unsafe factory config',name)
        else:raise AssertionError('FAILOPEN '+name)
    for name,b,v,d in [
        ('enable_broken_SOC_RTC',base.replace('status = "disabled";\n};\n\n&sdhci_2','status = "okay";\n};\n\n&sdhci_2'),variant,drv),
        ('wrong_PMIC_i2c_address',base.replace('s5m8767: pmic@66 {\n\t\tcompatible = "samsung,s5m8767-pmic";\n\t\treg = <0x66>;','s5m8767: pmic@66 {\n\t\tcompatible = "samsung,s5m8767-pmic";\n\t\treg = <0x67>;'),variant,drv),
        ('wrong_GPS_LPO',base,variant.replace('clocks = <&s5m8767_osc S2MPS11_CLK_AP>;','clocks = <&s5m8767_osc S2MPS11_CLK_BT>;'),drv),
        ('wrong_GPS_POWER_GPIO',base,variant.replace('enable-gpios = <&gpl2 2 GPIO_ACTIVE_HIGH>;','enable-gpios = <&gpl2 3 GPIO_ACTIVE_HIGH>;'),drv),
        ('missing_GPS_LPO_enable',base,variant,drv.replace('devm_clk_get_enabled(dev, "lpo")','devm_clk_get(dev, "lpo")')),
        ('wrong_GPS_power_on_probe',base,variant,drv.replace('GPIOD_OUT_LOW','GPIOD_OUT_HIGH')),
        ('drop_oscillator_AP_output',base.replace('clock-output-names = "en32khz_ap",','clock-output-names = "en32khz_disabled",'),variant,drv),
    ]:
        try:assert_contract(stock,b,v,d)
        except AssertionError:print('PASS rejected unsafe modern config',name)
        else:raise AssertionError('FAILOPEN '+name)
    print('G391 STOCK S5M RTC + BCM4752 GPS LPO PARITY PASS: 10/10 broken source mutations rejected; no clock/power/RTC writes')

if __name__=='__main__':main()
