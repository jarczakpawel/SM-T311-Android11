#!/usr/bin/env python3
"""Read-only source+redacted live 3G8 sensor contract and 12 wrong-device controls."""
import json
import re
from pathlib import Path

SRC=Path(__file__).resolve().parents[1]
ROOT=SRC.parents[2]
OEM=Path('/home/debian/Projekty/samsung/kernel-stock-samsung')
MODERN=ROOT/'kernel/samsung/lt013g/arch/arm/boot/dts/samsung'
EVIDENCE=Path('/home/debian/Projekty/samsung/artifacts/SM-T311-G398-SENSOR-OEM-IIO-20261017/G398-REAL-SENSORS-METADATA.json')

paths={
 'stock_config':OEM/'arch/arm/configs/tab3_3G8_01_defconfig',
 'stock_yas':OEM/'arch/arm/mach-exynos/midas-sensor.c',
 'stock_k3dh':OEM/'drivers/sensor/k3dh.c',
 'stock_midas':OEM/'arch/arm/mach-exynos/midas-sensor.c',
 'modern_dtsi':MODERN/'exynos4212-tab3.dtsi',
 'modern_3g8':MODERN/'exynos4212-tab3-3g8.dts',
 'modern_hal':SRC/'gps-v195/legacy-island-src/legacy_sensors_iio_v411.c',
}
def need(cond,label):
    if not cond: raise AssertionError(label)
def gate(t,live):
    for x in ('CONFIG_SENSORS_YAS532=y','CONFIG_SENSORS_CM36653=y',
              'CONFIG_SENSORS_K3DH=y','CONFIG_TARGET_TAB3_3G8=y'):
        need(x in t['stock_config'],'WRONG OEM 3G8 selected defconfig '+x)
    need('defined(CONFIG_TARGET_TAB3_3G8)' in t['stock_yas'] and
         'magnetic_pdata.orientation = YAS532_POSITION_7;' in t['stock_yas'],
         'OEM Tab3 8inch YAS532 layout not preserved as reference')
    need('I2C_BOARD_INFO("yas532", 0x2e)' in t['stock_yas'],
         'wrong original Yamaha I2C address')
    need('I2C_BOARD_INFO("cm36653", 0x60)' in t['stock_yas'],
         'wrong original Capella I2C address')
    need('I2C_BOARD_INFO("k3dh", 0x19)' in t['stock_yas'],
         'wrong original 3G8 K3DH I2C address')
    need('position = 5; /* bottom/ upper-right */' in t['stock_yas'],
         'original Tab3 8inch accel position5 lost')
    for x in ('compatible = "yamaha,yas532";','reg = <0x2e>;',
              'compatible = "st,lis2dh12-accel";','reg = <0x19>;',
              'capella,proximity-threshold-low = <8>;',
              'capella,proximity-threshold-high = <10>;'):
        need(x in t['modern_dtsi'] or x in t['modern_3g8'],
             'modern DTS lost exact selected sensor element '+x)
    need('compatible = "capella,cm36653";' in t['modern_3g8'] and
         'reg = <0x60>;' in t['modern_3g8'],
         'physical CM36653 I2C 0x60 does not match selected 3G8 DTS')
    for x in ('find_iio("cm36653"','find_iio("lis2dh12_accel"',
              'find_iio("yas532"','SENSOR_TYPE_LIGHT',
              'SENSOR_TYPE_PROXIMITY','SENSOR_TYPE_ACCELEROMETER',
              'SENSOR_TYPE_MAGNETIC_FIELD',
              'if (raw >= 10.0f)','else if (raw <= 8.0f)',
              'read_xyz(ctx->mag, "magn"',
              'read_named_float(ctx->cm36653, "in_illuminance_raw"',
              'scale_or(ctx->mag,"in_magn_x_scale"'):
        need(x in t['modern_hal'],'selected Android11 HAL sensor mapping mismatch '+x)
    need(live['device']=='lt013g' and live['boot_completed']==1 and
         live['selinux']=='Enforcing' and live['sim']=='LOADED',
         'not original real alive G379 tablet')
    need(live['iio_names']=={
        'iio:device0':'lis2dh12_accel',
        'iio:device2':'cm36653','iio:device3':'yas532'},
         'real IIO device set changed')
    need(live['android_hw_list']==[
        'CM36653 Ambient Light','CM36653 Proximity',
        'LIS2DH12 Accelerometer','YAS532 Magnetic Field'],
        'physical Android11 sensorservice does not list four actual sensors')
    need(live['android_virtual_geomagnetic_vector']==True and
         live['active_accel_events_observed']==True and
         live['active_ambient_light_events_observed']==True,
         'real Android11 active sensors baseline not established')
    need(live['compass_calibration_physically_verified'] is False and
         live['proximity_hand_cover_test_verified'] is False and
         live['accel_3d_orientation_manipulation_verified'] is False,
         'unjustified owner physical sensor acceptance')
    scale=live['yas532_magn_scale']
    need(len(scale)==3 and all(x==0.00001 for x in scale),
         'real Yamaha YAS532 raw IIO picotesla/Gauss-derived scale changed')
    need(live['yas532_mount_matrix']==[[-1,0,0],[0,1,0],[0,0,-1]],
         'modern Yamaha mount-matrix does not match real IIO sysfs')
    need(live['cm36653_prox_runtime_thresholds']==[8,10],
         'real CM36653 calibration thresholds altered')
    return True

