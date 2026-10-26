#!/usr/bin/env python3
"""G913: only cap acquisition at configured BufferQueue max+1; no frame drop."""
from pathlib import Path
root=Path(__file__).resolve().parents[5]
s=(root/'frameworks/av/media/libstagefright/bqhelper/GraphicBufferSource.cpp').read_text()
h=(root/'frameworks/av/media/libstagefright/bqhelper/include/media/stagefright/bqhelper/GraphicBufferSource.h').read_text()
for needle in ('mConfiguredMaxAcquiredBuffers = bufferCount;',
'const bool atConsumerAcquireLimit',
'mNumOutstandingAcquires >= mConfiguredMaxAcquiredBuffers + 1',
'if (!areWeDiscardingAvailableBuffers_l() &&',
'++mRepeatLastFrameGeneration;',
'mConfiguredMaxAcquiredBuffers + 1'):
    assert needle in s, needle
assert 'mConfiguredMaxAcquiredBuffers;' in h
# G915 release cleanup: retain the scheduling guard, not temporary high-volume
# instrumented BQ probes from G912/G913 native A/B experiments.
assert 'G912_GBS_REJECTED' not in s
assert 'G913_GBS_DEFERRED' not in s
assert 'mT311RejectedAcquires' not in s
assert 'mT311DeferredAtLimit' not in s
start=s.index('const bool atConsumerAcquireLimit')
end=s.index('VideoBuffer buffer;',start)
assert 'mNumAvailableUnacquiredBuffers' in s[:start]
assert 'acquireBuffer_l(&buffer)' not in s[start:end]
script=Path(__file__).with_name('run_g913_transient.sh').read_text()
for marker in ('sleep 90','setsid sh "$0" --watchdog','umount -l "$file"',
              'G913_CAPTURE_COMPLETE_BEFORE_ROLLBACK', '--stress',
              'G913_ORIGINAL_LIB_RESTORED_AND_SERVICE_RESTARTED',
              'c93949af3ba9fa9a905fb6b02c9afd0ccfb27f32213dc3697e7952f39f3a4374'):
    assert marker in script,marker
assert script.index('setsid sh "$0" --watchdog') < script.index('mount -o bind "$stage" "$file"')
assert script.index('wait "$p"') < script.index('G913_CAPTURE_COMPLETE_BEFORE_ROLLBACK')
for forbidden in ('mount -o remount,rw','dd if=','cp "$file"','mv "$stage" "$file"'):
    assert forbidden not in script
print('G913_DEFER_AT_REAL_CONSUMER_LIMIT_AND_AUTO_RESTORE_GUARD_PASS')
