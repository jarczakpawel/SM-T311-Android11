#!/usr/bin/env python3
"""G408 release gate smoke and rejection tests using real archived G403 metadata.

No 1.1GB test copy, no Android or TWRP operations. Real candidate check must
still run only after the userdebug build finishes successfully.
"""
import ast
import importlib.util
from pathlib import Path
from zipfile import ZipFile

BASE=Path(__file__).resolve().parent
GATE=BASE/"test_g408_vendor_zram_targetfiles_delta.py"
G403=Path("/home/debian/Projekty/samsung/artifacts/SM-T311-G403-BOUNDED-CP-CLOSE-20261017/G403-full-guarded-target-files.zip")

def insist(condition,message):
    if not condition:raise AssertionError(message)

def blocked(callback,label):
    try:callback()
    except AssertionError:print("PASS reject",label)
    else:raise AssertionError("FAIL OPEN "+label)

def main():
    spec=importlib.util.spec_from_file_location("g408_release_gate",GATE)
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    m=module
    tree=ast.parse(GATE.read_text())
    mainfn=next(x for x in tree.body if isinstance(x,ast.FunctionDef) and x.name=="main")
    writes=sum(isinstance(x,ast.Name) and isinstance(x.ctx,ast.Store) and
               x.id=="old_build_props" for x in ast.walk(mainfn))
    reads=sum(isinstance(x,ast.Name) and isinstance(x.ctx,ast.Load) and
              x.id=="old_build_props" for x in ast.walk(mainfn))
    insist(writes==1 and reads==2,
           "G408 negative build variant gate references undefined/unused archived properties")
    insist(G403.exists(),"immutable G403 source archive unavailable")
    with ZipFile(G403) as z:
        old={f.filename:(f.CRC,f.file_size) for f in z.infolist()}
        props=z.read("SYSTEM/build.prop").decode()
        cil=z.read(m.SYS_EXT_CIL)
    insist(len(old)>4000,"expected actual complete archived G403 target files")
    m.check_build_variant(props,props)
    for key,value in [("ro.build.type","eng"),("ro.build.type","user"),
                      ("ro.build.flavor","lineage_lt013g-eng")]:
        x=props.splitlines()
        mutated="\n".join(key+"="+value if line.startswith(key+"=") else line for line in x)
        blocked(lambda mutated=mutated:m.check_build_variant(props,mutated),
                "wrong "+key+"="+value)
    fake=dict(old)
    required={m.FSTAB,m.INIT,m.SYS_EXT_CIL,
              "IMAGES/system.img",
              "SYSTEM/system_ext/etc/selinux/system_ext_sepolicy_and_mapping.sha256",
              "SYSTEM/vendor/etc/selinux/precompiled_sepolicy",
              "SYSTEM/vendor/etc/selinux/precompiled_sepolicy.system_ext_sepolicy_and_mapping.sha256"}
    insist(required<=set(old),"archive lacks G408 delta entities")
    for name in required:
        crc,size=fake[name]
        fake[name]=(crc^0x00204A08,size+1)
    insist(set(m.compare(old,fake))==required,"synthetic permitted G408 delta rejected")
    print("PASS G408 seven expected content/compiled policy differences accepted")
    for name in sorted(m.PROTECTED):
        if name not in fake:raise AssertionError("protected item absent "+name)
        mutated=dict(fake);crc,size=mutated[name];mutated[name]=(crc^1,size)
        blocked(lambda mutated=mutated:m.compare(old,mutated),"protected "+name)
    for name in (m.FSTAB,m.INIT,m.SYS_EXT_CIL,"IMAGES/system.img"):
        mutated=dict(fake);mutated[name]=old[name]
        blocked(lambda mutated=mutated:m.compare(old,mutated),"missing required "+name)
    for name in ("IMAGES/system.img","SYSTEM/vendor/lib/libril.so"):
        mutated=dict(fake);mutated.pop(name)
        blocked(lambda mutated=mutated:m.compare(old,mutated),"deleted "+name)
    p=cil.decode()
    proposed=(p.rstrip("\n")+"\n"+"\n".join(sorted(m.RAM_ALLOW))+"\n").encode()
    m.check_exact_policy_only(cil,proposed)
    print("PASS exactly two required G408 ZRAM permissions, no other policy delta")
    mutations=[
        ("no_toolbox_allow", (p.rstrip("\n")+"\n"+next(x for x in m.RAM_ALLOW if "init ram_device" in x)+"\n").encode()),
        ("unsafe_extra_block_device",(p.rstrip("\n")+"\n"+"\n".join(sorted(m.RAM_ALLOW))+
          "\n(allow init block_device (blk_file (read write)))\n").encode()),
        ("removed_old_CIL_line",("\n".join(p.splitlines()[1:])+"\n"+
          "\n".join(sorted(m.RAM_ALLOW))+"\n").encode()),
        ("duplicate_init_allow",proposed+next(x for x in m.RAM_ALLOW if "init ram_device" in x).encode()+b"\n"),
    ]
    for name,changed in mutations:
        blocked(lambda changed=changed:m.check_exact_policy_only(cil,changed),name)
    print("G408 REAL-BASELINE HOST GATE CONTRACT PASS: expected changes only, "
          "all protected components and mutations rejected; no device writes")

if __name__=="__main__":
    main()
