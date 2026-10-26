#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[5]
s=(root/'frameworks/av/media/libstagefright/bqhelper/GraphicBufferSource.cpp').read_text()
assert 'G912_GBS_REJECTED count=' in s
assert 'mNumOutstandingAcquires' in s and 'mFrameRepeatBlockedOnCodecBuffer' in s
p=Path(__file__).with_name('g912_readonly_bq_probe_trial.sh').read_text()
for token in ('setsid sh "$0" --watchdog','sleep 65','mount -o bind "$src" "$file"',
              'umount -l "$file"','G912_CAPTURE_COMPLETED_BEFORE_ROLLBACK',
              'G912_ORIGINAL_SHA_AND_CODEC_RESTORED',
              'cebdaca4fe2f7a9958b298515acdfb854a3c34d4f8660119df0a0bcf4102ada4'):
    assert token in p,token
assert p.index('setsid sh "$0" --watchdog') < p.index('mount -o bind "$src" "$file"')
for forbidden in ('mount -o remount,rw','dd if=','cp "$file"','mv "$src" "$file"'):
    assert forbidden not in p
print('G912_DIAGNOSTIC_NATIVE_SAFE_BIND_NO_BACKUP_GATE_PASS')
