#!/usr/bin/env python3
"""Generate read-only TWRP V4-equivalent G408 guard from a FINAL signed G408 OTA.

Host-side only. No adb, mount, reboot, flash, block-device writes or
modifications to the input archive. Refuse unknown OTA names and template
changes; output never overwrites an existing preflight.
"""
import argparse
import hashlib
import re
from pathlib import Path
from zipfile import ZipFile

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "g403_twrp_readonly_preflight_v4.sh"
SOURCE_SHA = "d7f1c8fd0e0c58d39d26797324a4f5a1e98ea06900bd91810d51b06b43c8dd2d"
G403_OTA = "SM-T311-G403-GUARDED-GPS-SUPL-CP-RETRY-SIGNED.zip"
G403_SHA = "b6df8dcab3af2c2afd41271f607f90ed511ce6df342fff172cfa6794b355a1b9"
G408_OTA = "SM-T311-G408-GUARDED-ZRAM-GPS-SUPL-SIGNED.zip"
G408_GATE = "SM-T311-G408-TWRP-READONLY-PREFLIGHT.sh"
EXPECTED_BOOT_SHA = "cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd"
EXPECTED_OLD_BOOT_SHA = "f9292bab437bce12cea56e6e0c067a15c87475b69a61fc0c13fd4d3f07b9694f"

def require(cond, message):
    if not cond:
        raise ValueError(message)

def render(template, signed_name, signed_sha, release):
    require(release in ("G403","G408"),"invalid release")
    require(re.fullmatch(r"[0-9a-f]{64}",signed_sha) is not None,"invalid signed SHA256")
    require("/" not in signed_name and signed_name.endswith(".zip"),
            "signed basename must be a simple ZIP filename")
    require(template.count("signed=/data/local/tmp/"+G403_OTA)==1,"unexpected template signed path")
    require(template.count("want="+G403_SHA)==1,"unexpected template hash")
    require(template.count(EXPECTED_BOOT_SHA)==1 and
            template.count(EXPECTED_OLD_BOOT_SHA)==1,"template BOOT guard missing")
    require("system_dev" in template and "separator < 7" in template,
            "template must retain V4 mountinfo safeguards")
    output=template.replace("signed=/data/local/tmp/"+G403_OTA,
                            "signed=/data/local/tmp/"+signed_name)
    output=output.replace("want="+G403_SHA,"want="+signed_sha)
    if release=="G408":
        output=output.replace("SM-T311-G403-TWRP-READONLY-PREFLIGHT-V4.sh",G408_GATE)
        output=output.replace("G403","G408")
    require("signed=/data/local/tmp/"+signed_name in output,"generated signed path absent")
    require("want="+signed_sha in output,"generated SHA absent")
    require("dd if=\"$boot\" bs=2048 count=3339" in output,
            "physical BOOT prefix guard modified")
    require("mount_state" in output and "SYSTEM mounted or mountinfo invalid" in output,
            "V4 failclosed mount guard disappeared")
    return output

# Physical G379 read-only block measurement: 4,366,336 sectors of 512 B,
# exactly 545,792 blocks of 4,096 B. Never accept out-of-bounds SYSTEM writes.
SYSTEM_BLOCKS = 545792

def validate_full_system_transfer(raw):
    """Strict non-A/B full system block transfer range parser, no device writes."""
    try:
        lines=raw.decode("ascii").splitlines()
    except UnicodeError as e:
        raise ValueError("non-ASCII transfer list") from e
    require(len(lines)>=5 and lines[:4][0]=="4",
            "only Android11 non-A/B transfer-list version4 allowed")
    require(lines[1:4][1:]==["0","0"],
            "no stash blocks or stash entry slots permitted")
    require(lines[1].isdigit() and 0 < int(lines[1]) <= SYSTEM_BLOCKS,
            "invalid written system block count")
    all_ranges=[]
    written=0
    commands=set()
    for line in lines[4:]:
        fields=line.split(" ")
        require(len(fields)==2 and fields[0] in {"erase","new","zero"},
                "non-full-OTA, stash, patch or unsupported transfer op")
        command,table=fields
        commands.add(command)
        require(table and all(part.isdigit() for part in table.split(",")),
                "invalid transfer-block range characters")
        nums=[int(part) for part in table.split(",")]
        require(len(nums)>=3 and nums[0]==len(nums)-1 and nums[0]%2==0,
                "transfer-block pair encoding malformed")
        for first,stop in zip(nums[1::2],nums[2::2]):
            require(0<=first<stop<=SYSTEM_BLOCKS,
                    "out-of-bounds, reversed or empty SYSTEM block range")
            all_ranges.append((first,stop))
            if command!="erase":
                written+=stop-first
    require(commands=={"erase","new","zero"},
            "complete full SYSTEM image must specify erase,new,zero")
    require(written==int(lines[1]),
            "declared image blocks differ from NEW+ZERO operations")
    all_ranges.sort()
    require(all_ranges and all_ranges[0][0]==0 and
            all_ranges[-1][1]==SYSTEM_BLOCKS,
            "transfer list does not cover exact physical SYSTEM p20 boundary")
    for previous,current in zip(all_ranges,all_ranges[1:]):
        require(previous[1]==current[0],
                "SYSTEM ranges overlap or leave uninitialized gaps")
    return {"system_blocks":SYSTEM_BLOCKS,
            "written_new_or_zero":written,
            "all_ranges_count":len(all_ranges)}

