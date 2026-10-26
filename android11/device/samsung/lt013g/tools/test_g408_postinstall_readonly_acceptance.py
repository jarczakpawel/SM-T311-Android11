#!/usr/bin/env python3
"""G408 exact new postinstall shell with synthesized Android and ZRAM state.

Host only; no actual /sys writes, no ADB, no network, no modem.
"""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile

BASE=Path(__file__).resolve().parent
SCRIPT=(BASE/'g408_postinstall_readonly_acceptance.sh').read_text()
def insist(cond,msg):
    if not cond: raise AssertionError(msg)
for pattern in (
    r'(?m)^\s*(?:reboot|setprop|svc|settings|mount|umount|swapon|swapoff|mkswap|chcon|chmod|chown|flash_image|wipe|su)\s',
    r'(?m)^\s*dd\s+.*\bof=',
    r'\bip\s+route\s+(?:add|del|replace)\b',
    r'\b(?:curl|wget|ping|nslookup|logcat|dumpsys\s+location)\b'):
    insist(not re.search(pattern,SCRIPT),'unsafe postinstall operation '+pattern)
with tempfile.TemporaryDirectory(prefix='SMT311-G408-ZRAM-READONLY-') as tmp:
    root=Path(tmp);bin_dir=root/'bin';bin_dir.mkdir()
    gps=root/'gps';gps.write_bytes(b'MOCK_G408_ARM32_GPS_SHA')
    fstab=root/'fstab';fstab.write_text(
        '/dev/block/platform/soc/12550000.mmc/by-name/OTA /misc emmc defaults defaults\n'
        '/dev/block/zram0 none swap defaults zramsize=805306368,swapprio=32758,wait\n')
    (root/'p11').write_text('MOCK_BLK')
    (root/'disk').write_text('805306368\n')
    (root/'algo').write_text('lzo-rle lzo [lz4]\n')
    pc=root/'page-cluster';pc.write_text('0\n')
    swaps=root/'swaps'
    good_swap='Filename Type Size Used Priority\n/dev/block/zram0 partition 786428 124 32758\n'
    swaps.write_text(good_swap)
    def cmd(name,script):
        f=bin_dir/name
        f.write_text('#!/bin/sh\n'+script);f.chmod(0o755)
    cmd('getprop', 'case "$1" in\n'
        'ro.product.device) echo "$MOCK_MODEL";;\n'
        'ro.build.version.release) echo "$MOCK_ANDROID";;\n'
        'sys.boot_completed) echo "$MOCK_BOOT";;\n'
        'init.svc.vendor.lt013g-gps-legacy-v195) echo "$MOCK_GPSD";;\nesac\n')
    cmd('getenforce','echo "$MOCK_SELINUX"\n')
    cmd('ls', 'if [ "$1" = "-Zd" ]; then echo "$MOCK_LABEL $2"; else /bin/ls "$@"; fi\n')
    cmd('ip', 'if [ "$1" = "-4" ] && [ "$2" = "-o" ]; then '
        'printf "%s\\n" "$MOCK_ADDR"; '
        'elif [ "$1" = "-4" ] && [ "$2" = "route" ]; then '
        'printf "%s\\n" "$MOCK_ROUTES"; fi\n')
    source=SCRIPT
    replacements={
        'gps_bin=/system/vendor/lib/hw/gps.default.so':'gps_bin='+str(gps),
        'vendor_fstab=/vendor/etc/fstab.lt013g':'vendor_fstab='+str(fstab),
        'p11=/dev/block/mmcblk0p11':'p11='+str(root/'p11'),
        'want_gps=8cba224bb2f77a4d829b6ff7a4f1da1461357b0cad3eba9d704ae7e56e88db5b':
            'want_gps='+hashlib.sha256(gps.read_bytes()).hexdigest(),
        '/sys/block/zram0/disksize':str(root/'disk'),
        '/sys/block/zram0/comp_algorithm':str(root/'algo'),
        '/proc/swaps':str(swaps),
        '/proc/sys/vm/page-cluster':str(pc),
        '[ -b "$p11" ]':'[ -f "$p11" ]',
    }
    for orig,new in replacements.items():
        insist(orig in source,'missing fixture remap: '+orig)
        source=source.replace(orig,new)
    runner=root/'runner.sh'
    runner.write_text(source)
    baseline={
        'MOCK_MODEL':'lt013g','MOCK_ANDROID':'11','MOCK_BOOT':'1',
        'MOCK_GPSD':'running','MOCK_SELINUX':'Enforcing',
        'MOCK_LABEL':'u:object_r:misc_block_device:s0',
        'MOCK_ADDR':'2: rmnet1 inet 10.12.99.3/32 scope global rmnet1',
        'MOCK_ROUTES':'default via 10.12.99.1 dev rmnet1 table rmnet1\n'
                      '10.12.99.1 dev rmnet1 scope link table rmnet1'}
    def run(**env):
        e=os.environ.copy();e.update(baseline);e.update(env)
        e['PATH']=str(bin_dir)+':/usr/bin:/bin'
        return subprocess.run(['/bin/sh',str(runner),'--read-only'],
                              env=e,capture_output=True,text=True)
    def ok(got,label):
        insist(got.returncode==0 and 'FAIL ' not in got.stdout,
               label+': '+got.stdout+' '+got.stderr)
        insist('10.12.99.' not in got.stdout, 'network PII in output')
    def fails(got,marker,label):
        insist(got.returncode==1 and marker in got.stdout and
               'STOP: required exact firmware/boot/SELinux parity not accepted.' in got.stdout,
               label+': '+got.stdout+' '+got.stderr)
    good=run();ok(good,'fully healthy synthetic G408')
    for code in ('G408_ZRAM_768_MIB_DEVICE_INITIALIZED',
                 'G408_ZRAM_LZ4_KERNEL_COMPRESSION',
                 'G408_ZRAM_ACTIVE_IN_KERNEL_SWAPS',
                 'G408_ACTIVE_VENDOR_FSTAB_HAS_ZRAM_ONLY',
                 'G408_SWAP_READAHEAD_PAGE_CLUSTER_ZERO'):
        insist('PASS '+code in good.stdout,'healthy synthetic missing '+code)
    insist('PENDING OUTDOOR_STABLE_GPS_GLONASS_3D_REQUIRES_OWNER_TEST' in good.stdout,
           'owner GPS test incorrectly considered passed')
    print('PASS G408 healthy RAM, LZ4, swap, exact GPS and p11, cellular pending')
    disk=root/'disk';disk.write_text('0\n')
    fails(run(),'FAIL G408_ZRAM_768_MIB_DEVICE_MISSING','inactive disk')
    disk.write_text('805306368\n')
    print('PASS FAIL stale G379 disk=0')
    algo=root/'algo';algo.write_text('lzo-rle [lzo] lz4\n')
    fails(run(),'FAIL G408_ZRAM_LZ4_NOT_SELECTED','wrong compressor')
    algo.write_text('lzo-rle lzo [lz4]\n')
    print('PASS FAIL wrong active compressor')
    swaps.write_text('Filename Type Size Used Priority\n')
    fails(run(),'FAIL G408_ZRAM_NOT_ACTIVE','swap missing')
    swaps.write_text('Filename Type Size Used Priority\n/dev/block/zram0 partition 524284 124 32758\n')
    fails(run(),'FAIL G408_ZRAM_NOT_ACTIVE','wrong-size swap')
    swaps.write_text(good_swap)
    print('PASS FAIL missing and undersized swap')
    initial=fstab.read_text()
    fstab.write_text(initial.replace('/dev/block/zram0 none swap', '/dev/block/mmcblk0p21 none swap'))
    fails(run(),'FAIL G408_ACTIVE_VENDOR_FSTAB_ZRAM_INCORRECT','unsafe disk swap')
    fstab.write_text(initial)
    print('PASS FAIL eMMC swap')
    for env,marker in [
        ({'MOCK_MODEL':'lt01wifi'},'FAIL NOT_LT013G_MODEL'),
        ({'MOCK_SELINUX':'Permissive'},'FAIL GLOBAL_SELINUX_NOT_ENFORCING'),
    ]:
        fails(run(**env),marker,'original critical guard')
    print('PASS original model/SELinux guards')
    pc.write_text('3\n')
    fails(run(),'FAIL G408_SWAP_READAHEAD_PAGE_CLUSTER_NOT_ZERO',
          'read-only page cluster tuning absent')
    pc.write_text('0\n')
    print('PASS FAIL actual page-cluster not configured')
    arg=subprocess.run(['/bin/sh',str(runner)],capture_output=True,text=True)
    insist(arg.returncode==2,'read-only arg required')
    print('G408 POSTINSTALL READ-ONLY HOST CONTRACT PASS; no actual hardware changes')
