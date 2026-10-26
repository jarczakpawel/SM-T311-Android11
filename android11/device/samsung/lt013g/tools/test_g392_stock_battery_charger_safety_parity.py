#!/usr/bin/env python3
"""SM-T311 G392 selected Samsung 3G8 charger/fuel-gauge safety contract.

Source-only; does NOT change charger/fuel-gauge /sys, wakealarm or live
regulators. Rejects accidental import of alternate Tab3 battery ADC table
and mutations to existing battery/charger safety limits. Existing values
are SOURCE baseline, not independent cell safety certification.
"""
import argparse
from pathlib import Path
import re

def check(ok, what):
    if not ok:
        raise AssertionError(what)

def block(dts, begin):
    idx=dts.find(begin)
    check(idx>=0,'missing DT source block: '+begin)
    first=dts.find('{',idx)
    check(first>=0,'unclosed DTS declaration: '+begin)
    level=1
    pos=first+1
    while pos<len(dts) and level:
        if dts[pos]=='{':level+=1
        elif dts[pos]=='}':level-=1
        pos+=1
    check(level==0,'unclosed DTS brace: '+begin)
    return dts[first+1:pos-1]

def exact_prop(node,name,raw):
    pattern=r'(?m)^\s*'+re.escape(name)+r'\s*=\s*'+re.escape(raw)+r'\s*;'
    check(len(re.findall(pattern,node))==1,'wrong or duplicate '+name+' = '+raw)

