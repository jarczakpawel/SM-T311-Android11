#!/usr/bin/env python3
"""G389 cumulative G388 source release plus enforcing OTA(p11) /misc label.

Fail closed on changed OEM partitions, Java telephony ART, GPS Binder CIL,
unexpected diagnostic tools, type relabel to anything except misc_block_device.
No flash or raw access to factory OTA/EFS/CP.
"""
from pathlib import Path
from zipfile import ZipFile
import argparse, hashlib, json, subprocess, sys

pa=argparse.ArgumentParser()
pa.add_argument('input',type=Path)
pa.add_argument('output',type=Path)
pa.add_argument('--baseline-g388',required=True,type=Path)
pa.add_argument('--baseline-g387',required=True,type=Path)
pa.add_argument('--baseline-g382',required=True,type=Path)
pa.add_argument('--android',required=True,type=Path)
a=pa.parse_args()
assert a.input.resolve()!=a.output.resolve() and not a.output.exists()
tools=Path(__file__).resolve().parent
subprocess.run([sys.executable,str(tools/'test_g389_bcb_enforcing_selinux.py'),
                '--android',str(a.android)],check=True)
ctx_name='SYSTEM/vendor/etc/selinux/vendor_file_contexts'
new_source=(a.android/'device/samsung/lt01-common/sepolicy/vendor/file_contexts').read_text()
exact='/dev/block/mmcblk0p11 u:object_r:misc_block_device:s0'
with ZipFile(a.input) as incoming, ZipFile(a.baseline_g388) as older:
    ctx=incoming.read(ctx_name).decode()
    old_ctx=older.read(ctx_name).decode()
    if ctx.count(exact)!=1 or exact in old_ctx:
        raise SystemExit('G389 compiled vendor_file_contexts missing exact new p11 label or already present in baseline')
    if exact not in new_source:
        raise SystemExit('G389 source not same compiled OTA p11 label')
    # New deployed vendor_file_contexts must preserve all old policies
    # byte-for-byte; only a tightly scoped new line/comments may change.
    if not set(old_ctx.splitlines()).issubset(set(ctx.splitlines())):
        raise SystemExit('G389 changes preexisting compiled file_contexts entries')
    new_lines=[line for line in ctx.splitlines() if line not in old_ctx.splitlines()]
    if new_lines != [exact]:
        raise SystemExit('G389 unwanted new file_contexts rules: '+repr(new_lines))
    immutable=[
      'IMAGES/boot.img','IMAGES/recovery.img',
      'SYSTEM/vendor/lib/libril.so','SYSTEM/lib/libaudiohal.so',
      'SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil',
      'SYSTEM/framework/telephony-common.jar',
      'SYSTEM/framework/arm/boot-telephony-common.art',
      'SYSTEM/framework/arm/boot-telephony-common.oat',
      'SYSTEM/framework/boot-telephony-common.vdex',
      'SYSTEM/vendor/etc/fstab.lt013g',
    ]
    for n in immutable:
        if incoming.read(n)!=older.read(n):
            raise SystemExit('G389 unexpectedly changed G388 OEM/ART/vendor fstab: '+n)
    for name in incoming.namelist():
        if 'T311GnssStatus' in name or 'T311NtpTransportProbe' in name:
            raise SystemExit('G389 diagnostic binary slipped into OTA: '+name)
# Reuse the exact G388 and G387/382 stock-first and telephony source contracts,
# including real Java 10+4 negative cases, factory OTA BCB p11 and G382 binaries.
subprocess.run([sys.executable,str(tools/'stage_g388_recovery_bcb_target_files.py'),
                str(a.input),str(a.output),
                '--baseline-g387',str(a.baseline_g387),
                '--baseline-g382',str(a.baseline_g382),
                '--android',str(a.android)],check=True)
with ZipFile(a.output) as final:
    if final.read(ctx_name).decode()!=ctx:
        raise SystemExit('G389 staging mutated exact enforcing BCB context')
    for n in immutable:
        with ZipFile(a.input) as src:
            if final.read(n)!=src.read(n):
                raise SystemExit('G389 staging changed immutable '+n)
res={
 'status':'G389_STOCK_P11_MISC_ENFORCING_AND_G388_BCB_G387_MODEM_GATE_PASS',
 'final_physically_installed':False,
 'bcblabel':exact,'compiled_vendor_file_contexts_sha256':hashlib.sha256(ctx.encode()).hexdigest(),
 'baseline_vendor_file_contexts_sha256':hashlib.sha256(old_ctx.encode()).hexdigest(),
 'source_compiled_exact_p11_label':True,
 'unmodified_prior_context_entries':True,
 'immutable_legacy_critical_outputs':immutable,
 'staged_sha256':hashlib.file_digest(a.output.open('rb'),'sha256').hexdigest(),
 'staged_bytes':a.output.stat().st_size,
}
a.output.with_name('G389-ENFORCING-BCB-RELEASE-GATE.json').write_text(json.dumps(res,indent=2)+'\n')
print(json.dumps(res,indent=2))
