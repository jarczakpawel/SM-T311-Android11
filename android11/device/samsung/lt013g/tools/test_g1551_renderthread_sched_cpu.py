#!/usr/bin/env python3
"""Regression verifies physical source fixture and rejects broken sched counters."""
import importlib.util
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('g1551', root / 'g1551_analyze_renderthread_sched_cpu.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
sample = root / 'testdata/g1550-physical-renderthread-cpu-wall-4swipes.tsv'
report = module.analyze(sample)
assert report['gesture_count'] == 8
assert report['sha256'] == '5528d9f2084fd61817f065db98ee7709bd0aaaa86c3ad96c7618a92cd4ec35c3'
assert report['totals']['window_wall_ms'] == 14290.0
assert report['totals']['renderthread_cpu_ms'] == 1920.681
assert report['totals']['ui_thread_cpu_ms'] == 1080.849
assert report['totals']['rt_voluntary_switches'] == 1240.0
assert report['totals']['rt_involuntary_switches'] == 1075.0
assert report['renderthread_cpu_share_of_measured_wall_pct'] == 13.441
text = sample.read_text()
with tempfile.TemporaryDirectory() as temporary:
    bad = Path(temporary) / 'bad.tsv'
    # Fail closed on fewer swipes, illegal counters and invalid gesture order.
    for altered in ('\n'.join(text.splitlines()[:-1])+'\n',
                    text.replace('8-close', '8-open'),
                    text.replace('18598.939057', '1.000000')):
        bad.write_text(altered)
        try:
            module.analyze(bad)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid physical schedule sample was accepted')
print('G1551_REAL_PHYSICAL_EIGHT_GESTURES_CPU_CONTEXT_SWITCH_SHA_REGRESSION_PASS')
