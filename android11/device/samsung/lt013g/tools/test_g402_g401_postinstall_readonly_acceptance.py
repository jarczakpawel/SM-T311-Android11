#!/usr/bin/env python3
"""Isolated synthetic execution of the exact read-only G401 acceptance shell.

Never calls ADB, network, modem, GNSS, or actual system partitions.
"""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile

BASE=Path(__file__).resolve().parent
SOURCE=(BASE/'g402_g401_postinstall_readonly_acceptance.sh').read_text()
def require(ok,why):
    if not ok: raise AssertionError(why)
for unsafe in (r'(?m)^\s*(?:reboot|setprop|svc|settings|mount|umount|chcon|chmod|chown|flash_image|wipe|su)\s',
               r'(?m)^\s*dd\s+.*\bof=',r'\bip\s+route\s+(?:add|del|replace)\b',
               r'\b(?:curl|wget|ping|nslookup|logcat|dumpsys\s+location)\b'):
    require(not re.search(unsafe,SOURCE),'unsafe source operation '+unsafe)
require('/dev/block/mmcblk0p11' in SOURCE and
        'cfe1ba47542d723577c5b2ef3f14841a24946b4022f631ef271a5c1cb26e9521' in SOURCE,
        'G402 does not target actual G401 and factory p11')
