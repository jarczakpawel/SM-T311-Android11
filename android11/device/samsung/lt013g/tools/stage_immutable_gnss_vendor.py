#!/usr/bin/env python3
import hashlib
import shutil
import sys
import tarfile
import tempfile
from pathlib import Path

from split_legacy_gnss_rootfs import run

base = Path(__file__).resolve().parent.parent / "gps-v195"
source = base / "legacy-rootfs.tar"
if hashlib.file_digest(source.open("rb"), "sha256").hexdigest() != "559c4ac19e3be326788a429c890e3d72d08aff5ca324ac1d40ad5a3c679b83ae":
    sys.exit("GNSS source archive differs from verified G261 input")

stage = base / "immutable"
if stage.exists() and any(stage.rglob("*")):
    sys.exit("GNSS private stage exists; refusing to overwrite it")

with tempfile.TemporaryDirectory(prefix="t311-gnss-") as temporary:
    result = run(source, Path(temporary))
    if result["immutable_file_count"] != 48 or result["mutable_file_count"] != 7:
        sys.exit("GNSS split differs from verified G261 manifest")
    with tarfile.open(Path(temporary) / result["immutable_tar"]) as archive:
        for member in archive:
            if member.isdir():
                continue
            if not member.isfile():
                sys.exit("Unexpected GNSS entry type")
            destination = stage / member.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(member) as source_file, destination.open("wb") as output:
                shutil.copyfileobj(source_file, output)
            destination.chmod(member.mode & 0o777)
    shutil.copyfile(Path(temporary) / result["mutable_tar"], base / "legacy-rootfs-mutable.tar")

files = sorted(path.relative_to(stage).as_posix() for path in stage.rglob("*") if path.is_file())
source_prefix = "device/samsung/lt013g/gps-v195/immutable/"
vendor_prefix = "$(TARGET_COPY_OUT_VENDOR)/etc/lt013g-gps-legacy/immutable/"
rules = [f"    {source_prefix}{name}:{vendor_prefix}{name}" for name in files]
(base / "immutable-code.mk").write_text("PRODUCT_COPY_FILES += \\\n" + " \\\n".join(rules) + "\n")
print(f"GNSS immutable staged: {len(files)} files; source and split verified; proprietary bytes remain untracked")
