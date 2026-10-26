#!/usr/bin/env python3
"""G321 signed combined G316 SYSTEM (six files) + BOOT. Staged only."""
from pathlib import Path
import json, hashlib, tempfile, zipfile, subprocess

HOME=Path("/home/debian/Projekty/samsung")
ROOT=Path("/home/debian/android/lineage-18.1-sm-t311")
BASE=HOME/"artifacts/SM-T311-G299-USB-RIL-SELINUX-20261015"
ART=HOME/"artifacts/SM-T311-G321-ATOMIC-ENFORCING-OTA-20261016"
SIX=BASE/"SM-T311-G316-G301-TO-G316-GNSS-ENFORCING-TWRP-SIGNED.zip"
BOOT=HOME/"artifacts/SM-T311-G321-XMM-RX-SUSPEND-20261016/BOOT-G321-XMM-SUSPEND-OFFLINE.img"
OLD=BASE/"SM-T311-G301-G300-TO-G301-GNSS-RIL-TWRP-SIGNED.zip"
OUTPUT=ART/"SM-T311-G321-G301-TO-G321-BOOT-GNSS-ENFORCING-TWRP-SIGNED.zip"
EXPECTED_OLD_BOOT="c0370d97d94c2dcedd5f5405ccda2dcf63afa4e2d30f9c07e1be577591e693ed"
EXPECTED_NEW_BOOT="bf208df3954a59d453b5a30ea06442e5b4078d96507cfe1a067efa3f3759f578"
SIX_SHA="bac4621a87b6168af2c9c04267c666efd7a58be9bf35d5639ce8e8e02b8968c5"
sha=lambda data:hashlib.sha256(data).hexdigest()
def sha_file(p):return sha(p.read_bytes())

