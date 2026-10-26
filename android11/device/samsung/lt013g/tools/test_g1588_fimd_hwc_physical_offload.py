#!/usr/bin/env python3
"""Reproduce real physical mixed HWC states and immutable FIMD source guard."""
import importlib.util
import tempfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('g1588',Path(__file__).with_name('g1588_analyze_fimd_hwc_physical_layers.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
a=Path(__file__).resolve().parents[4]
k=Path('/home/debian/Projekty/samsung/kernel-active-g958')
x=m.parse(check_source=k.is_dir(),android=a,kernel=k if k.is_dir() else None)
expected={'HOME':(4,1,3,0.330579),'OPEN':(3,1,2,0.259633),'CLOSE':(4,1,3,0.320748)}
for name,(n,dev,client,eff) in expected.items():
 got=x['states'][name]
 assert len(got['layers'])==n
 assert got['device_layers']==dev
 assert got['client_layers']==client
 assert got['last_dumpsys_estimated_pixel_offload_fraction']==eff
 assert got['composition_errors']==0
 assert got['cumulative_hwc_missed_frames']==209
assert x['on_device']['hwmixed']=='1'
assert x['on_device']['singleatomic']=='1'
assert 'ImageWallpaper#0' in x['states']['HOME']['layers'][0]['name']
assert x['states']['HOME']['layers'][0]['composition']=='DEVICE'
assert 'StatusBar#0' in x['states']['OPEN']['layers'][2]['name']
assert x['states']['OPEN']['layers'][2]['composition']=='DEVICE'
with tempfile.TemporaryDirectory() as tmp:
 dir=Path(tmp)
 for f in m.FILES.values(): (dir/f.name).write_bytes(f.read_bytes())
 changed=dir/m.FILES['HOME'].name
 changed.write_bytes(changed.read_bytes()+b'\n')
 try:m.parse(base=dir)
 except ValueError as e:assert 'G1588_REFUSE_CHANGED_PHYSICAL_HWC_HOME' in str(e)
 else:raise AssertionError('G1588_REFUSED_TO_FAIL_MODIFIED_DATA')
print('G1588_REAL_HWC_3_STATES_1_HARDWARE_PLANE_SOURCE_SHA_TAMPER_GUARDS_PASS')
