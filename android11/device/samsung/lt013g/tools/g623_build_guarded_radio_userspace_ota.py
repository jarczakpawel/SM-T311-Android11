#!/usr/bin/env python3
"""G623 signed guarded radio-userspace-only TWRP release.

Writes only four vendor bin files after exact boot/recovery/GPS and
old-or-new file checksum validation. In case of a partially successful
install, re-running the same signed ZIP is idempotent; never writes
raw partition images, CP firmware, modem NV, SIM or recovery.
"""
from pathlib import Path
from hashlib import sha256
import subprocess
import zipfile
import tempfile
import json

ANDROID = Path("/home/debian/android/lineage-18.1-sm-t311")
PORT = Path("/home/debian/Projekty/samsung/linux-7.3-port")
BASE = PORT / "artifacts/G597-EXACT-KERNEL-RELEASE/G597-BOOT-ONLY-TWRP-SIGNED.zip"
BASE_SHA = "8542edf6c90deba04b3e171123c4d44b45e57f9614267a75598a5f71036899a1"
OUT = PORT / "artifacts/G623-RADIO-USERSPACE-FOURFILE-OTA"
ZIP = OUT / "G623-AUDIO-MODEM-FOURFILE-TWRP-SIGNED.zip"
BOOT = "7022bffae070737ff3eec9da218f754d4d569f9b14821ce8fb3ebfa967dc1e28"
REC = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f"
GPS = "102b7ea8f6536baa6a7db7fc8a946f5d29f49e387519bc7d8580489a2f671df2"
SYSTEM_SIZE = 2235564032
FILES = {
    "t311-audio-ril": {
        "old": "79faf768b1af78cca260018988950e2de8f8ec591e9022f5b5f47ae159cb4041",
        "new": "afcfb90944f53746c05e3bd033d5421782a0428eccf473b8060fe133603f8e65",
        "label": "u:object_r:rild_exec:s0",
    },
    "t311-mobile-session-probe": {
        "old": "9b08074bd13ca6535257bffa817a103e58165a4554438488e92b1a98496947aa",
        "new": "8b7f15f0f58ef225c56a39dccec9dea232d6cb3937eaea818e56aafe058fd330",
        "label": "u:object_r:lt013g_modem_watchdog_probe_exec:s0",
    },
    "t311-modem-watchdog": {
        "old": "dfab4aeb0f2c59b904bff9fcdc988e464f2e911021864f12475a597ca4c159c6",
        "new": "f8b203ff22d101894e7ffa7c0c48cd4d7ca93c9b2bdf3a71c5926a35adfff86c",
        "label": "u:object_r:lt013g_modem_watchdog_exec:s0",
    },
    "t311-cbd-pidfd-signal": {
        "old": None,
        "new": "e9b08ba4055d8ee3f988d80bc3bfb8df7b0af204ec4214102704c5f5178ef048",
        "label": "u:object_r:lt013g_modem_watchdog_probe_exec:s0",
    },
}
def sha(data): return sha256(data).hexdigest()

