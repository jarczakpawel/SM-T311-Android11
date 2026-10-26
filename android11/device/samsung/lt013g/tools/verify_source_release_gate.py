#!/usr/bin/env python3
"""Fail closed before publishing the SM-T311 device/kernel source Git repos.

Checks *reachable Git history*, not only index, for known private firmware
paths and binary filenames. Does not grant OEM distribution permission.
Never reads, copies, deletes, pushes or transmits firmware contents.
"""
from __future__ import annotations

from pathlib import Path
import argparse
import subprocess
import sys

DEVICE = Path(__file__).resolve().parents[1]
KERNEL = Path("/home/debian/Projekty/samsung/kernel")
PRIVATE_PREFIXES = (
    "gps-v195/immutable/",
    "radio/prebuilt/",
    "prebuilt/",
    "vendor/samsung/",
    "vendor/proprietary/",
    "proprietary/",
)
PRIVATE_BASENAMES = {
    "libsec-ril.so", "libwrappergps.so", "t311-cbd",
    "recovery-current.img", "gpsd", "cbd",
    "libmali.so",
}
PRIVATE_EXTENSIONS = {".hcd", ".mbn", ".fw"}


def prohibited_path(name: str) -> str | None:
    """Return grounded exclusion reason or None for an ordinary source path."""
    path = name.strip().replace("\\", "/").lower()
    if not path:
        return None
    if path.startswith("/") or "/../" in f"/{path}/":
        return "unsafe or traversing path"
    for prefix in PRIVATE_PREFIXES:
        if path.startswith(prefix) or f"/{prefix}" in path:
            return "private/vendor firmware directory"
    base = path.rsplit("/", 1)[-1]
    if base in PRIVATE_BASENAMES:
        return "known stock binary/firmware basename"
    if base.startswith("legacy-rootfs") and base.endswith((".tar", ".tgz", ".tar.gz")):
        return "original or staged Broadcom rootfs archive"
    if path.startswith("firmware/") or "/firmware/" in path:
        if base.endswith((".bin", ".db", ".p7s")):
            return "prebuilt firmware or signed regulatory binary in kernel Git"
        if base.endswith(".txt") and ("brcmfmac" in base or "nvram" in base):
            return "device-specific Broadcom firmware/NVRAM config in kernel Git"
    if base.endswith(".img") and ("recovery" in base or "stock" in base):
        return "stock/recovery disk image"
    if Path(base).suffix in PRIVATE_EXTENSIONS:
        return "likely original device firmware image"
    return None


def git(repo: Path, *args: str) -> bytes:
    return subprocess.run(
        ["git", "-C", str(repo), *args], check=True, capture_output=True
    ).stdout


def tracked_paths(repo: Path) -> list[str]:
    bare = git(repo, "rev-parse", "--is-bare-repository").strip() == b"true"
    args = ("ls-tree", "-rz", "--name-only", "HEAD") if bare else ("ls-files", "-z")
    return [p.decode("utf-8", "surrogateescape") for p in
            git(repo, *args).split(b"\0") if p]

def historical_paths(repo: Path) -> list[str]:
    out = git(repo, "rev-list", "--objects", "--all")
    # --all includes every local ref and all reachable historical trees. It
    # does not examine remote-only commits never fetched to this checkout.
    objects = []
    for record in out.splitlines():
        _, sep, path = record.partition(b" ")
        if sep and path:
            objects.append(path.decode("utf-8", "surrogateescape"))
    return objects


def assess(repo: Path) -> dict:
    tracked = tracked_paths(repo)
    history = historical_paths(repo)
    offenders = []
    for source, names in (("git index", tracked), ("reachable Git history", history)):
        for name in names:
            why = prohibited_path(name)
            if why:
                offenders.append(f"{source}: {name}: {why}")
    return {
        "repo": str(repo),
        "tracked_count": len(tracked),
        "historical_path_occurrences": len(history),
        "offenders": sorted(set(offenders)),
    }


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--device", type=Path, default=DEVICE)
    p.add_argument("--kernel", type=Path, default=KERNEL)
    a = p.parse_args()
    errors = []
    for repo in (a.device, a.kernel):
        has_worktree = (repo / ".git").exists()
        has_bare_git = (repo / "HEAD").exists() and (repo / "objects").is_dir()
        if not (has_worktree or has_bare_git):
            print(f"ERROR: not a local worktree or bare Git repository: {repo}",
                  file=sys.stderr)
            return 3
        check = assess(repo)
        print("REPO", check["repo"])
        print("TRACKED", check["tracked_count"],
              "HISTORICAL_PATH_OCCURRENCES", check["historical_path_occurrences"])
        errors.extend(check["offenders"])
    if errors:
        print("BLOCK SOURCE RELEASE: private binary path(s) in Git refs", file=sys.stderr)
        for x in errors:
            print(" -", x, file=sys.stderr)
        return 2
    print("PASS: no known private GNSS/RIL/firmware paths in local Git index/history")
    print("LIMIT: local refs only; remote-only Git refs, opaque embedded binaries, "
          "legal rights/attribution and build artifacts are NOT certified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
