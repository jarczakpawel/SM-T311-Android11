#!/usr/bin/env python3
from pathlib import Path
s=(Path(__file__).resolve().parent/"g1401-disable-inactive-contact-backup.sh").read_text()
for term in ('settings get secure backup_enabled','pm disable-user --user 0 "$pkg"',
             'pkg=org.calyxos.backup.contacts','pm list packages -d',
             'getprop gsm.sim.state','debug.lt013g.gps.v357.lto'):
    assert term in s,term
for unsafe in ('\npm clear','\npm uninstall','\nrm -rf','com.android.se','com.android.phone','\nsetprop',
               'reboot','setenforce','com.android.providers.contacts','killall'):
    assert unsafe not in s,unsafe
print("G1401_BACKUP_OFF_ONLY_SAFE_DISABLE_SOURCE_PASS")