def main():
    t={name:p.read_text() for name,p in paths.items()}
    live=json.loads(EVIDENCE.read_text())
    gate(t,live)
    bad=[
        ('wrong_stock_magnetic_chip','stock_config','CONFIG_SENSORS_YAS532=y','CONFIG_SENSORS_YAS532=n'),
        ('wrong_stock_proximity_chip','stock_config','CONFIG_SENSORS_CM36653=y','CONFIG_SENSORS_CM36653=n'),
        ('wrong_stock_accel_chip','stock_config','CONFIG_SENSORS_K3DH=y','CONFIG_SENSORS_K3DH=n'),
        ('wrong_stock_8in_compass_layout','stock_yas','magnetic_pdata.orientation = YAS532_POSITION_7;','magnetic_pdata.orientation = YAS532_POSITION_4;'),
        ('wrong_original_i2c_magnetometer','stock_yas','I2C_BOARD_INFO("yas532", 0x2e)','I2C_BOARD_INFO("yas532", 0x2f)'),
        ('wrong_original_i2c_proximity','stock_yas','I2C_BOARD_INFO("cm36653", 0x60)','I2C_BOARD_INFO("cm36653", 0x61)'),
        ('wrong_dts_chip','modern_dtsi','compatible = "yamaha,yas532";','compatible = "yamaha,yas537";'),
        ('wrong_dts_accel','modern_dtsi','compatible = "st,lis2dh12-accel";','compatible = "st,lis3dsh-accel";'),
        ('wrong_dts_light_low','modern_3g8','capella,proximity-threshold-low = <8>;','capella,proximity-threshold-low = <4>;'),
        ('HAL_mismatched_proximity','modern_hal','if (raw >= 10.0f)','if (raw >= 100.0f)'),
        ('HAL_wrong_accelerometer','modern_hal','find_iio("lis2dh12_accel"','find_iio("invalid_accel"'),
        ('HAL_missing_magnetic_raw','modern_hal','read_xyz(ctx->mag, "magn"','read_xyz(ctx->mag, "incompatible"'),
    ]
    for label,key,old,new in bad:
        need(old in t[key], 'bad fixture missing actual source location '+label)
        v=dict(t)
        v[key]=t[key].replace(old,new)
        try:gate(v,live)
        except AssertionError:print('PASS G398 rejects incorrect stock/IIO assumption:',label)
        else:raise AssertionError('G398 fail-open on '+label)
    cases=[
      ('wrong live IIO device','iio_names',{'iio:device0':'lis2dh12_accel','iio:device2':'cm36653','iio:device3':'yas537'}),
      ('unverified physical compass incorrectly accepted','compass_calibration_physically_verified',True),
      ('wrong measured magnetometer scaling','yas532_magn_scale',[0.001,0.001,0.001]),
      ('wrong physical YAS mount matrix','yas532_mount_matrix',[[1,0,0],[0,1,0],[0,0,1]]),
    ]
    for label,key,val in cases:
        x=json.loads(json.dumps(live))
        x[key]=val
        try:gate(t,x)
        except AssertionError:print('PASS G398 rejects unsafe real-world inference:',label)
        else:raise AssertionError('G398 fail-open on '+label)
    print('G398 GENUINE 3G8 OEM/ANDROID11 IIO PHYSICAL SENSOR PARITY PASS; 16/16 negative fixtures rejected')
    print('REMAINING owner-only: compass calibration and X/Y physical axis orientation, proximity near/far; NO sensor hardware interaction done.')
if __name__=='__main__':main()
