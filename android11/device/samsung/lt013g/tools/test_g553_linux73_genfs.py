#!/usr/bin/env python3
"""Fail-closed source+compiled-policy parity for SM-T311 Linux 7.3 I2C nodes.
Read-only: does not write to device, does not modify SELinux or flash.
"""
from pathlib import Path
A=Path("/home/debian/android/lineage-18.1-sm-t311")
policy=A/"out/target/product/lt013g/system/vendor/etc/selinux/vendor_sepolicy.cil"
source=A/"device/samsung/lt01-common/sepolicy/vendor/genfs_contexts"
lights=A/"device/samsung/lt01-common/sepolicy/vendor/hal_light_lt013g.te"
health=A/"device/samsung/lt01-common/sepolicy/vendor/hal_health_lt013g.te"
pairs=[
 ("/devices/platform/i2c-13/i2c-13/13-002c/backlight/lcd-bl","sysfs_leds"),
 ("/devices/platform/i2c-10/i2c-10/10-0036/power_supply/max170xx_battery","sysfs_batteryinfo"),
 ("/devices/platform/i2c-9/i2c-9/9-0066/max77693-charger/power_supply/max77693-charger","sysfs_batteryinfo"),
]
s=source.read_text()
c=policy.read_text()
for path,label in pairs:
    a="genfscon sysfs "+path+" u:object_r:"+label+":s0"
    b="(genfscon sysfs "+path+" (u object_r "+label+" ((s0) (s0))))"
    assert s.splitlines().count(a)==1,(path,"source_count")
    assert c.splitlines().count(b)==1,(path,"compiled_count")
    print("PASS EXACT_SEL_LABEL",label,path)
assert "allow hal_light_default sysfs_leds:file rw_file_perms;" in lights.read_text()
assert "allow hal_health_default sysfs_batteryinfo:file r_file_perms;" in health.read_text()
print("PASS narrowly scoped DAC read and backlight write source")
print("G553_SOURCE_AND_COMPILED_VENDOR_POLICY_PASS")
