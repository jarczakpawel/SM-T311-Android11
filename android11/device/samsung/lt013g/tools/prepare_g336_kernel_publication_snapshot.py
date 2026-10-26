#!/usr/bin/env python3
"""Create an *isolated local* source-only kernel Git history with no firmware.

Uses borrowed read-only object storage from the original kernel via Git
alternates; no checkout, copying firmware, rewriting original refs or push.
The resulting bare repository is NOT standalone and is NOT legally cleared.
"""
from __future__ import annotations
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

SOURCE = Path("/home/debian/Projekty/samsung/kernel")
ROOT = Path("/home/debian/Projekty/samsung/artifacts/SM-T311-G336-CLEAN-KERNEL-SOURCE-20261016")
# Restrict exclusions to exactly the seven audited G335 stock firmware assets.
EXCLUDED = (
    "firmware/brcm/BCM4334B0.hcd",
    "firmware/brcm/BCM4334B0.samsung,t311.hcd",
    "firmware/brcm/brcmfmac4334-sdio.samsung,t311.bin",
    "firmware/brcm/brcmfmac4334-sdio.samsung,t311.txt",
    "firmware/regulatory.db",
    "firmware/regulatory.db.p7s",
    "firmware/s5p-mfc.fw",
)
CHECK_SOURCE = (
    "COPYING",
    "Makefile",
    "arch/arm/configs/sm_t311_android11_defconfig",
    "drivers/misc/samsung-tab3-xmm6262-runtime.c",
    "arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dts",
    "arch/arm/boot/dts/samsung/exynos4212-tab3.dtsi",
)


def run(*args, env=None, input=None):
    p = subprocess.run(args, env=env, input=input, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, check=True)
    return p.stdout.decode().strip()


def git(directory, *args, env=None, input=None):
    if directory.name.endswith(".git") and directory.is_dir():
        # Important: -C must reference the temporary worktree ROOT, not
        # the bare .git directory, or pathspec matches are silently scoped
        # under kernel-source-only.git/ and --force-remove becomes a no-op.
        return run("git", "-C", str(ROOT), "--git-dir=" + str(directory),
                   *args, env=env, input=input)
    return run("git", "-C", str(directory), *args, env=env, input=input)


