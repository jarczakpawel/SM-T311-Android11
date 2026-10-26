#!/usr/bin/env python3
"""Physical Android11 FrameInfo exactly replayed without ADB, Mesa or TWRP writes."""
import importlib.util
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('g1555', root / 'g1555_analyze_hardware_bufferqueue_frame_substages.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
source = root / 'testdata/g1541-physical-notificationshade-hwui-framestats.txt'
report = module.analyze(source)
assert report['raw_sha256'] == '43bdacf0f7d6f06f779f96b3b702727729a5efa4d6c96300d9a2c2faf49f707e'
assert report['retained_window_frames'] == 120
assert report['valid_same_frame_records'] == 114
assert report['excluded_invalid_or_nonmonotonic'] == 0
assert report['counts'] == {
    'same_frame_rt_over_vsync': 97,
    'renderthread_wait_over_8ms': 74,
    'gl_issue_to_swap_over_vsync': 39,
    'dequeue_over_8ms': 31,
    'dequeue_over_vsync': 18,
    'queue_over_8ms': 0,
    'long_gl_and_long_dequeue': 30,
    'long_gl_without_long_dequeue': 9,
    'slow_rt_and_wait8_but_dequeue_short': 54,
    'non_dequeue_drawswap_over_vsync': 0,
}
assert report['p50_ms']['wait_ns'] == 11.824
assert report['p50_ms']['drawswap_ns'] == 14.909
assert report['p50_ms']['dequeue_ns'] == 5.698
assert report['p50_ms']['queue_ns'] == 0.697
assert report['p50_ms']['non_dequeue_drawswap_ns'] == 9.654
assert report['p90_ms']['dequeue_ns'] == 21.236


# Reproducibility: same work, same physical G1527 Android11 SYSTEM, independent
# second capture. The distributions must be checked explicitly, not assumed.
second = root / 'testdata/g1557-repeat-physical-notificationshade-HWUI-FRAMESTATS.txt'
repeat = module.analyze(second)
assert repeat['valid_same_frame_records'] == 115
assert repeat['counts'] == {
    'same_frame_rt_over_vsync': 99,
    'renderthread_wait_over_8ms': 74,
    'gl_issue_to_swap_over_vsync': 34,
    'dequeue_over_8ms': 30,
    'dequeue_over_vsync': 17,
    'queue_over_8ms': 0,
    'long_gl_and_long_dequeue': 27,
    'long_gl_without_long_dequeue': 7,
    'slow_rt_and_wait8_but_dequeue_short': 54,
    'non_dequeue_drawswap_over_vsync': 2,
}
assert repeat['p50_ms']['wait_ns'] == 11.919
assert repeat['p50_ms']['dequeue_ns'] == 5.323
assert repeat['p50_ms']['queue_ns'] == 0.678

with tempfile.TemporaryDirectory() as tmp:
    bad = Path(tmp) / 'malformed_framestats.txt'
    # Missing all frame rows must be a hard error, not a false 0%-jank success.
    t = source.read_text()
    bad.write_text(t.replace('Window: NotificationShade', 'Window: OtherOnly'))
    try:
        module.analyze(bad)
    except (KeyError, ValueError):
        pass
    else:
        raise AssertionError('G1555 accepted missing NotificationShade window')

print('G1558_TWO_INDEPENDENT_PHYSICAL_114_AND_115_SAME_FRAME_BUFFERQUEUE_REGRESSIONS_PASS')
