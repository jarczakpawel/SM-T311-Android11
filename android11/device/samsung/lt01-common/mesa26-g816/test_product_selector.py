#!/usr/bin/env python3
"""SM-T311 ONLY: source Make refuses Mesa other than physical 26.2.5.

Runs real GNU Make conditional parser without building a ROM or touching tablet.
"""
import argparse
from pathlib import Path
import os
import subprocess
import tempfile

p=argparse.ArgumentParser()
p.add_argument('--device-dir',required=True,type=Path)
a=p.parse_args()
common=(a.device_dir/'common.mk').read_text()
device=(a.device_dir.parent/'lt013g/device.mk').read_text()
start=common.index('# SM-T311 ONLY: the physical Mali-400 release')
end=common.index('# END SM-T311 Mesa26 release lock',start)
source=common[start:end].strip()
assert 'SMT311_USE_MESA26 := true' in device
assert 'SMT311_MESA26_VERSION := 26.2.5' in device
assert 'mesa26-2625' in source
assert 'prebuilt/pjarczak/mesa26/' not in source
assert 'libGLES_mesa' not in source.replace('or libGLES_mesa', '')
mk='\n'.join(['TARGET_COPY_OUT_VENDOR := vendor', 'PRODUCT_COPY_FILES :=', 'PRODUCT_PACKAGES :=', 'SMT311_USE_MESA26 := true', 'SMT311_MESA26_VERSION := 26.2.5', source, 'all:', '\t@printf "%s\\n" "COPY=$(PRODUCT_COPY_FILES)" "PKGS=$(PRODUCT_PACKAGES)"'])+'\n'
def run(variables=None,extraenv=None):
    env=os.environ.copy();env.update(extraenv or {})
    args=['make','--no-print-directory','-f','-']
    args.extend(f'{k}={v}' for k,v in (variables or {}).items())
    return subprocess.run(args,input=mk,text=True,capture_output=True,env=env)

cases=[('default 26.2.5',{},True),('explicit current',{'SMT311_USE_MESA26':'true','SMT311_MESA26_VERSION':'26.2.5'},True),('blank version',{'SMT311_MESA26_VERSION':''},False),('old 26.2.4',{'SMT311_MESA26_VERSION':'26.2.4'},False),('disabled',{'SMT311_USE_MESA26':'false'},False),('invalid version',{'SMT311_MESA26_VERSION':'99.99'},False),('old with prior ACK still refused',{'SMT311_MESA26_VERSION':'26.2.4','SMT311_EXPLICIT_GRAPHICS_ROLLBACK':'I_ACKNOWLEDGE_ROLLBACK'},False),('disabled with ACK still refused',{'SMT311_USE_MESA26':'false','SMT311_EXPLICIT_GRAPHICS_ROLLBACK':'I_ACKNOWLEDGE_ROLLBACK'},False)]
for label,flags,ok in cases:
    result=run(flags)
    if ok:
        assert result.returncode==0,(label,result.stdout,result.stderr)
        copies=result.stdout.split('COPY=',1)[1].split('\n',1)[0].split()
        assert len(copies)==4 and all('mesa26-2625/' in c for c in copies),(label,copies)
        assert 'libgbm_mesa' in result.stdout and 'libdrm_mesa26' in result.stdout
    else:
        assert result.returncode!=0 and 'SM-T311 requires Mesa 26.2.5' in result.stderr,(label,result.stderr)
    print('MESA_ONLY_2625_PASS',label, 'BUILD_OK' if ok else 'REFUSED')
with tempfile.TemporaryDirectory(prefix='t311-2625-missing-') as d:
    result=run(extraenv={'SMT311_ANDROID_ROOT':d})
    assert result.returncode!=0 and 'SM-T311 Mesa 26.2.5 staged ARM32 outputs invalid' in result.stderr
    print('MESA_ONLY_2625_PASS missing staged artifacts REFUSED')
print('SM_T311_MESA_ONLY_2625_GNU_MAKE_9_OF_9_PASS')