def tree_names(directory, commit):
    raw = subprocess.run(["git", "-C", str(directory), "ls-tree", "-rz", "--name-only", commit],
                         stdout=subprocess.PIPE, check=True).stdout
    return {item.decode("utf-8", "surrogateescape") for item in raw.split(b"\0") if item}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path,
                   default=ROOT / "kernel-source-only-git-metadata.json")
    a = p.parse_args()
    if not (SOURCE / ".git").exists():
        raise SystemExit("Kernel source .git missing")
    orig_commit = git(SOURCE, "rev-parse", "HEAD")
    names = tree_names(SOURCE, "HEAD")
    prohibited = set(EXCLUDED)
    assert prohibited <= names, "G335 proprietary paths no longer match kernel HEAD"
    assert len(names) > 86000, "Kernel checkout unexpected: refuse to create partial snapshot"
    assert all(f in names for f in CHECK_SOURCE)
    # A previous interrupted attempt may have initialized an EMPTY bare
    # repository. Resume only if it has no refs. Never rewrite a root commit.
    bare = ROOT / "kernel-source-only-validated.git"
    ROOT.mkdir(parents=True, exist_ok=True)
    if bare.exists():
        assert (bare / "HEAD").exists(), "Unexpected existing destination"
        refs = subprocess.run(
            ["git", "-C", str(bare), "show-ref"],
            capture_output=True)
        if refs.returncode != 1:
            raise SystemExit("Refusing to overwrite existing publication refs: " + str(bare))
    else:
        run("git", "init", "--bare", "--initial-branch=sm-t311-source-only", str(bare))
    source_objects = (SOURCE / ".git" / "objects").resolve()
    alternates = bare / "objects/info/alternates"
    alternates.parent.mkdir(parents=True, exist_ok=True)
    alternates.write_text(str(source_objects) + "\n")

    env = os.environ.copy()
    env["GIT_INDEX_FILE"] = str(ROOT / "staging.index")
    # Even a bare Git repo needs a safe temporary worktree pointer for
    # update-index --force-remove. No files will be checked out or changed.
    env["GIT_WORK_TREE"] = str(ROOT)
    try:
        # This copies only the Git tree *references* into an isolated index.
        git(bare, "read-tree", orig_commit, env=env)
        for item in EXCLUDED:
            git(bare, "update-index", "--force-remove", "--", item, env=env)
        tree = git(bare, "write-tree", env=env)
        message = ("Samsung SM-T311 Linux 6.12.112 source-only publication candidate\n\n"
                   "A clean initial commit from audited local kernel HEAD "
                   + orig_commit + ".\n"
                   "Excludes exactly seven original Broadcom/Exynos regulatory/firmware inputs.\n"
                   "Builds still need lawfully obtained local OEM firmware; distribution rights "
                   "and complete license notices remain unverified.\n")
        commit_env = env.copy()
        commit_env.update({
            "GIT_AUTHOR_NAME": "SM-T311 Source Release",
            "GIT_AUTHOR_EMAIL": "source-only@local.invalid",
            "GIT_COMMITTER_NAME": "SM-T311 Source Release",
            "GIT_COMMITTER_EMAIL": "source-only@local.invalid",
        })
        new_commit = git(bare, "commit-tree", tree, env=commit_env,
                         input=message.encode())
        git(bare, "update-ref", "refs/heads/sm-t311-source-only", new_commit)
        git(bare, "symbolic-ref", "HEAD", "refs/heads/sm-t311-source-only")
        resulting = tree_names(bare, "HEAD")
        assert resulting == names - prohibited, (
            "Unexpected changes beyond the seven firmware paths",
            len(resulting), len(names))
        assert not (resulting & prohibited)
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from verify_source_release_gate import prohibited_path
        rejected = [name for name in resulting if prohibited_path(name)]
        assert not rejected, "Unexpected firmware paths remain: " + repr(rejected[:8])
        assert len(git(bare, "rev-list", "--parents", "HEAD").split()) == 1, (
            "Snapshot accidentally inherits original kernel Git history")
        assert len(git(bare, "rev-list", "--count", "--all")) == 1
        unchanged = {}
        for item in CHECK_SOURCE:
            before = git(SOURCE, "rev-parse", f"{orig_commit}:{item}")
            after = git(bare, "rev-parse", f"{new_commit}:{item}")
            assert before == after
            unchanged[item] = before
        history = git(bare, "rev-list", "--objects", "--all")
        assert all(item not in history for item in EXCLUDED)
        # Confirm git references exactly one root commit; alternates still
        # hold the original objects, so the bare repo is NOT a standalone copy.
        result = {
            "status": "PASS_SOURCE_ONLY_GIT_HISTORY; LICENSE_CLEARANCE_PENDING",
            "original_kernel_head": orig_commit,
            "source_only_root_commit": new_commit,
            "source_only_tree": tree,
            "source_only_commit_count": 1,
            "source_tree_count_before": len(names),
            "source_tree_count_after": len(resulting),
            "removed_source_paths": list(EXCLUDED),
            "source_path_difference_exactly_seven": True,
            "sample_source_blob_ids_unmodified": unchanged,
            "private_firmware_in_new_reachable_history": False,
            "original_dev_kernel_repository_mutated": False,
            "repository_borrows_original_object_storage": True,
            "standalone_source_archive_or_bundle_created": False,
            "redistribution_or_licensing_clearance": False,
            "public_push_performed": False,
            "warning": "Bare local repo borrows original Git object database via alternates. "
                       "DO NOT publish the raw .git directory or use a --mirror backup; "
                       "only audited reachable root commit, with legal clearance, should "
                       "be exported as an independent repo.",
        }
        outfile = a.output.resolve()
        outfile.parent.mkdir(parents=True, exist_ok=True)
        outfile.write_text(json.dumps(result, ensure_ascii=False,
                                      sort_keys=True, indent=2) + "\n")
        print("G336 source-only root commit", new_commit)
        print("G336 kernel tracked source paths", len(names), "->", len(resulting))
        print("G336 removed EXACT seven known firmware/regulatory files")
        print("G336 one root commit/no reachable private firmware paths PASS")
        print("G336 kernel driver DTS defconfig and COPYING blob-ID parity PASS")
        print("G336 source-only local candidate, NO push, NOT standalone, NOT license-cleared")
        print("G336 manifest", outfile)
    finally:
        Path(env["GIT_INDEX_FILE"]).unlink(missing_ok=True)


if __name__ == "__main__":
    main()
