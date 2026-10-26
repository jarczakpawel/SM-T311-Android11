#!/usr/bin/env python3
"""Two independent positive/negative checks of REAL post-G1597 capture, no device mutations."""
import hashlib
from pathlib import Path
from importlib.util import spec_from_file_location, module_from_spec

R=Path(__file__).resolve().parent
T=R/'testdata'
S=T/'g1598-hwc-optin-off-postflash-'
checks={
 'SF-LATENCY':'c6866f043646309a75cc96f147ca8967e50050d37b8e59c0c5ba6295e0fb0036',
 'HWUI-FRAMESTATS':'8a4c0249514733a53c1e63014c3a45aedd5cbdccfd761f5aed55aabe5487f65d',
 'SUMMARY':'9d4602ccaabdd1ff8fa2a03dbee72e436ab4db543920dd292be904e73bc02eb1',
}
for name,expected in checks.items():
 p=T/('g1598-hwc-optin-off-postflash-'+name+'.txt')
 assert p.is_file() and hashlib.sha256(p.read_bytes()).hexdigest()==expected
raw=(T/'g1598-hwc-optin-off-postflash-SUMMARY.txt').read_text()
assert 'Total frames rendered: 310' in raw
assert 'Janky frames: 257 (82.90%)' in raw
assert 'kernel=7.3.0-pjarczak' in raw
assert 'refresh_period_ns=17107264' in raw
name='g1555_analyze_hardware_bufferqueue_frame_substages.py'
spec=spec_from_file_location('g1555',R/name)
m=module_from_spec(spec);spec.loader.exec_module(m)
x=m.analyze(T/'g1598-hwc-optin-off-postflash-HWUI-FRAMESTATS.txt')
assert x['valid_same_frame_records']==115
assert x['counts']['dequeue_over_8ms']==33
assert x['counts']['renderthread_wait_over_8ms']==74
assert x['counts']['same_frame_rt_over_vsync']==95
assert x['counts']['queue_over_8ms']==0
print('G1599_REAL_POSTFLASH_HWC_OPTIN_OFF_257_310_115_FRAMEINFO_SAME_LATENCY_SHA_PASS_NO_FPS_GAIN')
