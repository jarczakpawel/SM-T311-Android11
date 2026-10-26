#!/usr/bin/env python3
"""G327 signed combined G316 SYSTEM (six files) + BOOT. Staged only."""
from pathlib import Path
import json, hashlib, tempfile, zipfile, subprocess

HOME=Path("/home/debian/Projekty/samsung")
ROOT=Path("/home/debian/android/lineage-18.1-sm-t311")
BASE=HOME/"artifacts/SM-T311-G299-USB-RIL-SELINUX-20261015"
ART=HOME/"artifacts/SM-T311-G327-ATOMIC-ENFORCING-OTA-20261016"
SIX=BASE/"SM-T311-G316-G301-TO-G316-GNSS-ENFORCING-TWRP-SIGNED.zip"
BOOT=HOME/"artifacts/SM-T311-G327-XMM-TX-GENERATION-20261016/BOOT-G327-XMM-TX-GENERATION-OFFLINE.img"
OLD=BASE/"SM-T311-G301-G300-TO-G301-GNSS-RIL-TWRP-SIGNED.zip"
OUTPUT=ART/"SM-T311-G327-G301-TO-G327-BOOT-GNSS-ENFORCING-TWRP-SIGNED.zip"
EXPECTED_OLD_BOOT="c0370d97d94c2dcedd5f5405ccda2dcf63afa4e2d30f9c07e1be577591e693ed"
EXPECTED_NEW_BOOT="9487424414439edf4dd00bf4169bcd684572cc4abe6690af9589632205666436"
SIX_SHA="bac4621a87b6168af2c9c04267c666efd7a58be9bf35d5639ce8e8e02b8968c5"
sha=lambda data:hashlib.sha256(data).hexdigest()
def sha_file(p):return sha(p.read_bytes())

