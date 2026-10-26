#!/usr/bin/env python3
"""G769 source-only release prerequisite: verify already-owned private ROM inputs.

Read only. Never stages/copies/commits OEM assets and never interacts with
Android/ADB. A successful check does NOT establish redistribution rights.
The corresponding local private tar is checked against its pinned SHA and
the 48 compiled immutable files must match BOTH manifest and tar members.
"""
from __future__ import annotations

import hashlib
import tarfile
from pathlib import Path

DEVICE = Path(__file__).resolve().parents[1]
GPS = DEVICE / "gps-v195"
IMMUTABLE = GPS / "immutable"
EXPECTED_ARCHIVE_SHA = "559c4ac19e3be326788a429c890e3d72d08aff5ca324ac1d40ad5a3c679b83ae"
RADIO_SHA = {
    "t311-cbd": "03a5758257050aa8ccf3b3e3e46b2f8b054acb4bdb968220dc0af8f8e8adaca4",
    "libsec-ril.so": "432658fd34c966a8ed16b9548ca8d015383d26c6c732826a43363131d31e3a94",
    "libsecnativefeature.so": "32f5dff375f45c6423b882e97c50103e5eb3e0b7928b848a43f8d70534b0f100",
    "libstlport.so": "c0ca1cf2ca60ca80ac7898a036f41d71625d85c2f32b88251a79d7465d26803f",
    "libt311_cutils_compat.so": "b080a16607afaac0438c5b1806964c19a191605cd1959b9904d31d86108923c7",
}
MUTABLE_SHA = "43c98f44d0b33e3d1abc581682af31b18782e3686a60b1f2aa5f765117282299"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            h.update(chunk)
    return h.hexdigest()


def checked_regular(path: Path, expected: str) -> None:
    if not path.is_file() or path.is_symlink():
        raise ValueError(f"G769 MISSING/UNSAFE INPUT: {path}")
    if sha256(path) != expected:
        raise ValueError(f"G769 PRIVATE INPUT SHA MISMATCH: {path}")


def check() -> None:
    rootfs = GPS / "legacy-rootfs.tar"
    checked_regular(rootfs, EXPECTED_ARCHIVE_SHA)
    checked_regular(GPS / "legacy-rootfs-mutable.tar", MUTABLE_SHA)

    entries = (GPS / "PRIVATE_IMMUTABLE_INPUT_SHA256.txt").read_text().splitlines()
    expected = {}
    for line in entries:
        digest, rel = line.split("  ", 1)
        path = Path(rel)
        if (len(digest) != 64 or not all(c in "0123456789abcdef" for c in digest)
                or path.is_absolute() or ".." in path.parts or rel in expected):
            raise ValueError("G769 malformed/duplicate private input manifest")
        expected[rel] = digest
    if len(expected) != 48:
        raise ValueError(f"G769 wrong number of immutable inputs: {len(expected)}")
    actual = {
        p.relative_to(IMMUTABLE).as_posix()
        for p in IMMUTABLE.rglob("*") if p.is_file()
    }
    if actual != set(expected):
        raise ValueError(
            f"G769 immutable input inventory mismatch: absent={sorted(set(expected)-actual)}, "
            f"unexpected={sorted(actual-set(expected))}"
        )
    rules = (GPS / "immutable-code.mk").read_text()
    for rel, digest in expected.items():
        checked_regular(IMMUTABLE / rel, digest)
        path = f"device/samsung/lt013g/gps-v195/immutable/{rel}:"
        if rules.count(path) != 1:
            raise ValueError(f"G769 missing/duplicate Android PRODUCT_COPY_FILES: {rel}")

    # Compare the actual build inputs against the authenticated private
    # archive: do not accept a matching local manifest with unrelated tar.
    with tarfile.open(rootfs, "r") as archive:
        members = {
            member.name.removeprefix("./"): member
            for member in archive if member.isfile()
        }
        for rel, digest in expected.items():
            member = members.get(rel)
            if member is None:
                raise ValueError(f"G769 private archive member absent: {rel}")
            stream = archive.extractfile(member)
            if stream is None:
                raise ValueError(f"G769 private archive member unreadable: {rel}")
            h = hashlib.sha256()
            for data in iter(lambda: stream.read(1048576), b""):
                h.update(data)
            if h.hexdigest() != digest:
                raise ValueError(f"G769 private archive/staged mismatch: {rel}")

    for rel, digest in RADIO_SHA.items():
        checked_regular(DEVICE / "radio/prebuilt" / rel, digest)
    print("G769_PRIVATE_BUILD_INPUTS_PASS: 48 GNSS members, 5 radio blobs, "
          "mutable rootfs and private archive matched")
    print("G769_SOURCE_ONLY: no firmware copied or tracked; redistribution NOT approved")


if __name__ == "__main__":
    try:
        check()
    except (OSError, ValueError, tarfile.TarError) as error:
        raise SystemExit(str(error))
