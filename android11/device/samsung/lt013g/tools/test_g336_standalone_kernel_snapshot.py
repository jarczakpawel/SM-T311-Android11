#!/usr/bin/env python3
"""G336 independent clean kernel Git root: exact 7 exclusions, no copied blobs."""
from pathlib import Path
import json
import subprocess

BASE = Path("/home/debian/Projekty/samsung/artifacts/SM-T311-G336-CLEAN-KERNEL-SOURCE-20261016")
ORIGINAL = Path("/home/debian/Projekty/samsung/kernel")
PUB = BASE / "kernel-source-only-standalone.git"
META = BASE / "kernel-source-only-git-metadata.json"
SUSPECT = (
    "firmware/brcm/BCM4334B0.hcd",
    "firmware/brcm/BCM4334B0.samsung,t311.hcd",
    "firmware/brcm/brcmfmac4334-sdio.samsung,t311.bin",
    "firmware/brcm/brcmfmac4334-sdio.samsung,t311.txt",
    "firmware/regulatory.db", "firmware/regulatory.db.p7s",
    "firmware/s5p-mfc.fw",
)


def cmd(repo, *args, expect=0):
    proc = subprocess.run(["git", "-C", str(repo), *args], capture_output=True)
    assert proc.returncode == expect, (repo, args, proc.stderr.decode(errors="replace"))
    return proc.stdout.decode("utf8", "surrogateescape").strip()


def names(repo, commit):
    return set(filter(None, cmd(repo, "ls-tree", "-r", "--name-only", commit).splitlines()))


def main():
    meta = json.loads(META.read_text())
    src_ref = meta["original_kernel_head"]
    dst_ref = meta["source_only_root_commit"]
    assert meta["removed_source_paths"] == list(SUSPECT)
    assert cmd(ORIGINAL, "rev-parse", "HEAD") == src_ref
    assert cmd(PUB, "rev-parse", "HEAD") == dst_ref
    assert cmd(PUB, "rev-list", "--count", "--all") == "1"
    assert cmd(PUB, "cat-file", "-p", dst_ref).startswith("tree ")
    assert len(cmd(PUB, "rev-list", "--parents", "--all").split()) == 1
    assert not (PUB/"objects/info/alternates").exists()
    src_names = names(ORIGINAL, src_ref)
    dst_names = names(PUB, dst_ref)
    assert src_names - dst_names == set(SUSPECT)
    assert dst_names - src_names == set()
    assert len(src_names) == 86776 and len(dst_names) == 86769
    print("PASS original kernel tree equals source-only tree plus EXACT seven excluded firmware")
    for name in SUSPECT:
        blob = cmd(ORIGINAL, "rev-parse", src_ref + ":" + name)
        cmd(PUB, "cat-file", "-e", blob, expect=1)
    print("PASS all seven original firmware blobs absent from independent Git object storage")
    for name, blob in meta["sample_source_blob_ids_unmodified"].items():
        assert cmd(ORIGINAL, "rev-parse", src_ref + ":" + name) == blob
        assert cmd(PUB, "rev-parse", dst_ref + ":" + name) == blob
    print("PASS unchanged ARM32 defconfig/DTS, XMM6262 driver, Makefile and COPYING blobs")
    cmd(PUB, "fsck", "--full", "--no-reflogs")
    print("PASS standalone Git object integrity; no alternates and only one root commit")
    gate = Path(__file__).with_name("verify_source_release_gate.py")
    subprocess.run(
        ["python3", str(gate), "--kernel", str(PUB)], check=True,
        stdout=subprocess.DEVNULL)
    print("PASS complete device + standalone kernel known private path release gate")
    print("LIMIT: open-source/proprietary licenses and all upstream notices still require review")


if __name__ == "__main__":
    main()
