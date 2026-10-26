#!/usr/bin/env python3
"""June-2026-onward SM-T311 file provenance inventory.

Read-only. Modification times are discovery clues, not authorship proof.
Includes current worktree, untracked assets and preserved WIP commit history.
Does not modify build outputs, binaries, source trees or the physical device.
"""
import argparse
import datetime as dt
import hashlib
import json
import os
import subprocess
from collections import Counter
from pathlib import Path

REPOS = ("frameworks/base", "frameworks/native", "frameworks/av",
         "external/mesa3d", "external/libdrm", "hardware/samsung",
         "device/samsung/lt013g", "device/samsung/lt01-common",
         "packages/apps/Trebuchet", "packages/providers/MediaProvider",
         "external/drm_hwcomposer", "external/libfuse", "lineage-sdk",
         "packages/apps/LineageParts", "vendor/lineage")
SKIP_DIRS = {".git", ".repo", "out", "obj", ".gradle", "__pycache__",
             ".cache", ".venv", "node_modules", "target", "build"}
SENSITIVE_NAMES = {"id_rsa", "id_ed25519", ".env", "credentials.json"}


def git(root, *args):
    return subprocess.check_output(
        ["git", "-C", str(root), *args], stderr=subprocess.DEVNULL
    ).decode("utf-8", "replace").strip()


def hashed(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        while buf := f.read(1048576):
            h.update(buf)
    return h.hexdigest()


def scan(root, cutoff):
    stack = [root]
    while stack:
        folder = stack.pop()
        try:
            with os.scandir(folder) as it:
                for e in it:
                    if e.is_symlink():
                        continue
                    if e.is_dir(follow_symlinks=False):
                        if e.name not in SKIP_DIRS:
                            stack.append(Path(e.path))
                        continue
                    if not e.is_file(follow_symlinks=False):
                        continue
                    st = e.stat(follow_symlinks=False)
                    if st.st_mtime >= cutoff:
                        yield Path(e.path), st
        except (PermissionError, FileNotFoundError):
            continue


def main(aosp, samsung, cutoff, output):
    since = dt.datetime.strptime(cutoff, "%Y-%m-%d").replace(
        tzinfo=dt.timezone.utc).timestamp()
    result = []
    summaries = []
    for repo in REPOS:
        root = aosp / repo
        tracked = set(filter(None, git(root, "ls-files", "-z").split("\x00")))
        wip = git(root, "for-each-ref", "--format=%(refname:short)",
                  "refs/heads/sm-t311-g257-WIP-needs-live-validation-*").splitlines()
        touched = {}
        for branch in wip:
            # These commits preserve previously uncommitted changes, including
            # those hidden by checkout and artificial October mtime.
            names = git(root, "diff-tree", "--no-commit-id", "--name-only",
                        "-r", branch).splitlines()
            for name in names:
                touched.setdefault(name, []).append(branch)
        count = Counter()
        for path, st in scan(root, since):
            rel = path.relative_to(root).as_posix()
            status = ("PRESERVED_WIP_COMMIT" if rel in touched else
                      "UNTRACKED" if rel not in tracked else
                      "GIT_TRACKED_CHECK_BASELINE")
            count[status] += 1
            entry = {
                "path": f"{repo}/{rel}", "mtime_utc": dt.datetime.fromtimestamp(
                    st.st_mtime, tz=dt.timezone.utc).isoformat(),
                "size": st.st_size, "git": status,
                "wip_refs": touched.get(rel, []),
                "sha256": (hashed(path) if status != "GIT_TRACKED_CHECK_BASELINE" and
                           st.st_size <= 16 * 1024 * 1024 and
                           path.name not in SENSITIVE_NAMES else None),
            }
            result.append(entry)
        # Git WIP might be only in the branch, not current worktree or with
        # earlier timestamp. Record it anyway so nothing is omitted.
        already = {x["path"] for x in result if x["path"].startswith(repo + "/")}
        for rel, branches in touched.items():
            path = f"{repo}/{rel}"
            if path not in already:
                count["WIP_NOT_PRESENT_IN_CURRENT_WORKTREE"] += 1
                result.append({"path": path, "mtime_utc": None, "size": None,
                               "git": "WIP_NOT_PRESENT_IN_CURRENT_WORKTREE",
                               "wip_refs": branches, "sha256": None})
        summaries.append({"repo": repo, "branch": git(root, "branch", "--show-current"),
                          "head": git(root, "rev-parse", "HEAD"),
                          "wip_refs": wip, "mtime_candidates": dict(count)})
    for name, root in [("kernel", samsung / "kernel")]:
        tracked = set(filter(None, git(root, "ls-files", "-z").split("\x00")))
        count = Counter()
        for path, st in scan(root, since):
            rel = path.relative_to(root).as_posix()
            status = "UNTRACKED" if rel not in tracked else "GIT_TRACKED_CHECK_BASELINE"
            count[status] += 1
            result.append({"path": f"{name}/{rel}",
                "mtime_utc": dt.datetime.fromtimestamp(st.st_mtime, tz=dt.timezone.utc).isoformat(),
                "size": st.st_size, "git": status, "wip_refs": [],
                "sha256": hashed(path) if status != "GIT_TRACKED_CHECK_BASELINE" and
                      st.st_size <= 16 * 1024 * 1024 and
                      path.name not in SENSITIVE_NAMES else None})
        summaries.append({"repo": name, "branch": git(root, "branch", "--show-current"),
                          "head": git(root, "rev-parse", "HEAD"),
                          "wip_refs": [], "mtime_candidates": dict(count)})
    inventory = {
        "cutoff_utc": cutoff, "generated_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "scope": "15 AOSP repos + canonical kernel; excludes build output, history backups and .git internals",
        "warning": "mtime is NOT proof of authorship. git checkout/clone/copy modifies timestamps. Git/WIP and content diffs take precedence.",
        "tracked_source_rule": "do NOT auto-commit every file newer than June; compare upstream base and WIP, then user handoffs and live SHA",
        "repositories": summaries,
        "counts": dict(Counter(x["git"] for x in result)),
        "files": result,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(inventory, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"cutoff": cutoff, "counts": inventory["counts"],
                      "total": len(result), "report": str(output),
                      "per_repo": {x["repo"]: x["mtime_candidates"] for x in summaries}},
                     ensure_ascii=False, indent=2))


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--aosp", type=Path, required=True)
    p.add_argument("--samsung", type=Path, required=True)
    p.add_argument("--since", default="2026-06-01")
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    main(a.aosp, a.samsung, a.since, a.output)
