#!/usr/bin/env python3
from pathlib import Path
r=Path(__file__).resolve().parents[5]
s=(r/'external/v4l2_codec2/components/V4L2EncodeComponent.cpp').read_text()
assert 'g916SampleIndex' not in s
assert 'G917 EXPERIMENT ONLY' not in s
for v in ('outputBytesUsed < outputDataOffset',
'outputBytesUsed > mOutputBufferSize',
'encodedDataSize > block->size()',
'G916_INVALID_MFC_CAPTURE_RANGE',
'G916_INVALID_CODEC2_OUTPUT_RANGE'):
 assert v in s,v
sh=Path(__file__).with_name('g916_mfc_output_trial.sh').read_text()
for v in ('sleep 80','setsid sh "$0" --watchdog','umount -l "$file"',
'/system/vendor/lib/libv4l2_codec2_components.so',
'G916_COMPLETE_30SEC_MP4_BEFORE_RESTORE',
'9adf74cf8a6c9aae9a52adf062a4845f7d911c9affcc03c63fdb69d312047d07'):
 assert v in sh,v
assert sh.index('setsid sh "$0" --watchdog') < sh.index('mount -o bind "$stage" "$file"')
assert sh.index('wait "$pid"') < sh.index('G916_COMPLETE_30SEC_MP4_BEFORE_RESTORE')
for bad in ('mount -o remount,rw','dd if=','cp "$file"','mv "$stage" "$file"'):
 assert bad not in sh
print('G916_MFC_OUTPUT_BOUNDS_SPARSE_READONLY_METADATA_AND_WATCHDOG_PASS')