def create():
    if sha(BASE.read_bytes()) != BASE_SHA: raise RuntimeError("unexpected TWRP reference binary")
    blobs = {}
    for name, entry in FILES.items():
        data = (ANDROID/"out/target/product/lt013g/system/vendor/bin"/name).read_bytes()
        if sha(data) != entry["new"]: raise RuntimeError(f"{name} has changed")
        if not (data.startswith(b"\x7fELF") or name == "t311-modem-watchdog" and
                data.startswith(b"#!/vendor/bin/sh")): raise RuntimeError(f"{name} format mismatch")
        blobs[name] = data
    # All preconditions executed before package_extract_file, fail closed.
    guard = """#!/sbin/sh
set -eu
bb=/sbin/busybox
fail() { echo "G623 REFUSED: $1" >&2; exit 1; }
hash() { "$bb" sha256sum "$1" | "$bb" cut -d" " -f1; }
[ -b /dev/block/mmcblk0p9 ] && [ -b /dev/block/mmcblk0p10 ] && [ -b /dev/block/mmcblk0p20 ] || fail "partition"
[ "$("$bb" blockdev --getsize64 /dev/block/mmcblk0p20)" = "__SIZE__" ] || fail "SYSTEM size"
[ "$(hash /dev/block/mmcblk0p9)" = "__BOOT__" ] || fail "BOOT hash"
[ "$(hash /dev/block/mmcblk0p10)" = "__REC__" ] || fail "RECOVERY hash"
[ "$(hash /system/system/vendor/lib/hw/gps.default.so)" = "__GPS__" ] || fail "G621 GPS hash"
case "$1" in pre|post) ;; *) fail "phase" ;; esac
""".replace("__SIZE__",str(SYSTEM_SIZE)).replace("__BOOT__", BOOT).replace("__REC__", REC).replace("__GPS__", GPS)
    updater = """ui_print("G623: guarded four-file radio+audio vendor update");
ifelse(is_mounted("/system"),unmount("/system"),ui_print("Mounting G623 SYSTEM"));
assert(mount("ext4","EMMC","/dev/block/mmcblk0p20","/system","rw"));
assert(file_getprop("/system/system/build.prop","ro.build.version.sdk") == "30");
assert(package_extract_file("META-INF/g623-guard.sh","/tmp/g623-guard.sh"));
assert(run_program("/sbin/sh","/tmp/g623-guard.sh","pre") == "0");
"""
    for name, entry in FILES.items():
        remote = f"/system/system/vendor/bin/{name}"
        guard += f"""p="{remote}"
if [ "$1" = pre ]; then
"""
        if entry["old"] is None:
            guard += f""" if [ -e "$p" ]; then [ "$(hash "$p")" = "{entry["new"]}" ] || fail "unknown existing {name}"; fi
"""
        else:
            guard += f""" [ -f "$p" ] || fail "missing {name}"
 curr="$(hash "$p")"
 [ "$curr" = "{entry["old"]}" ] || [ "$curr" = "{entry["new"]}" ] || fail "unexpected {name}"
"""
        guard += f"""else
 [ -f "$p" ] && [ "$(hash "$p")" = "{entry["new"]}" ] || fail "post-hash {name}"
fi
"""
        updater += f'''assert(package_extract_file("payload/{name}","{remote}"));
set_metadata("{remote}","uid",0,"gid",2000,"mode",0755,"selabel","{entry["label"]}");
'''
    guard += 'exit 0\n'
    updater += """assert(run_program("/sbin/sh","/tmp/g623-guard.sh","post") == "0");
unmount("/system");
ui_print("G623: all four vendor hashes VERIFIED; no BOOT, CP, EFS or recovery writes");
set_progress(1.000000);
"""
    with zipfile.ZipFile(BASE) as z:
        if z.testzip() is not None: raise RuntimeError("G597 reference broken")
        updater_binary = z.read("META-INF/com/google/android/update-binary")
    if ZIP.exists(): raise FileExistsError("release immutable; refuse overwrite")
    OUT.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="g623-") as d:
        unsigned=Path(d)/"unsigned.zip"
        with zipfile.ZipFile(unsigned,"w",zipfile.ZIP_DEFLATED) as z:
            z.writestr("META-INF/com/google/android/update-binary",updater_binary,compress_type=zipfile.ZIP_STORED)
            z.writestr("META-INF/com/google/android/updater-script",updater)
            z.writestr("META-INF/g623-guard.sh",guard)
            for name,data in blobs.items():z.writestr("payload/"+name,data)
        script=Path(d)/"guard.sh";script.write_text(guard)
        subprocess.run(["sh","-n",str(script)],check=True)
        subprocess.run(["java","-Xmx900m","-Djava.library.path="+str(ANDROID/"out/host/linux-x86/lib64"),
                        "-jar",str(ANDROID/"out/host/linux-x86/framework/signapk.jar"),"-w",
                        str(ANDROID/"build/make/target/product/security/testkey.x509.pem"),
                        str(ANDROID/"build/make/target/product/security/testkey.pk8"),
                        str(unsigned),str(ZIP)],cwd=ANDROID,check=True)
    with zipfile.ZipFile(ZIP) as z:
        if z.testzip() is not None:raise RuntimeError("signed ZIP CRC failed")
        for name,data in blobs.items():assert z.read("payload/"+name)==data
        assert z.read("META-INF/g623-guard.sh")==guard.encode()
        assert z.read("META-INF/com/google/android/updater-script")==updater.encode()
        assert all(n.startswith("META-INF/") or n in {"payload/"+x for x in blobs} for n in z.namelist())
    manifest={"release":"G623","state":"SIGNED_HOST_ONLY_NOT_INSTALLED",
              "system_write_files":["/system/system/vendor/bin/"+x for x in blobs],
              "files":FILES,"expected_boot":BOOT,"expected_recovery":REC,
              "expected_g621_gps":GPS,"zip_sha256":sha(ZIP.read_bytes()),"zip_bytes":ZIP.stat().st_size}
    (OUT/"G623_MANIFEST.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print(json.dumps(manifest,indent=2))
if __name__=="__main__":create()