def source_template():
    data=SOURCE.read_bytes()
    require(hashlib.sha256(data).hexdigest()==SOURCE_SHA,
            "V4 trusted read-only preflight source SHA has changed")
    return data.decode()

def check_signed_archive(path):
    require(path.is_file() and path.name==G408_OTA,
            "only exact signed G408 release basename accepted")
    with ZipFile(path) as z:
        names=z.namelist()
        require(len(names)==len(set(names)),"duplicate OTA entries")
        require("recovery.img" not in names and "userdata.img" not in names,
                "unexpected recovery/data payload in signed OTA")
        for name in ("META-INF/t311-g379-check.sh",
                     "META-INF/com/google/android/updater-script",
                     "boot.img", "system.new.dat.br", "system.transfer.list",
                     "META-INF/com/android/otacert"):
            require(name in names,"missing complete signed OTA field "+name)
        boot=z.read("boot.img")
        require(len(boot)==6838272 and
                hashlib.sha256(boot).hexdigest()==EXPECTED_BOOT_SHA,
                "unexpected BOOT image, refuse gate creation")
        transfer=validate_full_system_transfer(z.read("system.transfer.list"))
        require(z.getinfo("system.new.dat.br").file_size>10000000,
                "signed OTA has no full compressed SYSTEM payload")
        require(z.getinfo("system.patch.dat").file_size==0,
                "incremental SYSTEM patch payload forbidden")
        guard=z.read("META-INF/t311-g379-check.sh").decode()
        require(EXPECTED_BOOT_SHA in guard and EXPECTED_OLD_BOOT_SHA in guard,
                "signed OTA guard not pinned to G379 / expected BOOT")
        updater=z.read("META-INF/com/google/android/updater-script").decode()
        markers=[
            'assert(run_program("/sbin/sh", "/tmp/t311-g379-check.sh", "pre")',
            'block_image_update("/dev/block/platform/dw_mmc/by-name/SYSTEM"',
            'package_extract_file("boot.img", "/dev/block/platform/dw_mmc/by-name/BOOT")',
            'assert(run_program("/sbin/sh", "/tmp/t311-g379-check.sh", "post")'
        ]
        require(all(updater.count(x)==1 for x in markers),
                "signed OTA guarded updater structure mismatch")
        require(all(updater.index(x)<updater.index(y) for x,y in zip(markers,markers[1:])),
                "signed OTA installation order mismatch")
        require(not any(x in updater for x in ("format(", "wipe_data", "recovery.img", '"/data"')),
                "forbidden data/recovery action in signed OTA updater")
    sha=hashlib.sha256()
    with path.open("rb") as f:
        for part in iter(lambda:f.read(4*1024*1024),b""):
            sha.update(part)
    return sha.hexdigest()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--signed",type=Path,required=True)
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args()
    signed_sha=check_signed_archive(args.signed)
    require(args.out.name==G408_GATE,"incorrect G408 gate output basename")
    require(not args.out.exists(),"refusing overwrite existing G408 preflight")
    script=render(source_template(),G408_OTA,signed_sha,"G408")
    args.out.write_text(script)
    args.out.chmod(0o755)
    print("G408 GATE GENERATED HOST ONLY",args.out,
          "bytes",args.out.stat().st_size,
          "script_sha256",hashlib.sha256(script.encode()).hexdigest(),
          "signed_ota_sha256",signed_sha,
          "NO_DEVICE_OPERATIONS")
if __name__=="__main__":
    main()
