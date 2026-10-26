#!/usr/bin/env python3
"""Two independent physical same-SystemUI-process controls; no device access."""
from pathlib import Path
import importlib.util
import tempfile
spec=importlib.util.spec_from_file_location('g1562',Path(__file__).with_name('g1562_analyze_shade_vs_statusbar_same_process.py'))
mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
r=mod.analyze()
assert set(r['runs']) == {'first','repeat'}
assert [r['runs'][k]['windows']['NotificationShade']['valid_frames'] for k in ('first','repeat')] == [114,115]
assert [r['runs'][k]['windows']['StatusBar']['valid_frames'] for k in ('first','repeat')] == [45,46]
for key in ('first','repeat'):
 x=r['runs'][key]['shade_to_statusbar_median_ratio']
 assert 20 < x['renderthread_queue_wait'] < 22
 assert 4 < x['commands_to_swap'] < 5
 assert 18 < x['DequeueBufferDuration'] < 20
 assert 1 < x['QueueBufferDuration'] < 2
 assert 7 < x['intended_vsync_to_completion_wall'] < 8
expected={'first':(11.824,0.568,5.698,0.29),'repeat':(11.919,0.548,5.323,0.292)}
for key,(q,s,d,sd) in expected.items():
 windows=r['runs'][key]['windows']
 assert windows['NotificationShade']['stages']['renderthread_queue_wait']['p50']==q
 assert windows['StatusBar']['stages']['renderthread_queue_wait']['p50']==s
 assert windows['NotificationShade']['stages']['DequeueBufferDuration']['p50']==d
 assert windows['StatusBar']['stages']['DequeueBufferDuration']['p50']==sd
# Refuse to interpret modified source fixtures; a copied altered SHA must not pass.
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 for name in mod.SOURCES.values():
  (p/name).write_bytes((Path(__file__).with_name('testdata')/name).read_bytes())
 f=p/mod.SOURCES['first'];f.write_bytes(f.read_bytes()+b'\n')
 try:mod.analyze(p)
 except ValueError:pass
 else:raise AssertionError('G1562 input SHA tamper not caught')
print('G1562_TWO_REAL_SYSTEMUI_SHADE_VS_STATUSBAR_SHA_VERIFIED_CONTROL_PASS')
