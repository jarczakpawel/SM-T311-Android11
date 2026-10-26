#!/usr/bin/env python3
import bisect
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from xml.etree import ElementTree

target_path, ota_path = map(Path, sys.argv[1:3])
with zipfile.ZipFile(target_path) as target, zipfile.ZipFile(ota_path) as ota:
    if target.testzip() or ota.testzip():
        raise SystemExit("ZIP CRC failed")
    script = ota.read("META-INF/com/google/android/updater-script").decode()
    if "block_image_update(" not in script or 'package_extract_file("boot.img", "/dev/block/platform/dw_mmc/by-name/BOOT")' not in script:
        raise SystemExit("Unexpected OTA installer")
    if "/RECOVERY" in script or "recovery.img" in ota.namelist():
        raise SystemExit("Unexpected recovery modification")
    ota_boot = hashlib.sha256(ota.read("boot.img")).hexdigest()
    target_boot = hashlib.sha256(target.read("IMAGES/boot.img")).hexdigest()
    if ota_boot != target_boot or target.getinfo("IMAGES/recovery.img").file_size != 8388608 or ota.getinfo("boot.img").file_size > 8388608:
        raise SystemExit("BOOT mismatch or partition capacity mismatch")
    if hashlib.sha256(target.read("IMAGES/recovery.img")).hexdigest() != "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f":
        raise SystemExit("Target-files RECOVERY differs from physical TWRP")
    manifest = ElementTree.fromstring(target.read("SYSTEM/vendor/etc/vintf/manifest.xml"))
    for service, interface in (("android.hardware.audio", "IDevicesFactory"),
                               ("android.hardware.audio.effect", "IEffectsFactory")):
        if not any(hal.get("format") == "hidl" and
                   hal.findtext("name") == service and
                   hal.findtext("version") == "6.0" and
                   any(node.findtext("name") == interface and
                       node.findtext("instance") == "default"
                       for node in hal.findall("interface"))
                   for hal in manifest.findall("hal")):
            raise SystemExit("Missing mandatory audio VINTF service: " + service)
    transfer = ota.read("system.transfer.list").decode().splitlines()
    if transfer[0] != "4" or transfer[2:4] != ["0", "0"]:
        raise SystemExit("Unexpected transfer list version")
    expected_blocks = int(transfer[1])
    with tempfile.TemporaryDirectory(prefix="t311-ota-audit-") as tmp:
        sparse_path = Path(tmp) / "system.img"
        compressed_path = Path(tmp) / "system.new.dat.br"
        with target.open("IMAGES/system.img") as src, sparse_path.open("wb") as dst:
            shutil.copyfileobj(src, dst, 1024 * 1024)
        with ota.open("system.new.dat.br") as src, compressed_path.open("wb") as dst:
            shutil.copyfileobj(src, dst, 1024 * 1024)
        with sparse_path.open("rb") as sparse:
            magic, major, minor, file_hdr, chunk_hdr, block_size, total_blocks, total_chunks, _ = struct.unpack("<IHHHHIIII", sparse.read(28))
            if magic != 0xED26FF3A or major != 1 or block_size != 4096 or total_blocks != 545792:
                raise SystemExit("Unexpected SYSTEM sparse header")
            sparse.seek(file_hdr)
            chunks = []
            starts = []
            cursor = 0
            for _ in range(total_chunks):
                kind, _, count, size = struct.unpack("<HHII", sparse.read(12))
                offset = sparse.tell() + chunk_hdr - 12
                if kind == 0xCAC1 and size != chunk_hdr + count * block_size:
                    raise SystemExit("Invalid RAW sparse chunk")
                if kind == 0xCAC2 and size != chunk_hdr + 4:
                    raise SystemExit("Invalid FILL sparse chunk")
                if kind == 0xCAC3 and size != chunk_hdr:
                    raise SystemExit("Invalid DONT_CARE sparse chunk")
                if kind not in (0xCAC1, 0xCAC2, 0xCAC3):
                    raise SystemExit("Unexpected sparse chunk")
                starts.append(cursor)
                chunks.append((cursor + count, kind, offset))
                sparse.seek(offset + size - chunk_hdr)
                cursor += count
            if cursor != total_blocks:
                raise SystemExit("Sparse block count mismatch")
            process = subprocess.Popen(["out/host/linux-x86/bin/brotli", "-d", "-c", str(compressed_path)], stdout=subprocess.PIPE)
            compared = 0
            zeroed = 0
            try:
                for line in transfer[4:]:
                    operation, ranges = line.split(" ", 1)
                    if operation not in ("new", "zero", "erase"):
                        raise ValueError("Unexpected transfer operation: " + operation)
                    numbers = [int(x) for x in ranges.split(",")]
                    if len(numbers) != numbers[0] + 1 or numbers[0] % 2:
                        raise ValueError("Invalid transfer ranges")
                    for start, end in zip(numbers[1::2], numbers[2::2]):
                        if not 0 <= start < end <= total_blocks:
                            raise ValueError("Transfer beyond SYSTEM partition")
                        if operation == "zero":
                            zeroed += end - start
                        if operation == "erase":
                            continue
                        if operation == "new":
                            compared += end - start
                        while start < end:
                            idx = bisect.bisect_right(starts, start) - 1
                            last, kind, offset = chunks[idx]
                            n = min(end, last) - start
                            if kind == 0xCAC1:
                                sparse.seek(offset + (start - starts[idx]) * block_size)
                                expected = sparse.read(n * block_size)
                            elif kind == 0xCAC2:
                                sparse.seek(offset)
                                expected = sparse.read(4) * (n * block_size // 4)
                            else:
                                expected = bytes(n * block_size)
                            if operation == "new":
                                actual = process.stdout.read(len(expected))
                                if actual != expected:
                                    raise ValueError("SYSTEM OTA data differs from target-files at block " + str(start))
                            elif expected.strip(b"\x00"):
                                raise ValueError("SYSTEM zero range contains nonzero data at block " + str(start))
                            start += n
                if compared + zeroed != expected_blocks or process.stdout.read(1):
                    raise ValueError("OTA compressed stream size differs from transfer list")
            finally:
                process.stdout.close()
                exit_code = process.wait()
            if exit_code:
                raise SystemExit("Brotli decode failed")
    with ota_path.open("rb") as fd:
        ota_sha256 = hashlib.file_digest(fd, "sha256").hexdigest()
    print(json.dumps({"status": "PASS", "ota_boot_sha256": ota_boot, "system_new_blocks": compared, "system_zero_blocks": zeroed, "system_bytes_compared": compared * block_size, "system_partition_blocks": total_blocks, "recovery_written": False, "ota_sha256": ota_sha256}, indent=2))
