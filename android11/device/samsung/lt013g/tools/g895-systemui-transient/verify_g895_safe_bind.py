#!/usr/bin/env python3
"""Static fail-closed guard for ephemeral SystemUI APK trial.

The actual system file must never be overwritten or backed up by trial code.
The running APK keeps the old image mapped; rollback MUST use MNT_DETACH.
"""
from pathlib import Path

p=Path(__file__).with_name('g895_systemui_bind_trial.sh')
s=p.read_text()
required=[
'--watchdog', '--trial', '--restore',
'51d4870efed53ec1f1198ecc3a535af1878074aecf27a8c40c271069990cddae',
'dfc2d99b790791d86893818924344c138ce397eaf44b2014bf9eca8aaa7ac31c',
'"$(getprop ro.product.device)" = lt013g',
'"$(getprop ro.build.version.sdk)" = 30',
'"$(getprop sys.boot_completed)" = 1',
'"$(getenforce)" = Enforcing',
'"$(sha "$target")" = "$old"',
'"$(sha "$src")" = "$new"',
'mount -o bind "$src" "$target"',
'umount -l "$target"',
'G895_BIND_LAZY_DETACH_PASS',
'setsid sh "$0" --watchdog',
'G895_RESTORED_ORIGINAL_SYSTEMUI_SHA_PASS',
'kill -9 "$p"',
]
for requirement in required:
    assert requirement in s, f'G895 rollback safety contract missing: {requirement}'
for invalid in ('mount -o remount,rw', 'mount -o remount,rw /system', 'mkfs.', 'dd if=', 'rm -f "$target"', 'mv "$src" "$target"', 'cp "$target"'):
    assert invalid not in s, f'G895 trial may overwrite/backup live system: {invalid}'
assert s.index('setsid sh "$0" --watchdog') < s.index('mount -o bind "$src" "$target"')
assert '8|36|48)' in s and 'sleep 66' in s
assert 'sleep "$trial_hold"' in s
assert 'G906_UNQUALIFIED_TEST_DURATION' in s
assert s.index('sleep "$trial_hold"') < s.index('# Returning from the script MUST automatically restore')
print('G895_EPHEMERAL_UI_SHA_WATCHDOG_BIND_LAZY_DETACH_AND_NO_BACKUP_SOURCE_GUARD_PASS')
