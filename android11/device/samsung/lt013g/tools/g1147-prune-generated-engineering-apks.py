#!/usr/bin/env python3
"""SM-T311 G1147: remove only generated engineering diagnostic app output.

SOURCE files / APK test source / device user data are NEVER touched.
Must only apply after the native target-files build job has stopped:
the --native-build-finished acknowledgement is mandatory for deletion.
Default dry run is read-only. Never flash, backup, use PIN or send audio.
"""
import argparse
from pathlib import Path
import shutil

OUT = Path("/home/debian/android/lineage-18.1-sm-t311/out/target/product/lt013g")
BASE = OUT / "system/product/app"
PROBES = ("T311Camera2Smoke", "T311GnssStatus")

def refuse(message):
    raise SystemExit("G1147_PRODUCTION_DIAGNOSTIC_CLEANUP_REFUSE: " + message)

def inspect(name):
    dest = BASE / name
    # Do not follow symlinks in the generated tree or touch anything outside
    # this device's EXACT product-app output directory.
    if dest.is_symlink():
        refuse("probe directory symlink " + name)
    if not dest.exists():
        print("PASS G1147_DIAGNOSTIC_ABSENT " + name)
        return None
    if not dest.is_dir() or dest.resolve() != BASE.resolve() / name:
        refuse("unexpected probe path " + str(dest))
    files = []
    for child in dest.rglob("*"):
        if child.is_symlink():
            refuse("symlink below output directory " + str(child))
        if child.is_file():
            allowed = {
                dest / (name + ".apk"),
                dest / "oat/arm" / (name + ".odex"),
                dest / "oat/arm" / (name + ".vdex"),
            }
            if child not in allowed:
                refuse("unrecognized generated diagnostic file " + str(child))
            files.append(child)
        elif not child.is_dir():
            refuse("non-file non-directory in generated diagnostic " + str(child))
    if not files:
        refuse("unexpected empty diagnostic output directory " + name)
    print("G1147_PRUNABLE_GENERATED_ENGINEERING_OUTPUT " +
          name + " files=" + str(len(files)))
    return dest

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--apply", action="store_true")
    p.add_argument("--native-build-finished", action="store_true",
                   help="Explicit acknowledgement the long native build job ended")
    args = p.parse_args()
    if args.apply and not args.native_build_finished:
        refuse("cannot mutate output while native build may be running")
    if not BASE.is_dir() or BASE.is_symlink():
        refuse("wrong device PRODUCT staging layout")
    items = [inspect(name) for name in PROBES]
    if args.apply:
        for folder in items:
            if folder is not None:
                shutil.rmtree(folder)
                print("G1147_REMOVED_ONLY_GENERATED_DIAGNOSTIC_DIR " + folder.name)
        assert all(not (BASE / name).exists() for name in PROBES)
        print("G1147_PRODUCTION_STAGING_DIAGNOSTIC_REMOVAL_PASS")
    else:
        print("G1147_DRY_RUN_NO_FILES_CHANGED; repackage ONLY after native build finished")

if __name__ == "__main__":
    main()
