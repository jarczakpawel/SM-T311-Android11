#!/usr/bin/env python3
"""Prevent inadvertent queue behavior/ROM mutation in diagnostic release."""
from pathlib import Path
r=Path(__file__).resolve().parents[5]
s=(r/'frameworks/av/media/libstagefright/bqhelper/GraphicBufferSource.cpp').read_text()
h=(r/'frameworks/av/media/libstagefright/bqhelper/include/media/stagefright/bqhelper/GraphicBufferSource.h').read_text()
a='''    if (err != OK) {
        // G912: SM-T311 MediaRecorder'''
assert a in s
assert 'mT311RejectedAcquires;' in h
assert 'G912_GBS_REJECTED count=%u acquired=%d pending=%d' in s
assert '((++mT311RejectedAcquires & 63u) == 1u)' in s
assert 'mConsumer->setMaxAcquiredBufferCount(bufferCount)' in s
assert 'onInputBufferEmptied(codec_buffer_id bufferId, int fenceFd)' in s
sh=Path(__file__).with_name('run_g912_transient.sh').read_text()
for x in ('setsid sh "$0" --watchdog', 'umount -l "$file"', 'sleep 66',
          'G912_CAPTURE_COMPLETE_BEFORE_ROLLBACK',
          'G912_ORIGINAL_LIB_RESTORED_AND_SERVICE_RESTARTED',
          'cebdaca4fe2f7a9958b298515acdfb854a3c34d4f8660119df0a0bcf4102ada4'):
    assert x in sh,x
assert sh.index('setsid sh "$0" --watchdog')<sh.index('mount -o bind "$stage" "$file"')
assert sh.index('wait "$p"') < sh.index('G912_CAPTURE_COMPLETE_BEFORE_ROLLBACK')
for bad in ('mount -o remount,rw','dd if=','cp "$file"','mv "$stage" "$file"'):
    assert bad not in sh
print('G912_OWNER_TRACE_ON_REJECT_WITH_NO_BEHAVIOR_CHANGE_AND_AUTO_RESTORE_SOURCE_PASS')
