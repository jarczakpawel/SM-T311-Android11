#!/usr/bin/env python3
"""Exact two physical post-G1568 Android11 BufferQueue captures, source-only replay."""
from pathlib import Path
import hashlib
import json
import runpy

R=Path(__file__).resolve().parent
T=R/'testdata'
AN=runpy.run_path(str(R/'g1555_analyze_hardware_bufferqueue_frame_substages.py'),run_name='g1577_frameinfo_import')['analyze']
EXPECTED={
 'g1575':{
  'HWUI-FRAMESTATS':'293ab0ca0d7491226e7f3de6c090c26455abd02216dc0766f3998a614a514fd7',
  'SF-LATENCY':'c5351c2533f9ab7ec890fac21cde19973ecfe76e9c55033f8b17e64e728e6629',
  'SUMMARY':'f263c93655a8994ee8c3f0da8816bb34bd10c161016593923bc412ec5a15494c',
  'janky':263,'total':309,'valid':115,'wait8':79,'dequeue8':30,'paired_slow':101,
 },
 'g1576':{
  'HWUI-FRAMESTATS':'7fcb47bccd973b9abda63c001f0a00e6dd9d53a513a430ed1c6a08f3b3733776',
  'SF-LATENCY':'2a628cdda89069c41c351fd91127f6747b6e2389ddc931ab092f29b6ec8067db',
  'SUMMARY':'1fa7a7665df0a0b57004feb2e6b483d097f34caa1d33f5d7ee5be1983481c915',
  'janky':261,'total':317,'valid':114,'wait8':79,'dequeue8':27,'paired_slow':98,
 }
}
for key,expected in EXPECTED.items():
    stem=key+'-background-geometry-postflash-'
    files={name:T/(stem+name+'.txt') for name in ('SF-LATENCY','HWUI-FRAMESTATS','SUMMARY')}
    for name,p in files.items():
        assert hashlib.sha256(p.read_bytes()).hexdigest()==expected[name],(key,name)
    summary=files['SUMMARY'].read_text()
    assert f'Total frames rendered: {expected["total"]}' in summary
    assert f'Janky frames: {expected["janky"]}' in summary
    x=AN(files['HWUI-FRAMESTATS'])
    assert x['valid_same_frame_records']==expected['valid']
    assert x['counts']['renderthread_wait_over_8ms']==expected['wait8']
    assert x['counts']['dequeue_over_8ms']==expected['dequeue8']
    assert x['counts']['same_frame_rt_over_vsync']==expected['paired_slow']
    assert x['counts']['queue_over_8ms']==0
    print('G1577_PHYSICAL_SHA_VERIFIED',key,expected['janky'],expected['total'],x['counts'])
print('G1577_TWO_PHYSICAL_POSTFLASH_115_114_HWUI_FRAME_REGRESSIONS_PASS_NO_FPS_GAIN_CLAIM')
