#!/usr/bin/env python3
"""Host regression for real G379 signed TWRP pre/post safety, no device writes.

The real signed checker is extracted and all device paths replaced with
temporary regular files and safe symbolic links; no real partitions accessed.
"""
from pathlib import Path
import hashlib
import subprocess
import tempfile
import zipfile

HOME=Path("/home/debian/Projekty/samsung")
ART=HOME/"artifacts/SM-T311-G379-LIVE-OTA-20261016"
SIGNED=ART/"SM-T311-G379-GUARDED-BOOT-GPS-CARRIER-TWRP-SIGNED.zip"
OLD=HOME/"artifacts/SM-T311-G373-HSIC-DELAYED-PDP-20261016/G373-BOOT-PADDED.img"
NEW_HASH="cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd"
OLD_HASH="f9292bab437bce12cea56e6e0c067a15c87475b69a61fc0c13fd4d3f07b9694f"

def sha(data):return hashlib.sha256(data).hexdigest()
with zipfile.ZipFile(SIGNED) as z:
    assert z.testzip() is None
    oldscript=z.read("META-INF/com/google/android/updater-script").decode()
    g=z.read("META-INF/t311-g379-check.sh").decode()
    newest=z.read("boot.img")
    assert sha(newest)==NEW_HASH
    assert oldscript.count('"pre"')==1 and oldscript.count('"post"')==1
    assert oldscript.index('"pre"')<oldscript.index("block_image_update(")<oldscript.index('"post"')
    assert "format(" not in oldscript and "wipe_data" not in oldscript
old=OLD.read_bytes()
assert sha(old)==OLD_HASH
assert len(old)==len(newest)
assert Path("/usr/bin/busybox").is_file()

with tempfile.TemporaryDirectory(prefix="g379-failclosed-") as td:
    root=Path(td)
    boot=root/"synthetic-boot.bin"
    system=root/"synthetic-system.bin"
    boot_alias=root/"by-name-BOOT"
    system_alias=root/"by-name-SYSTEM"
    boot_alias.symlink_to(boot)
    system_alias.symlink_to(system)
    system.write_bytes(b"test-system-block")
    real=g
    g=g.replace("bb=/sbin/busybox","bb=/usr/bin/busybox")
    g=g.replace("boot=/dev/block/mmcblk0p9",f"boot={boot}")
    g=g.replace("system=/dev/block/mmcblk0p20",f"system={system}")
    g=g.replace('[ -b "$boot" ]','[ -f "$boot" ]')
    g=g.replace('[ -b "$system" ]','[ -f "$system" ]')
    g=g.replace('/dev/block/platform/dw_mmc/by-name/BOOT',str(boot_alias))
    g=g.replace('/dev/block/platform/dw_mmc/by-name/SYSTEM',str(system_alias))
    script=root/"recovery-preflight.sh"
    script.write_text(g);script.chmod(0o700)
    assert sha(old)==OLD_HASH
    def run(phase):
        return subprocess.run(["/bin/sh",str(script),phase],capture_output=True,text=True)
    boot.write_bytes(old)
    assert run("pre").returncode==0,"old known system not accepted"
    assert run("post").returncode!=0,"old boot should not pass postcondition"
    boot.write_bytes(newest)
    assert run("pre").returncode==0,"idempotent new boot should pass"
    assert run("post").returncode==0,"new boot should pass"
    boot.write_bytes(bytes(len(old)))
    assert run("pre").returncode!=0,"unknown boot accepted"
    assert run("post").returncode!=0,"corrupt boot accepted"
    boot.write_bytes(old)
    system_alias.unlink()
    assert run("pre").returncode!=0,"wrong partition mapping accepted"
    system_alias.symlink_to(system)
    boot_alias.unlink()
    assert run("pre").returncode!=0,"missing BOOT symlink accepted"
    assert run("invalid").returncode!=0,"invalid preflight phase accepted"
print("G379_TWRP_SIGNED_OTA_FAILCLOSED_HOST_PASS cases=9 BOOT=G373->G378 userdata_erased=0 physical_device_touched=0")
