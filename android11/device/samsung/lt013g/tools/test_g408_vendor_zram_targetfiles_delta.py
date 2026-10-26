#!/usr/bin/env python3
"""Release gate for SM-T311 G408 source-only vendor ZRAM candidate target-files.

Never flashes or modifies a tablet. Compare against archived immutable,
physically staged signed G403 reference; reject any unrelated binary mutation.
"""
import argparse
import hashlib
import sys
from pathlib import Path
from zipfile import ZipFile
from collections import Counter

FSTAB = "SYSTEM/vendor/etc/fstab.lt013g"
INIT = "SYSTEM/vendor/etc/init/hw/init.lt013g.rc"
GPS = "SYSTEM/vendor/lib/hw/gps.default.so"
CHANGES = {FSTAB, INIT, "IMAGES/system.img", "IMAGES/cache.img",
           "IMAGES/userdata.img", "ROOT/sepolicy", "SYSTEM/build.prop",
           "SYSTEM/vendor/build.prop", "SYSTEM/product/build.prop",
           "SYSTEM/system_ext/build.prop", "SYSTEM/vendor/odm/etc/build.prop",
           "SYSTEM/system_ext/etc/selinux/system_ext_sepolicy.cil",
           "SYSTEM/system_ext/etc/selinux/system_ext_sepolicy_and_mapping.sha256",
           "SYSTEM/vendor/etc/selinux/precompiled_sepolicy",
           "SYSTEM/vendor/etc/selinux/precompiled_sepolicy.system_ext_sepolicy_and_mapping.sha256"}
PROTECTED = {
    "IMAGES/boot.img", "BOOTABLE_IMAGES/boot.img",
    "IMAGES/recovery.img", "BOOTABLE_IMAGES/recovery.img",
    "SYSTEM/vendor/etc/recovery.img",
    "RECOVERY/RAMDISK/etc/recovery.fstab",
    "SYSTEM/vendor/lib/libril.so",
    "SYSTEM/vendor/lib/t311-ril/libsec-ril.so",
    "SYSTEM/lib/libaudiohal.so",
    "SYSTEM/vendor/lib/hw/audio.primary.lt013g.so",
    "SYSTEM/vendor/etc/lt013g-gps-legacy/immutable/system/lib/hw/gps.default.so",
    "SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil",
    "SYSTEM/etc/selinux/plat_sepolicy.cil",
    "SYSTEM/etc/selinux/plat_sepolicy_and_mapping.sha256",
    "SYSTEM/vendor/etc/selinux/precompiled_sepolicy.plat_sepolicy_and_mapping.sha256",
    "SYSTEM/vendor/etc/selinux/vendor_file_contexts",
    "SYSTEM/framework/telephony-common.jar",
    "SYSTEM/framework/arm/boot-telephony-common.art",
    "SYSTEM/framework/arm/boot-telephony-common.oat",
    "SYSTEM/framework/boot-telephony-common.vdex",
    GPS,
}
BOOT_HASH = "cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd"
TWRP_HASH = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
GPS_HASH = "8cba224bb2f77a4d829b6ff7a4f1da1461357b0cad3eba9d704ae7e56e88db5b"

def insist(ok,msg):
    if not ok: raise AssertionError(msg)

def check_build_variant(old_props, new_props):
    def property_value(contents, key):
        matches=[line.split("=",1)[1] for line in contents.splitlines()
                 if line.startswith(key+"=")]
        insist(len(matches)==1, "missing or duplicate build property "+key)
        return matches[0]
    for key in ("ro.build.type","ro.build.flavor"):
        previous=property_value(old_props,key)
        candidate=property_value(new_props,key)
        insist(previous==candidate,
               "candidate build variant differs from signed/staged G403: "+
               key+" expected="+previous+" actual="+candidate)
    insist(property_value(new_props, "ro.build.type")=="userdebug",
           "G408 release must be the matching ARM32 userdebug build")

PROP_CHANGES = {
    "SYSTEM/build.prop": {
        "ro.build.date",
        "ro.build.date.utc",
        "ro.build.description",
        "ro.build.display.id",
        "ro.build.version.incremental",
        "ro.system.build.date",
        "ro.system.build.date.utc",
        "ro.system.build.fingerprint",
        "ro.system.build.version.incremental",
    },
    "SYSTEM/vendor/build.prop": {
        "ro.bootimage.build.date",
        "ro.bootimage.build.date.utc",
        "ro.bootimage.build.fingerprint",
        "ro.vendor.build.date",
        "ro.vendor.build.date.utc",
        "ro.vendor.build.fingerprint",
        "ro.vendor.build.version.incremental",
    },
    "SYSTEM/product/build.prop": {
        "ro.product.build.date", "ro.product.build.date.utc",
        "ro.product.build.fingerprint", "ro.product.build.version.incremental",
    },
    "SYSTEM/system_ext/build.prop": {
        "ro.system_ext.build.date", "ro.system_ext.build.date.utc",
        "ro.system_ext.build.fingerprint", "ro.system_ext.build.version.incremental",
    },
    "SYSTEM/vendor/odm/etc/build.prop": {
        "ro.odm.build.date", "ro.odm.build.date.utc",
        "ro.odm.build.fingerprint", "ro.odm.build.version.incremental",
    },
}

