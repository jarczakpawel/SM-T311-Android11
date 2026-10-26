#!/usr/bin/env python3
"""Compile actual Samsung SM-T311 stock K3DH function with selected K2DH,
check source+live modern IIO/Android HAL matrices and reject bad branches.

No device/network/API writes, fake axis vectors only; no personally-sensitive
sensor samples, no physical movement assertion.
"""
from pathlib import Path
import json
import re
import subprocess
import tempfile

LT=Path(__file__).resolve().parents[1]
ANDROID=LT.parents[2]
OEM=Path('/home/debian/Projekty/samsung/kernel-stock-samsung')
DEF=OEM/'arch/arm/configs/tab3_3G8_01_defconfig'
STOCK=OEM/'drivers/sensor/k3dh.c'
BOARD=OEM/'arch/arm/mach-exynos/midas-sensor.c'
HAL=ANDROID/'device/samsung/lt01-common/sensors/sensors_lt013g.cpp'
DTS=ANDROID/'kernel/samsung/lt013g/arch/arm/boot/dts/samsung/exynos4212-tab3.dtsi'
SNAP=Path('/home/debian/Projekty/samsung/artifacts/SM-T311-G399-K2DH-MATRIX-STOCK-20261017/G399-LIVE-IIO-MATRIX-PASSIVE.json')
EXPECT=[1,0,0,0,-1,0,0,0,-1]
def require(yes,message):
    if not yes: raise AssertionError(message)
def actual_function(stock):
    start=stock.index('static void k3dh_xyz_position_adjust(')
    finish=stock.index('/* Read X,Y and Z-axis acceleration raw data */',start)
    return stock[start:finish]
