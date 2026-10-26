#!/usr/bin/env python3
"""Immutable physical G1608 HWC postflash four-swipes; no performance claims."""
import hashlib
import importlib.util
from pathlib import Path
R=Path(__file__).resolve().parent
T=R/'testdata'
checks={
  'SF-LATENCY':'aaa9bc14cc3f70b6b286f60bb3803d7fa944d9eac341c9fb8c66f2452925d750',
  'HWUI-FRAMESTATS':'64768ab91fcdbf631bc6b70a5618404c4bd10ec0456850b3c0e23ddf58988c14',
  'SUMMARY':'66894a150d8b275cdca6a5c0c33b08db161a026b0086472d1c56f106fe869092'
}
for name,expected in checks.items():
    p=T/f'g1614-real-buffer-hwc-postflash-{name}.txt'
    assert p.is_file() and hashlib.sha256(p.read_bytes()).hexdigest()==expected,(name,p)
s=(T/'g1614-real-buffer-hwc-postflash-SUMMARY.txt').read_text()
assert 'Total frames rendered: 307' in s
assert 'Janky frames: 253 (82.41%)' in s
assert 'kernel=7.3.0-pjarczak' in s
assert 'refresh_period_ns=17107264' in s
spec=importlib.util.spec_from_file_location('analyzer',R/'g1555_analyze_hardware_bufferqueue_frame_substages.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
x=m.analyze(T/'g1614-real-buffer-hwc-postflash-HWUI-FRAMESTATS.txt')
assert x['valid_same_frame_records']==115
assert x['counts']['dequeue_over_8ms']==37
assert x['counts']['renderthread_wait_over_8ms']==80
assert x['counts']['same_frame_rt_over_vsync']==101
assert x['counts']['queue_over_8ms']==0
print('G1615_REAL_G1608_PHYSICAL_SHA_VERIFIED_253_307_JANKY_115_FRAMEINFO_NO_FPS_GAIN_PASS')