def check_generated_build_properties_only(old_contents, new_contents, path):
    """No unexpected keys added, deleted or silently changed in Android props."""
    insist(path in PROP_CHANGES, "unsupported build properties path")
    def properties(contents):
        result={}
        for line in contents.decode().splitlines():
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            insist("=" in line, "unexpected non-property content in "+path)
            key,value=line.split("=",1)
            insist(key and key not in result, "duplicate or empty build property "+key)
            result[key]=value
        return result
    old=properties(old_contents)
    new=properties(new_contents)
    insist(set(old)==set(new), "G408 build properties keys added or removed "+path)
    changed={key for key in old if old[key]!=new[key]}
    insist(changed==PROP_CHANGES[path],
           "G408 unexpected build property values: "+repr(sorted(changed ^ PROP_CHANGES[path])))
    variant_key={
        "SYSTEM/build.prop": "ro.build.type",
        "SYSTEM/vendor/build.prop": "ro.vendor.build.type",
        "SYSTEM/product/build.prop": "ro.product.build.type",
        "SYSTEM/system_ext/build.prop": "ro.system_ext.build.type",
        "SYSTEM/vendor/odm/etc/build.prop": "ro.odm.build.type",
    }[path]
    insist(new.get(variant_key)=="userdebug",
           "unexpected ROM/partition build variant")
    for key in changed:
        if "fingerprint" in key:
            insist(":userdebug/test-keys" in new[key],
                   "unexpected build fingerprint domain "+key)
        if key.endswith("version.incremental"):
            insist(new[key].startswith("eng.debian.20261017."),
                   "unexpected build incremental profile "+key)
    return True

SYS_EXT_CIL = "SYSTEM/system_ext/etc/selinux/system_ext_sepolicy.cil"
RAM_ALLOW = {
    "(allow init ram_device (blk_file (ioctl read write getattr lock append map open watch watch_reads)))",
    "(allow toolbox ram_device (blk_file (ioctl read write getattr lock append map open watch watch_reads)))",
}
def check_exact_policy_only(old_contents,new_contents):
    old=Counter(old_contents.decode().splitlines())
    new=Counter(new_contents.decode().splitlines())
    added=list((new-old).elements())
    removed=list((old-new).elements())
    insist(len(added)==2 and set(added)==RAM_ALLOW and not removed,
           "G408 SELinux new privileges NOT exactly init+toolbox ram_device: "+repr((sorted(added), sorted(removed))))