def stock_compiled_vectors(stock,enable_k2dh):
    func=actual_function(stock)
    header='#include <assert.h>\n#include <stdio.h>\n#include <stdint.h>\ntypedef int16_t s16;\nstruct k3dh_acc { s16 x,y,z; };\n'
    source=header+func+'''
int main(void) {
 const short in[][3]={{1,0,0},{0,1,0},{0,0,1},{1,2,3},{-2,4,-7}};
 for (int i=0;i<5;i++) {
  struct k3dh_acc acc={in[i][0],in[i][1],in[i][2]};
  k3dh_xyz_position_adjust(&acc,5);
  printf("%d,%d,%d\\n",acc.x,acc.y,acc.z);
 }
 return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='smt311-k2dh-selected-') as d:
        code=Path(d)/'source.c'
        exe=Path(d)/'stock'
        code.write_text(source)
        cmd=['cc','-std=c11','-O2','-Wall','-Wextra','-Werror']
        if enable_k2dh: cmd.append('-DCONFIG_SENSORS_K2DH')
        cmd.extend([str(code),'-o',str(exe)])
        cp=subprocess.run(cmd,capture_output=True,text=True)
        require(cp.returncode==0,'original stock C compile failed '+cp.stderr[:1500])
        run=subprocess.run([str(exe)],capture_output=True,text=True)
        require(run.returncode==0,'selected stock matrix function failed')
        return [[int(v) for v in line.split(',')] for line in run.stdout.splitlines()]
def parse_vendor_matrix(modern):
    m=re.search(r'kVendorAccelMatrix\s*\{\{([^}]+)\}\}',modern)
    require(m is not None,'modern live Android sensor HAL missing selected matrix')
    return [int(v.strip()) for v in m.group(1).split(',')]
def parse_dts_matrix(dts):
    match=re.search(r'lis3dh:\s*accelerometer@19\s*\{(.*?)\n\s*\};',dts,re.S)
    require(match is not None,'selected modern 3G8 accelerometer @0x19 absent')
    m=re.search(r'mount-matrix\s*=\s*([^;]+);',match.group(1))
    require(m is not None,'kernel does not declare accelerator mount matrix')
    return [int(v) for v in re.findall(r'"(-?\d+)"',m.group(1))]
def gate(t,real):
    require('CONFIG_TARGET_TAB3_3G8=y' in t['config'],'not OEM 3G8')
    require('CONFIG_SENSORS_K3DH=y' in t['config'],'stock K3DH not selected')
    require('CONFIG_SENSORS_K2DH=y' in t['config'],'CRITICAL: real stock K2DH macro not selected')
    require('position = 5; /* bottom/ upper-right */' in t['board'],'actual Tab3 8in board pos5 lost')
    require('#ifdef CONFIG_SENSORS_K2DH' in t['stock'],'selected OEM conditional missing')
    old=stock_compiled_vectors(t['stock'],True)
    expected=[[1,0,0],[0,-1,0],[0,0,-1],[1,-2,-3],[-2,-4,7]]
    require(old==expected,'exact selected K2DH stock 3G8 compiled axis rotation differs: '+repr(old))
    require(parse_dts_matrix(t['dts'])==EXPECT,
            'actual booted kernel selected DTS vendor XYZ orientation changed')
    require(parse_vendor_matrix(t['hal'])==EXPECT,
            'real Android11 HAL fallback axis matrix different from actual selected stock C')
    require('read_mount_matrix(ctx->accelerometer, kVendorAccelMatrix' in t['hal'] and
            'apply_matrix(ctx->accel_matrix' in t['hal'],
            'Android11 visible HAL not actually using selected IIO matrix')
    accel_raw=t['driver'][t['driver'].index('static int st_accel_read_raw('):
                          t['driver'].index('static void st_accel_invalidate_xyz_burst(',t['driver'].index('static int st_accel_read_raw('))]
    require('case IIO_CHAN_INFO_RAW:' in accel_raw and
            'st_sensors_read_info_raw' in accel_raw,
            'actual modern upstream raw IIO sensor path not intact')
    require(real['device']=='lt013g' and real['boot_completed']==1 and
            real['selinux']=='Enforcing' and real['sim_state']=='LOADED',
            'real owner G379 tablet state differs')
    require(real['kernel_accel_name']=='lis2dh12_accel',
            'real original 3G8 compatible accel IIO missing')
    require(sum(real['accelerometer_mount_matrix'],[])==EXPECT,
            'read-only physical IIO mount matrix differs from original source')
    require(real['modern_hal_sha256_device']=='0d59cb3f06cc4e701c1b3c005b44b39371a994a154f40855f40f7ffec6c66c46',
            'physical live HAL binary changed from known G379')
    require(not real['owner_dynamic_axes_accepted'],
            'unsupervised user physical rotation falsely marked PASS')
    require(real['firmware_physically_installed']=='G379' and
            real['new_signed_ota_staged_not_installed']=='G397',
            'wrong release/deployment state')
    return old
def main():
    names={
        'config':DEF,'stock':STOCK,'board':BOARD,'hal':HAL,'dts':DTS,
        'driver':ANDROID/'kernel/samsung/lt013g/drivers/iio/accel/st_accel_core.c'
    }
    t={k:f.read_text() for k,f in names.items()}
    evidence=json.loads(SNAP.read_text())
    vectors=gate(t,evidence)
    wrong_without_k2dh=stock_compiled_vectors(t['stock'],False)
    require(wrong_without_k2dh != vectors,
            'wrong K3DH-only macro unexpectedly has same old position5 transform')
    print('PASS real OEM selected source K2DH branch compiled, position5 unit XYZ vectors=',vectors)
    print('PASS K3DH-only wrong branch shown different:',wrong_without_k2dh)
    mutants=[
        ('wrong_stock_k2dh_flag','config','CONFIG_SENSORS_K2DH=y','CONFIG_SENSORS_K2DH=n'),
        ('wrong_stock_k3dh_flag','config','CONFIG_SENSORS_K3DH=y','CONFIG_SENSORS_K3DH=n'),
        ('wrong_model','config','CONFIG_TARGET_TAB3_3G8=y','CONFIG_TARGET_TAB3_3G10=y'),
        ('wrong_original_position5','board','position = 5; /* bottom/ upper-right */','position = 7; /* bottom/ upper-right */'),
        ('wrong_source_branch_selected','stock','#ifdef CONFIG_SENSORS_K2DH','#if 0 /* wrong K3DH branch */'),
        ('wrong_original_K2DH_matrix','stock','{{1,  0,  0}, { 0,  -1,  0}, { 0,  0, -1} }','{{0,  1,  0}, { 1,  0,  0}, { 0,  0, -1} }'),
        ('wrong_modern_hal_fallback','hal','kVendorAccelMatrix{{1, 0, 0, 0, -1, 0, 0, 0, -1}}','kVendorAccelMatrix{{0, 1, 0, 1, 0, 0, 0, 0, -1}}'),
        ('wrong_hal_apply','hal','apply_matrix(ctx->accel_matrix','apply_matrix(ctx->mag_matrix'),
        ('wrong_hal_mount_matrix_read','hal','read_mount_matrix(ctx->accelerometer, kVendorAccelMatrix','read_mount_matrix(ctx->accelerometer, kVendorMagMatrix'),
        ('wrong_dts_mount','dts','"1", "0", "0",\n\t\t\t\t  "0", "-1", "0",\n\t\t\t\t  "0", "0", "-1"','"0", "1", "0",\n\t\t\t\t  "1", "0", "0",\n\t\t\t\t  "0", "0", "-1"'),
        ('wrong_dts_addr','dts','accelerometer@19','accelerometer@1a'),
        ('no_3_axis_IIO_raw','driver','st_sensors_read_info_raw','unsupported_fake_raw'),
    ]
    for label,key,old,new in mutants:
        require(old in t[key], 'mutant wrong target '+label)
        src=dict(t)
        src[key]=src[key].replace(old,new) if label == "no_3_axis_IIO_raw" else src[key].replace(old,new,1)
        try:gate(src,evidence)
        except AssertionError:print('PASS rejected OEM branch/axis regression:',label)
        else:raise AssertionError('FAIL OPEN '+label)
    data_mutants=[
        ('wrong physical mount matrix','accelerometer_mount_matrix',[[0,1,0],[1,0,0],[0,0,-1]]),
        ('live other ROM falsely deployed','firmware_physically_installed','G399'),
        ('unverified dynamic physical rotation falsely accepted','owner_dynamic_axes_accepted',True),
        ('other model silently accepted','device','lt01wifi'),
    ]
    for label,key,val in data_mutants:
        evidence2=json.loads(json.dumps(evidence))
        evidence2[key]=val
        try:gate(t,evidence2)
        except AssertionError:print('PASS rejected unsafe physical inference:',label)
        else:raise AssertionError('FAIL OPEN '+label)
    print('G399 ACTUAL OEM 3G8 SELECTED PREPROCESSOR + LIVE MODERN ANDROID11/IIO MATCH PASS 16/16 bad changes rejected.')
    print('Owner physical 3axis dynamic orientation and sensor calibration remain OPEN; no kernel/ROM changed.')
if __name__=='__main__': main()
