#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
s=(root/"gps-v195/init.lt013g.gps-legacy.rc").read_text()
assert s.count("setprop debug.lt013g.gps.v357.lto 0")==1
assert "setprop debug.lt013g.gps.v357.lto 1" not in s
assert "write /sys/class/sec/gps/GPS_PWR_EN/value 0" in s
assert "chown root system /dev/ttySAC1" in s
assert "start vendor.lt013g-gps-legacy-v195" in s
proxy=(root/"gps-v195/gps_proxy.c").read_text()
assert 'v392_property_enabled("debug.lt013g.gps.v357.lto")' in proxy
assert 'G357_LTO_REJECT opt_in_disabled' in proxy
print("G1390_GNSS_AUTO_LTO_DISABLED_INIT_PRESERVES_BCM4752_PASS")
