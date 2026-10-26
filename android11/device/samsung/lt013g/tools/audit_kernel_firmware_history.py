#!/usr/bin/env python3
"""Read-only kernel Git history + OEM firmware provenance matrix.

Hashes only: never includes/exports firmware binary data or changes kernel Git.
Known 7 staged kernel firmware inputs from the local SM-T311 Linux6.12 tree.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

KERNEL = Path("/home/debian/Projekty/samsung/kernel")
VENDOR = Path("/home/debian/android/lineage-18.1-sm-t311/vendor/samsung/lt01-common/proprietary")
FILES = (
    ("firmware/brcm/BCM4334B0.hcd",
     "system/bin/bcm4334.hcd"),
    ("firmware/brcm/BCM4334B0.samsung,t311.hcd",
     "vendor/firmware/brcm/BCM4334B0.samsung,t311.hcd"),
    ("firmware/brcm/brcmfmac4334-sdio.samsung,t311.bin",
     "vendor/firmware/brcm/brcmfmac4334-sdio.samsung,t311.bin"),
    ("firmware/brcm/brcmfmac4334-sdio.samsung,t311.txt",
     "vendor/firmware/brcm/brcmfmac4334-sdio.samsung,t311.txt"),
    ("firmware/regulatory.db", "vendor/firmware/regulatory.db"),
    ("firmware/regulatory.db.p7s", "vendor/firmware/regulatory.db.p7s"),
    ("firmware/s5p-mfc.fw", "vendor/firmware/s5p-mfc.fw"),
)


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1048576), b""):
            h.update(chunk)
    return h.hexdigest()


def git(*cmd):
    return subprocess.run(["git", "-C", str(KERNEL), *cmd],
                          check=True, capture_output=True).stdout.decode().strip()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    tracked = set(git("ls-files").splitlines())
    historical = git("rev-list", "--objects", "--all")
    matrix = []
    for kernel_rel, vendor_rel in FILES:
        k = KERNEL / kernel_rel
        v = VENDOR / vendor_rel
        if not k.is_file() or not v.is_file():
            raise SystemExit(f"Required comparison file missing: {kernel_rel} {vendor_rel}")
        if kernel_rel not in tracked:
            raise SystemExit(f"Tracked kernel firmware path unexpectedly absent: {kernel_rel}")
        kh, vh = sha(k), sha(v)
        matrix.append({
            "kernel_path": kernel_rel,
            "vendor_source_path": str(Path("vendor/samsung/lt01-common/proprietary") / vendor_rel),
            "kernel_size": k.stat().st_size,
            "vendor_size": v.stat().st_size,
            "kernel_sha256": kh,
            "vendor_sha256": vh,
            "byte_identical": kh == vh and k.stat().st_size == v.stat().st_size,
            "current_git_index_tracked": True,
            "known_blob_in_reachable_git_history": kernel_rel in historical,
            "redistribution_rights_verified": False,
        })
    assert len(matrix) == 7
    assert sum(i["byte_identical"] for i in matrix) == 6, "Known firmware comparison changed"
    result = {
        "device": "Samsung SM-T311 kernel 6.12 source",
        "kernel_head": git("rev-parse", "HEAD"),
        "status": "FAIL-PUBLIC-GIT-RELEASE-PREFLIGHT",
        "reason": "Seven firmware/regulatory/config assets are tracked in kernel Git; rights and notices not cleared",
        "files": matrix,
        "matching_stock_oem_assets": 6,
        "mismatching_stock_oem_assets": 1,
        "kernel_git_history_contains_firmware_objects": True,
        "history_notes": [
            "A working-tree .gitignore or deleting a tracked file in a new commit does NOT remove its historical Git objects.",
            "GitHub publishing a repository with the current local reachable history carries old OEM bytes unless release provenance/rights are established or a clean source-only history is curated.",
            "Preserve local runtime firmware and working kernel build inputs; no firmware removal, Git history rewrite, push or device flashing is performed here.",
            "A clean source-only snapshot can be built from audited text sources without including original firmware objects, but kernel/ROM builds still require lawfully obtained local vendor inputs.",
            "regulatory.db and regulatory.db.p7s may have separate upstream license or redistribution conditions; this audit does not decide them.",
        ],
    }
    out = args.output.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print("G335 kernel firmware: tracked", len(matrix),
          "stock-identical", result["matching_stock_oem_assets"],
          "stock-different", result["mismatching_stock_oem_assets"])
    for row in matrix:
        print("HASH MATCH" if row["byte_identical"] else "HASH DIFF",
              row["kernel_path"], row["kernel_sha256"],
              "vendor", row["vendor_sha256"])
    print("G335 public kernel Git publish gate BLOCKED, report", out)


if __name__ == "__main__":
    main()