def compare(old,new):
    insist(set(old)==set(new), "file added/removed from targetfiles")
    changed = {n for n in old if old[n]!=new[n]}
    insist({FSTAB,INIT,SYS_EXT_CIL,"IMAGES/system.img"}.issubset(changed),
           "G408 active vendor config, compiled SELinux and SYSTEM image must change")
    insist(changed.issubset(CHANGES),
           "unexpected G408 targetfiles delta: "+repr(sorted(changed-CHANGES)))
    for name in PROTECTED:
        insist(name in old and old[name]==new[name],
               "protected OEM/G403 file modified: "+name)
    return sorted(changed)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--old",type=Path,required=True)
    ap.add_argument("--new",type=Path,required=True)
    a=ap.parse_args()
    insist(a.old.is_file() and a.new.is_file(),"missing targetfiles")
    with ZipFile(a.old) as z0, ZipFile(a.new) as z1:
        old={f.filename:(f.CRC,f.file_size) for f in z0.infolist()}
        new={f.filename:(f.CRC,f.file_size) for f in z1.infolist()}
        old_build_props=z0.read("SYSTEM/build.prop").decode()
        check_build_variant(old_build_props,
                            z1.read("SYSTEM/build.prop").decode())
        for prop_path in PROP_CHANGES:
            check_generated_build_properties_only(z0.read(prop_path),
                                                  z1.read(prop_path),
                                                  prop_path)
        changed=compare(old,new)
        check_exact_policy_only(z0.read(SYS_EXT_CIL),z1.read(SYS_EXT_CIL))
        # Android11 root policy is the exact same compiled policy shipped by
        # vendor. It MUST change with the two intended system_ext allows,
        # and MUST NOT contain a different policy than this verified binary.
        insist("ROOT/sepolicy" in changed,
               "compiled root merged SELinux policy did not change")
        insist(z1.read("ROOT/sepolicy") ==
               z1.read("SYSTEM/vendor/etc/selinux/precompiled_sepolicy"),
               "root and vendor precompiled SELinux differ")
        insist(z0.read("ROOT/sepolicy") ==
               z0.read("SYSTEM/vendor/etc/selinux/precompiled_sepolicy"),
               "reference G403 root and precompiled SELinux differ")
        for name in ("SYSTEM/system_ext/etc/selinux/system_ext_sepolicy_and_mapping.sha256",
                     "SYSTEM/vendor/etc/selinux/precompiled_sepolicy",
                     "SYSTEM/vendor/etc/selinux/precompiled_sepolicy.system_ext_sepolicy_and_mapping.sha256"):
            insist(name in changed,
                   "rebuilt/compiled policy metadata is unexpectedly unchanged: "+name)
        fs=z1.read(FSTAB).decode()
        rc=z1.read(INIT).decode()
        insist(fs.count("/dev/block/zram0 none swap defaults zramsize=805306368,swapprio=32758,wait")==1,
               "active fstab missing G408 768MiB zram")
        insist(rc.count("    swapon_all /vendor/etc/fstab.lt013g")==1,
               "actual Android11 vendor init fails to activate swap")
        insist(rc.count("    write /proc/sys/vm/page-cluster 0")==1 and
               "write /proc/sys/vm/swappiness" not in rc,
               "G408 vendor_init page-cluster policy unsafe or missing")
        insist("/misc emmc defaults defaults" in fs,
               "factory p11 /misc recovery BCB fix removed")
        insist(hashlib.sha256(z1.read(GPS)).hexdigest()==GPS_HASH,
               "G403 complete GNSS CP retries changed unexpectedly")
        insist(hashlib.sha256(z1.read("IMAGES/boot.img")).hexdigest()==BOOT_HASH,
               "factory BOOT changed")
        insist(hashlib.sha256(z1.read("IMAGES/recovery.img")).hexdigest()==TWRP_HASH,
               "factory TWRP changed")
        insist(b"misc_block_device" in z1.read("SYSTEM/vendor/etc/selinux/vendor_file_contexts"),
               "p11 original SELinux policy changed")
        insist(b"addLt013gPdpGatewayHostRoute" in z1.read("SYSTEM/framework/telephony-common.jar"),
               "G387 guarded 3G /32 host route lost")
        insist(all("T311GnssStatus" not in n and "T311NtpTransportProbe" not in n
                   for n in new), "diagnostic APK escaped into targetfiles")
        for name in (FSTAB,INIT):
            insist(z0.read(name)!=z1.read(name),"old G403 file identical to G408 "+name)
    # Demonstrate build-flavor regression is a hard release failure, without
    # generating another full 1.1GiB archive.
    baseline_props=old_build_props
    check_build_variant(baseline_props, baseline_props)
    try:
        check_build_variant(baseline_props,
                            baseline_props.replace("ro.build.type=userdebug",
                                                   "ro.build.type=eng"))
    except AssertionError:
        print("PASS reject G408 eng-vs-G403 userdebug mismatch")
    else:
        raise AssertionError("unsafe build variant changed without detection")
    # Prove the policy safety contract rejects additional capabilities,
    # silently deleted old allowances and omitted ZRAM grants.
    with ZipFile(a.old) as z0:
        reference_policy=z0.read(SYS_EXT_CIL)
    verified_old=reference_policy.decode()
    def forged_policy(extra_lines):
        return (verified_old.rstrip("\n")+"\n"+"\n".join(extra_lines)+"\n").encode()
    check_exact_policy_only(reference_policy,forged_policy(sorted(RAM_ALLOW)))
    for label, broken in (
        ("no_toolbox_allow", forged_policy([next(x for x in RAM_ALLOW if "init ram_device" in x)])),
        ("overbroad_eMMC_device", forged_policy(sorted(RAM_ALLOW)+["(allow init block_device (blk_file (read write)))"])),
        ("dropping_existing_rules", ("\n".join(verified_old.splitlines()[1:])+"\n"+"\n".join(sorted(RAM_ALLOW))).encode()),
    ):
        try:check_exact_policy_only(reference_policy,broken)
        except AssertionError:print("PASS reject unsafe SELinux mutant",label)
        else:raise AssertionError("G408 policy fail-open "+label)
    mutants=[
        ("OEM_RIL","SYSTEM/vendor/lib/libril.so"),
        ("GNSS_binary",GPS),
        ("framework_cellular","SYSTEM/framework/telephony-common.jar"),
        ("kernel_boot","IMAGES/boot.img"),
        ("factory_TWRP","IMAGES/recovery.img"),
        ("private_binder_SELinux","SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil"),
        ("factory_p11_label","SYSTEM/vendor/etc/selinux/vendor_file_contexts"),
        ("old_Samsung_audio","SYSTEM/lib/libaudiohal.so"),
    ]
    for label,n in mutants:
        pretend=dict(new)
        c,length=pretend[n]
        pretend[n]=(c^1,length)
        try:compare(old,pretend)
        except AssertionError:print("PASS reject unsafe delta",label)
        else:raise AssertionError("FAILED protection "+label)
    print("G408 TARGETFILES STRICT PASS: only intended vendor fstab/init and generated images")
    print("G408 changed files",changed)
    print("8/8 critical OEM/G403 mutations rejected, no tablet write")

if __name__=="__main__":
    main()