def contract(defconfig, original_battery,original_mfd,modern):
    selections=(
        'CONFIG_TARGET_TAB3_3G8=y',
        'CONFIG_MFD_MAX77693=y',
        'CONFIG_FUELGAUGE_MAX17050=y',
        'CONFIG_FUELGAUGE_MAX17050_COULOMB_COUNTING=y',
        'CONFIG_CHARGER_MAX77693_BAT=y',
    )
    for v in selections:
        check(re.search(r'(?m)^'+re.escape(v)+r'$',defconfig), 'stock selected option missing: '+v)
    check('# CONFIG_BATTERY_MAX77693_CHARGER is not set' in defconfig,
          'alternate MAX77693 charger path selected unexpectedly')
    # Critical 3G8 original board branch ONLY registers trivial init and cable
    # flag. The remaining ADC thermistor table is compiled for other models,
    # not for exact SM-T311, and must not become an invented calibration.
    check(re.search(
        r'(?s)#if defined\(CONFIG_TARGET_TAB3_3G8\).*?'
        r'void __init exynos_tab3_charger_init\(void\)\s*\{\s*\}\s*'
        r'bool is_cable_attached = false;\s*#else',original_battery),
        'selected 3G8 charger branch no longer distinct from other Tab3 thermistor variants')
    check('static sec_bat_adc_table_data_t tab3_temp_table[]' in
          original_battery.split('bool is_cable_attached = false;',1)[1].split('#else',1)[1],
          'expected alternate-model thermistor table moved or missing')
    check('I2C_BOARD_INFO("max77693", (0xCC >> 1))' in original_mfd,
          'original MAX77693 PMIC address differs')
    check('GPIO_IF_PMIC_IRQ' in original_mfd,
          'original MAX77693 IRQ owner missing')
    pmic=block(modern,'pmic@66')
    exact_prop(pmic,'compatible','"maxim,max77693"')
    exact_prop(pmic,'reg','<0x66>')
    exact_prop(pmic,'interrupt-parent','<&gpx1>')
    exact_prop(pmic,'interrupts','<5 IRQ_TYPE_EDGE_FALLING>')
    check('wakeup-source;' in pmic,'charger/MUIC PMIC wake ownership lost')
    charger=block(pmic,'charger {')
    exact_prop(charger,'compatible','"maxim,max77693-charger"')
    exact_prop(charger,'charger-supply','<&charger_reg>')
    exact_prop(charger,'extcon','<&max77693_muic>')
    for name,raw in (
        ('maxim,constant-microvolt','<4350000>'),
        ('maxim,min-system-microvolt','<3600000>'),
        ('maxim,thermal-regulation-celsius','<100>'),
        ('maxim,battery-overcurrent-microamp','<3500000>'),
        ('maxim,charge-input-threshold-microvolt','<4300000>'),
        ('maxim,usb-current-microamp','<500000>'),
        ('maxim,ac-current-microamp','<1800000>'),
        ('maxim,fast-charge-timer-hours','<6>'),
        ('maxim,top-off-current-microamp','<250000>'),
        ('maxim,top-off-timer-minutes','<40>'),
    ):
        exact_prop(charger,name,raw)
    fg=block(modern,'fuel-gauge@36')
    exact_prop(fg,'compatible','"maxim,max17050"')
    exact_prop(fg,'reg','<0x36>')
    exact_prop(fg,'interrupt-parent','<&gpx2>')
    exact_prop(fg,'interrupts','<3 IRQ_TYPE_EDGE_FALLING>')
    exact_prop(fg,'maxim,over-heat-temp','<500>')
    exact_prop(fg,'maxim,over-volt','<4500>')
    # Existing 100 Celsius is IC thermal regulation, NOT battery pack
    # overheat cut-off (50.0C from the fuel gauge setting). Do not treat
    # either DTS value as tested battery protection operation.

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--stock',type=Path,required=True)
    p.add_argument('--modern-kernel',type=Path,required=True)
    a=p.parse_args()
    sc=(a.stock/'arch/arm/configs/tab3_3G8_01_defconfig').read_text()
    sb=(a.stock/'arch/arm/mach-exynos/board-tab3-battery.c').read_text()
    sm=(a.stock/'arch/arm/mach-exynos/board-exynos4212-mfd.c').read_text()
    md=(a.modern_kernel/'arch/arm/boot/dts/samsung/exynos4212-tab3.dtsi').read_text()
    contract(sc,sb,sm,md)
    variants=[
        ('wrong_factory_fuel_selection',sc.replace('CONFIG_FUELGAUGE_MAX17050=y','# CONFIG_FUELGAUGE_MAX17050 is not set'),sb,sm,md),
        ('alternate_factory_charger',sc.replace('CONFIG_CHARGER_MAX77693_BAT=y','# CONFIG_CHARGER_MAX77693_BAT is not set'),sb,sm,md),
        ('wrong_factory_variant',sc.replace('CONFIG_TARGET_TAB3_3G8=y','# CONFIG_TARGET_TAB3_3G8 is not set'),sb,sm,md),
        ('wrong_3g8_therm_branch',sc,sb.replace('CONFIG_TARGET_TAB3_3G8','CONFIG_TARGET_TAB3_3G10',1),sm,md),
        ('different_factory_PMIC',sc,sb,sm.replace('I2C_BOARD_INFO("max77693", (0xCC >> 1))','I2C_BOARD_INFO("max77693", (0xC8 >> 1))'),md),
    ]
    changes=[
        ('dangerously_higher_charge_voltage','maxim,constant-microvolt = <4350000>','maxim,constant-microvolt = <4500000>'),
        ('higher_USB_input_current','maxim,usb-current-microamp = <500000>','maxim,usb-current-microamp = <1500000>'),
        ('higher_AC_input_current','maxim,ac-current-microamp = <1800000>','maxim,ac-current-microamp = <2580000>'),
        ('lost_charge_timer','maxim,fast-charge-timer-hours = <6>','maxim,fast-charge-timer-hours = <0>'),
        ('lost_battery_overheat_threshold','maxim,over-heat-temp = <500>','maxim,over-heat-temp = <800>'),
        ('lost_battery_overvoltage_threshold','maxim,over-volt = <4500>','maxim,over-volt = <4600>'),
        ('incorrect_fuel_gauge_type','compatible = "maxim,max17050"','compatible = "maxim,max17042"'),
        ('dangerous_wrong_PMIC_irq','interrupts = <5 IRQ_TYPE_EDGE_FALLING>','interrupts = <4 IRQ_TYPE_EDGE_FALLING>'),
        ('lost_MUIC_extcon','extcon = <&max77693_muic>;','extcon = <&max77693_wrong>;'),
        ('fuel_gauge_wrong_IRQ','interrupts = <3 IRQ_TYPE_EDGE_FALLING>','interrupts = <2 IRQ_TYPE_EDGE_FALLING>'),
    ]
    for label,old,new in changes:
        check(old in md,'source baseline missing for mutant '+label)
        variants.append((label,sc,sb,sm,md.replace(old,new,1)))
    for label,tc,tb,tm,td in variants:
        try:contract(tc,tb,tm,td)
        except AssertionError:print('PASS reject unsafe source mutant:',label)
        else:raise AssertionError('FAIL OPEN: '+label)
    print('G392 STOCK SM-T311 CHARGING/FUEL/THERMAL SOURCE GATE PASS: 15/15 mutated layouts rejected; NO current or thermal setting changed; LIVE battery health requires separate evidence')

if __name__=='__main__':main()