def main():
    rows=json.loads((BASE/"G316-FILE-OTA-MANIFEST.json").read_text())["changed_files"]
    assert len(rows)==6 and sha_file(SIX)==SIX_SHA and sha_file(BOOT)==EXPECTED_NEW_BOOT
    # Android 11 mounts the SYSTEM block as / (system-as-root), but TWRP
    # mounts that same filesystem at /system. Prefix live root paths.
    assert {row["path"] for row in rows} == {
        "/system/sepolicy",
        "/system/system/vendor/etc/selinux/precompiled_sepolicy",
        "/system/system/vendor/etc/selinux/vendor_sepolicy.cil",
        "/system/system/vendor/bin/lt013g-gps-legacy-launcher-v195",
        "/system/system/vendor/lib/hw/gps.default.so",
        "/system/system/vendor/etc/init/init.lt013g.gps-legacy.rc",
    }
    assert BOOT.stat().st_size==6838272 and BOOT.stat().st_size%2048==0
    bootblocks=BOOT.stat().st_size//2048
    bb="/sbin/busybox"
    scriptlines=[
        "#!/sbin/sh","set -eu",'mode="$1"',"bb="+bb,
        'boot="/dev/block/mmcblk0p9"','payload="/tmp/g321-boot.img"',
        'fail() { echo "G321: $1" >&2; exit "$2"; }',
        'hash() { $bb sha256sum "$1" | $bb cut -d" " -f1; }',
        'boothead() { $bb dd if="$boot" bs=2048 count='+str(bootblocks)+' 2>/dev/null | $bb sha256sum | $bb cut -d" " -f1; }',
        '[ -b "$boot" ] || fail "no BOOT device" 61',
        '# Android 11 uses /dev/block/by-name; the actual TWRP recovery',
        '# fstab uses /dev/block/platform/dw_mmc/by-name/BOOT instead.',
        'if [ -L /dev/block/platform/dw_mmc/by-name/BOOT ]; then',
        ' alias=/dev/block/platform/dw_mmc/by-name/BOOT',
        'elif [ -L /dev/block/by-name/BOOT ]; then',
        ' alias=/dev/block/by-name/BOOT',
        'else fail "no TWRP/Android BOOT alias" 62; fi',
        '[ "$($bb readlink -f "$alias")" = "$boot" ] || fail "unexpected BOOT mapping" 63',
        '[ "$(hash "$payload")" = "'+EXPECTED_NEW_BOOT+'" ] || fail "invalid BOOT payload" 64',
        'if [ "$mode" = flash_boot ]; then',
    ]
    # No standalone BOOT write is ever allowed with old/mixed GNSS policy.
    for row in rows:
        scriptlines.append(' [ "$(hash "' + row["path"] + '")" = "' + row["new"] + '" ] || fail "SYSTEM not ready for Enforcing BOOT" 74')
    scriptlines.extend([
        ' current="$(boothead)"',
        ' if [ "$current" != "'+EXPECTED_NEW_BOOT+'" ]; then',
        '   [ "$(hash "$boot")" = "'+EXPECTED_OLD_BOOT+'" ] || fail "unknown physical BOOT before flash" 65',
        '   $bb dd if="$payload" of="$boot" bs=2048 || fail "BOOT write failed" 66',
        '   $bb sync',
        ' fi',
        ' [ "$(boothead)" = "'+EXPECTED_NEW_BOOT+'" ] || fail "BOOT write verification failed" 67',
        ' exit 0',
        'fi',
    ])
    scriptlines.extend([
        'check() {',
        ' f="$1"; old="$2"; new="$3"; [ -f "$f" ] || fail "missing system file" 68',
        ' sum="$(hash "$f")"',
        ' if [ "$mode" = pre ]; then',
        '   [ "$sum" = "$old" ] || [ "$sum" = "$new" ] || fail "unexpected old SYSTEM file" 69',
        ' else [ "$sum" = "$new" ] || fail "SYSTEM file mismatch" 70; fi',
        '}',
    ])
    for row in rows:
        scriptlines.append('check "'+row["path"]+'" "'+row["old"]+'" "'+row["new"]+'"')
    scriptlines.extend([
        'if [ "$mode" = pre ]; then',
        ' [ "$(boothead)" = "'+EXPECTED_NEW_BOOT+'" ] || [ "$(hash "$boot")" = "'+EXPECTED_OLD_BOOT+'" ] || fail "unknown old BOOT" 71',
        'elif [ "$mode" = post ]; then',
        ' [ "$(boothead)" = "'+EXPECTED_NEW_BOOT+'" ] || fail "BOOT mismatch after update" 72',
        'elif [ "$mode" != post_system ]; then fail "unknown phase" 73; fi',
        'exit 0',
    ])
    checks="\n".join(scriptlines)+"\n"
    edify=[
       'ui_print("SM-T311 G321 combined: G316 GNSS/SELinux and matching BOOT");',
       'ifelse(is_mounted("/system"), unmount("/system"), ui_print("Mounting SYSTEM"));',
       'assert(mount("ext4","EMMC","/dev/block/mmcblk0p20","/system","rw"));',
       'assert(file_getprop("/system/system/build.prop","ro.build.version.sdk") == "30");',
       'assert(package_extract_file("META-INF/t311-g321-check.sh","/tmp/t311-g321-check.sh"));',
       'assert(package_extract_file("payload/BOOT-G321.img","/tmp/g321-boot.img"));',
       'assert(run_program("/sbin/sh","/tmp/t311-g321-check.sh","pre") == "0");',
       'show_progress(0.87,0);',
    ]
    with zipfile.ZipFile(SIX) as stage, zipfile.ZipFile(OLD) as old:
        assert stage.testzip() is None and old.testzip() is None
        for row in rows:
            name="payload/"+row["zip"]
            assert sha(stage.read(name))==row["new"]
            edify.append('assert(package_extract_file("'+name+'","'+row["path"]+'"));')
            edify.append('set_metadata("'+row["path"]+'","uid",0,"gid",'+str(row["gid"])+',"mode",'+row["mode"]+');')
        edify.extend([
            'assert(run_program("/sbin/sh","/tmp/t311-g321-check.sh","post_system") == "0");',
            'ui_print("SYSTEM verified; now writing BOOT last");',
            'assert(run_program("/sbin/sh","/tmp/t311-g321-check.sh","flash_boot") == "0");',
            'assert(run_program("/sbin/sh","/tmp/t311-g321-check.sh","post") == "0");',
            'unmount("/system");',
            'ui_print("G321 completed, verify Enforcing, GPS and modem after boot");',
        ])
        updater="\n".join(edify)+"\n"
        with tempfile.TemporaryDirectory(prefix="g321-") as td:
            unsigned=Path(td)/"unsigned.zip"
            with zipfile.ZipFile(unsigned,"w",zipfile.ZIP_DEFLATED,compresslevel=8) as z:
                z.writestr("META-INF/com/google/android/update-binary",old.read("META-INF/com/google/android/update-binary"),compress_type=zipfile.ZIP_STORED)
                z.writestr("META-INF/com/google/android/updater-script",updater)
                z.writestr("META-INF/t311-g321-check.sh",checks)
                z.writestr("payload/BOOT-G321.img",BOOT.read_bytes())
                for row in rows:z.writestr("payload/"+row["zip"],stage.read("payload/"+row["zip"]))
            subprocess.run([
                "java","-Xmx1024m","-Djava.library.path="+str(ROOT/"out/host/linux-x86/lib64"),
                "-jar",str(ROOT/"out/host/linux-x86/framework/signapk.jar"),"-w",
                str(ROOT/"build/make/target/product/security/testkey.x509.pem"),
                str(ROOT/"build/make/target/product/security/testkey.pk8"),
                str(unsigned),str(OUTPUT)
            ],check=True,cwd=ROOT)
    with zipfile.ZipFile(OUTPUT) as z:
        assert z.testzip() is None
        assert z.read("META-INF/com/google/android/updater-script")==updater.encode()
        assert z.read("META-INF/t311-g321-check.sh")==checks.encode()
        assert sha(z.read("payload/BOOT-G321.img"))==EXPECTED_NEW_BOOT
        for row in rows:assert sha(z.read("payload/"+row["zip"]))==row["new"]
        assert "recovery.img" not in z.namelist()
    m={
        "stage":"G321","physical_installed":False,
        "source_live_G301_BOOT_full_partition_sha256":EXPECTED_OLD_BOOT,
        "BOOT_partition":"/dev/block/mmcblk0p9","BOOT_size":8388608,
        "source_six_file_ota_sha256":SIX_SHA,
        "target_BOOT_sha256":EXPECTED_NEW_BOOT,"BOOT_bytes":BOOT.stat().st_size,
        "SYSTEM_six_file_sha_prechecked":True,"SYSTEM_six_file_sha_postchecked":True,
        "BOOT_partition_SHA_prechecked":True,"BOOT_payload_SHA_prechecked":True,
        "BOOT_write_last_after_SYSTEM_postcheck":True,
        "BOOT_payload_verification_after_write":True,
        "recovery_efs_userdata_unmodified":True,
        "updater_script":"META-INF/com/google/android/updater-script",
        "signed_combined_ota_sha256":sha_file(OUTPUT),"signed_combined_ota_bytes":OUTPUT.stat().st_size,
        "rows":rows,
        "warning":"Built/tested OFFLINE. Requires verified TWRP recovery execution, user present, Android11 compatibility and actual boot tests; does not guarantee rollback."
    }
    (ART/"G321-COMBINED-MANIFEST.json").write_text(json.dumps(m,indent=2)+"\n")
    print(json.dumps({k:m[k] for k in ("stage","signed_combined_ota_sha256","signed_combined_ota_bytes","BOOT_write_last_after_SYSTEM_postcheck","physical_installed")},indent=2))
if __name__=="__main__":main()