with tempfile.TemporaryDirectory(prefix='SMT311-G402-READONLY-FIXTURE-') as d:
    root=Path(d); b=root/'bin'; b.mkdir()
    gps=root/'modern-gps';gps.write_bytes(b'G401 synthetic GNSS ELF bytes only')
    fstab=root/'vendor-fstab'
    fstab.write_text('/dev/block/platform/soc/12550000.mmc/by-name/OTA /misc emmc defaults defaults\n')
    (root/'p11').write_bytes(b'SYNTHETIC-BLOCK-NO-REAL-HARDWARE')
    def mkcmd(name,code):
        p=b/name;p.write_text('#!/bin/sh\n'+code);p.chmod(0o755)
    mkcmd('getprop',"""case "$1" in
ro.product.device) echo "$MOCK_MODEL";;
ro.build.version.release) echo "$MOCK_ANDROID";;
sys.boot_completed) echo "$MOCK_BOOT";;
init.svc.vendor.lt013g-gps-legacy-v195) echo "$MOCK_GPSD";;
esac
""")
    mkcmd('getenforce','echo "$MOCK_SELINUX"\n')
    mkcmd('ls',"""if [ "$1" = "-Zd" ]; then
 printf '%s %s\n' "$MOCK_LABEL" "$2"
else
 /bin/ls "$@"
fi
""")
    mkcmd('ip',"""if [ "$1" = "-4" ] && [ "$2" = "-o" ]; then
 printf '%s\n' "$MOCK_ADDR"
elif [ "$1" = "-4" ] && [ "$2" = "route" ]; then
 printf '%s\n' "$MOCK_ROUTES"
fi
""")
    source=SOURCE
    remap={
      'gps_bin=/system/vendor/lib/hw/gps.default.so':'gps_bin='+str(gps),
      'vendor_fstab=/vendor/etc/fstab.lt013g':'vendor_fstab='+str(fstab),
      'p11=/dev/block/mmcblk0p11':'p11='+str(root/'p11'),
      'want_gps=cfe1ba47542d723577c5b2ef3f14841a24946b4022f631ef271a5c1cb26e9521':
           'want_gps='+hashlib.sha256(gps.read_bytes()).hexdigest(),
      '[ -b "$p11" ]':'[ -f "$p11" ]'
    }
    for old,new in remap.items():
        require(old in source,'missing fixture mapping: '+old)
        source=source.replace(old,new)
    script=root/'mock-actual-G402.sh';script.write_text(source)
    baseline={
      'MOCK_MODEL':'lt013g','MOCK_ANDROID':'11','MOCK_BOOT':'1',
      'MOCK_GPSD':'running','MOCK_SELINUX':'Enforcing',
      'MOCK_LABEL':'u:object_r:misc_block_device:s0',
      'MOCK_ADDR':'2: rmnet1 inet 10.100.2.4/32 scope global rmnet1',
      'MOCK_ROUTES':'default via 10.100.2.1 dev rmnet1 table rmnet1\n10.100.2.1 dev rmnet1 scope link table rmnet1'
    }
    def run(**overrides):
        env=os.environ.copy();env.update(baseline);env.update(overrides)
        env['PATH']=str(b)+':/usr/bin:/bin'
        return subprocess.run(['/bin/sh',str(script),'--read-only'],capture_output=True,text=True,env=env)
    good=run()
    require(good.returncode==0 and 'FAIL ' not in good.stdout and
            'PASS EXACT_G401_MODERN_ARM32_GPS_ELF_INSTALLED' in good.stdout and
            'PASS PDP_32_GATEWAY_DIRECT_HOST_ROUTE_AND_DEFAULT' in good.stdout and
            'PENDING OUTDOOR_STABLE_GPS_GLONASS_3D_REQUIRES_OWNER_TEST' in good.stdout,
            'good synthetic G401 postinstall rejected: '+good.stdout+' '+good.stderr)
    require('10.100.2.1' not in good.stdout and '10.100.2.4' not in good.stdout,'private IP leaked')
    print('PASS G402 synthetic G401 exact ROM/fstab/SELinux/route with mandatory GPS3D owner PENDING')
    mutations=[
      ('wrong_model',{'MOCK_MODEL':'lt01wifi'}),
      ('wrong_os',{'MOCK_ANDROID':'12'}),
      ('boot_incomplete',{'MOCK_BOOT':'0'}),
      ('SELinux_permissive',{'MOCK_SELINUX':'Permissive'}),
      ('p11_wrong_context',{'MOCK_LABEL':'u:object_r:block_device:s0'}),
    ]
    for name,env in mutations:
        bad=run(**env)
        require(bad.returncode==1 and 'FAIL ' in bad.stdout and
                'STOP: required exact firmware/boot/SELinux parity not accepted' in bad.stdout,
                'fail-open negative '+name+': '+bad.stdout)
        print('PASS G402 hard FAIL negative:',name)
    original=gps.read_bytes();gps.write_bytes(b'G400 older GPS not current signed G401')
    bad=run();require(bad.returncode==1 and 'FAIL G401_GPS_ELF_NOT_INSTALLED_OR_SHA_MISMATCH' in bad.stdout,
                      'wrong GPS ELF accepted')
    gps.write_bytes(original)
    print('PASS G402 rejects earlier incorrect GPS ELF')
    origfstab=fstab.read_text()
    fstab.write_text('/dev/block/platform/soc/12550000.mmc/by-name/OTA /cache emmc defaults defaults\n')
    bad=run();require(bad.returncode==1 and 'FAIL SELECTED_VENDOR_MISC_FSTAB_MISSING' in bad.stdout,
                      'missing selected /misc accepted')
    fstab.write_text(origfstab)
    print('PASS G402 rejects missing factory p11 /misc')
    for name,env in [
      ('no_cellular_PDP',{'MOCK_ADDR':'','MOCK_ROUTES':''}),
      ('no_gateway_host_route',{'MOCK_ROUTES':'default via 10.100.2.1 dev rmnet1 table rmnet1'}),
      ('gpsd_idle_or_down',{'MOCK_GPSD':'stopped'})]:
        uncertain=run(**env)
        require(uncertain.returncode==0 and 'PENDING ' in uncertain.stdout and
                'FAIL ' not in uncertain.stdout and
                'SOURCE/BOOT PARITY PASS ONLY' in uncertain.stdout,
                'optional network/GPSD falsely hard-failed '+name)
        require('10.100.2.1' not in uncertain.stdout and '10.100.2.4' not in uncertain.stdout,
                'cellular address leaked '+name)
        print('PASS G402 conditional physical test stays PENDING:',name)
    noarg=subprocess.run(['/bin/sh',str(script)],capture_output=True,text=True)
    require(noarg.returncode==2 and 'USAGE:' in noarg.stdout,'read-only flag required')
    print('G402 EXACT SHELL READONLY CONTRACT PASS: 1 good,7 hard failures,3 pending scenarios; no writes or private addresses')