def main():
    rows=json.loads((BASE/"G316-FILE-OTA-MANIFEST.json").read_text())["changed_files"]
    # Six-file ABI and destination list remains unchanged. Only the GNSS
    # proxy HAL is refreshed to decouple satellite callbacks from NMEA text.
    gps_name="SYSTEM/vendor/lib/hw/gps.default.so"
    gps_file=ROOT/"out/target/product/lt013g/system/vendor/lib/hw/gps.default.so"
    gps_new_sha="e2a056d95319fe5adaba7c4e7747b77f94eb218049f230878fdcd5f1a450acca"
    assert gps_file.exists() and sha_file(gps_file)==gps_new_sha
    gps_blob=gps_file.read_bytes()
    assert len([row for row in rows if row["zip"]==gps_name])==1
    for row in rows:
        if row["zip"]==gps_name:
            row["new"]=gps_new_sha
            row["bytes"]=len(gps_blob)
    assert len(rows)==6 and sha_file(SIX)==SIX_SHA and sha_file(BOOT)==EXPECTED_NEW_BOOT
    # Measured with ls -Z on physical G301 and recorded for audit.
    # The installed TWRP carries an OLDER SELinux policy without the
    # lt013g_gps_legacy_exec type: set_metadata(selabel=...) can fail
    # because recovery cannot recognize the newer type. Importantly,
    # updater's package_extract_file opens existing paths O_TRUNC, so
    # extraction preserves inode security.selinux xattrs on these six
    # pre-existing files. Do NOT recreate/unlink before extraction.
    SELABELS = {
        "ROOT/sepolicy": "u:object_r:sepolicy_file:s0",
        "SYSTEM/vendor/etc/selinux/precompiled_sepolicy": "u:object_r:vendor_configs_file:s0",
        "SYSTEM/vendor/etc/selinux/vendor_sepolicy.cil": "u:object_r:vendor_configs_file:s0",
        "SYSTEM/vendor/bin/lt013g-gps-legacy-launcher-v195": "u:object_r:lt013g_gps_legacy_exec:s0",
        "SYSTEM/vendor/lib/hw/gps.default.so": "u:object_r:vendor_file:s0",
        "SYSTEM/vendor/etc/init/init.lt013g.gps-legacy.rc": "u:object_r:vendor_configs_file:s0",
    }
    assert {row["zip"] for row in rows} == set(SELABELS)
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
    assert 4*1024*1024 <= BOOT.stat().st_size <= 8*1024*1024 and BOOT.stat().st_size%2048==0
    bootblocks=BOOT.stat().st_size//2048
    bb="/sbin/busybox"
    scriptlines=[
        "#!/sbin/sh","set -eu",'mode="$1"',"bb="+bb,
        'boot="/dev/block/mmcblk0p9"','payload="/tmp/g327-boot.img"',
        'fail() { echo "G327: $1" >&2; exit "$2"; }',
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
       'ui_print("SM-T311 G327 combined: G316 GNSS/SELinux and matching BOOT");',
       'ifelse(is_mounted("/system"), unmount("/system"), ui_print("Mounting SYSTEM"));',
       'assert(mount("ext4","EMMC","/dev/block/mmcblk0p20","/system","rw"));',
       'assert(file_getprop("/system/system/build.prop","ro.build.version.sdk") == "30");',
       'assert(package_extract_file("META-INF/t311-g327-check.sh","/tmp/t311-g327-check.sh"));',
       'assert(package_extract_file("payload/BOOT-G327.img","/tmp/g327-boot.img"));',
       'assert(run_program("/sbin/sh","/tmp/t311-g327-check.sh","pre") == "0");',
       'show_progress(0.87,0);',
    ]
    with zipfile.ZipFile(SIX) as stage, zipfile.ZipFile(OLD) as old:
        assert stage.testzip() is None and old.testzip() is None
        for row in rows:
            name="payload/"+row["zip"]
            blob = gps_blob if row["zip"] == gps_name else stage.read(name)
            assert sha(blob)==row["new"]
            edify.append('assert(package_extract_file("'+name+'","'+row["path"]+'"));')
            edify.append('set_metadata("'+row["path"]+'","uid",0,"gid",'+str(row["gid"])+',"mode",'+row["mode"]+');')
        edify.extend([
            'assert(run_program("/sbin/sh","/tmp/t311-g327-check.sh","post_system") == "0");',
            'ui_print("SYSTEM verified; now writing BOOT last");',
            'assert(run_program("/sbin/sh","/tmp/t311-g327-check.sh","flash_boot") == "0");',
            'assert(run_program("/sbin/sh","/tmp/t311-g327-check.sh","post") == "0");',
            'unmount("/system");',
            'ui_print("G327 completed, verify Enforcing, GPS and modem after boot");',
        ])
        updater="\n".join(edify)+"\n"
        with tempfile.TemporaryDirectory(prefix="g327-") as td:
            unsigned=Path(td)/"unsigned.zip"
            with zipfile.ZipFile(unsigned,"w",zipfile.ZIP_DEFLATED,compresslevel=8) as z:
                z.writestr("META-INF/com/google/android/update-binary",old.read("META-INF/com/google/android/update-binary"),compress_type=zipfile.ZIP_STORED)
                z.writestr("META-INF/com/google/android/updater-script",updater)
                z.writestr("META-INF/t311-g327-check.sh",checks)
                z.writestr("payload/BOOT-G327.img",BOOT.read_bytes())
                for row in rows:
                    blob = gps_blob if row["zip"] == gps_name else stage.read("payload/"+row["zip"])
                    z.writestr("payload/"+row["zip"],blob)
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
        assert z.read("META-INF/t311-g327-check.sh")==checks.encode()
        assert sha(z.read("payload/BOOT-G327.img"))==EXPECTED_NEW_BOOT
        script_bytes=z.read("META-INF/com/google/android/updater-script")
        assert b'"selabel"' not in script_bytes
        for row in rows:
            assert sha(z.read("payload/"+row["zip"]))==row["new"]
            assert ('set_metadata("'+row["path"]+'"').encode() in script_bytes
        assert "recovery.img" not in z.namelist()
    m={
        "stage":"G327","physical_installed":False,
        "source_live_G301_BOOT_full_partition_sha256":EXPECTED_OLD_BOOT,
        "BOOT_partition":"/dev/block/mmcblk0p9","BOOT_size":8388608,
        "source_six_file_ota_sha256":SIX_SHA,
        "new_gps_proxy_hal_sha256":gps_new_sha,
        "new_gps_proxy_gnss_callback_independent_of_raw_nmea":True,
        "remaining_five_payloads_unchanged_from_g316":True,
        "target_BOOT_sha256":EXPECTED_NEW_BOOT,"BOOT_bytes":BOOT.stat().st_size,
        "SYSTEM_six_file_sha_prechecked":True,"SYSTEM_six_file_sha_postchecked":True,
        "BOOT_partition_SHA_prechecked":True,"BOOT_payload_SHA_prechecked":True,
        "BOOT_write_last_after_SYSTEM_postcheck":True,
        "BOOT_payload_verification_after_write":True,
        "recovery_efs_userdata_unmodified":True,
        "updater_script":"META-INF/com/google/android/updater-script",
        "signed_combined_ota_sha256":sha_file(OUTPUT),"signed_combined_ota_bytes":OUTPUT.stat().st_size,
        "rows":rows,
        "physical_selinux_labels_per_file": SELABELS,
        "set_metadata_explicit_selabel": False,
        "preserve_existing_security_selinux_via_O_TRUNC": True,
        "installed_twrp_policy_has_gps_exec_type": False,
        "installed_twrp_policy_sha256": "3a91bab38aafbcb058c203e5fb0d206e909365177415a6ed303d043d87aad08f",
        "warning":"Built/tested OFFLINE. Requires verified TWRP recovery execution, user present, Android11 compatibility and actual boot tests; does not guarantee rollback."
    }
    (ART/"G327-COMBINED-MANIFEST.json").write_text(json.dumps(m,indent=2)+"\n")
    print(json.dumps({k:m[k] for k in ("stage","signed_combined_ota_sha256","signed_combined_ota_bytes","BOOT_write_last_after_SYSTEM_postcheck","physical_installed")},indent=2))
if __name__=="__main__":main()
