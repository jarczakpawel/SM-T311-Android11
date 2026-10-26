#!/usr/bin/env bash
# G1182: Resume ONLY the release packaging and attestations after G1148 systemimage
# built successfully but G1148's outer finalizer exited 2 on a shell parse error.
# No full SYSTEM rebuild, no output pruning, no device access/reboot/audio/backup.
set -euo pipefail
AOSP=/home/debian/android/lineage-18.1-sm-t311
PRODUCT="$AOSP/out/target/product/lt013g"
QA=/home/debian/Projekty/samsung/linux-7.3-port/tools
LOCK=/home/debian/Projekty/samsung/artifacts/G1163-android11-rom-finalizer.lock
[ "${1:-}" = "--prior-g1148-terminal-failure-verified" ] || {
  echo "G1182_REFUSE: require independent prior failed G1148 terminal observation" >&2; exit 11;
}
[ ! -L "$LOCK" ] || { echo "G1182_REFUSE: symlink lock" >&2; exit 12; }
exec 9>"$LOCK"
flock -n 9 || { echo "G1182_REFUSE: finalizer still running" >&2; exit 13; }
[ ! -L "$PRODUCT" ] && [ -d "$PRODUCT" ] && [ -d "$PRODUCT/system" ] || {
  echo "G1182_REFUSE: product directory missing/symlink" >&2; exit 14;
}
IMAGE="$PRODUCT/system.img"
[ -f "$IMAGE" ] && [ ! -L "$IMAGE" ] || {
  echo "G1182_REFUSE: G1148's fully built SYSTEM image missing" >&2; exit 15;
}
for filename in \
  "$PRODUCT/obj/PACKAGING/G1152-qualified-ROM-source-manifest.json" \
  "$PRODUCT/obj/PACKAGING/G1152-qualified-target-files-qa.json"; do
  [ ! -e "$filename" ] && [ ! -L "$filename" ] || {
    echo "G1182_REFUSE: existing release receipt $filename" >&2; exit 16;
  }
done
cd "$AOSP"
# Main native build terminal success receipt G1159 was already independently
# verified by the original G1148 before its build. Its status remains unchanged.
python3 "$QA/g1159-verify-native-job-terminal-receipt.py"
python3 "$QA/g1146-verify-mfc-policy-source-pruning.py"
python3 "$QA/g1137-verify-g1130-kernel-android-publication-gate.py" --release-gate
python3 "$QA/g1139-verify-android-system-ext4-image.py"
for prop in "$PRODUCT/system/build.prop" "$PRODUCT/system/vendor/build.prop"; do
  [ -f "$prop" ] && [ ! -L "$prop" ] || { echo "G1182_REFUSE: no fingerprint" >&2; exit 17; }
  grep -Eq '^ro\.(system|vendor)\.build\.fingerprint=.*SMT311G1148' "$prop" || {
    echo "G1182_REFUSE: stale source build fingerprint" >&2; exit 18;
  }
done
echo G1182_ALREADY_BUILT_IMAGE_ACCEPTED_NO_SYSTEM_REBUILD
export BUILD_NUMBER=SMT311G1148
export MKE2FS_CONFIG="$AOSP/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
export PATH="/home/debian/Projekty/samsung/tools/smt311/aosp-python3-shim:$PATH"
[ -f "$MKE2FS_CONFIG" ] && [ ! -L "$MKE2FS_CONFIG" ] || exit 19
set +u
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
# Packaging only; never delete or rebuild the accepted real SYSTEM.
TARGETFILES_BUILD_START=$(date +%s)
m -j4 target-files-package
python3 "$QA/g1137-verify-g1130-kernel-android-publication-gate.py" --release-gate
python3 "$QA/g1139-verify-android-system-ext4-image.py"
mapfile -t ZIP_CANDIDATES < <(find "$PRODUCT/obj/PACKAGING/target_files_intermediates" \
  -maxdepth 1 -type f -name 'lineage_lt013g-target_files-*.zip' \
  -newermt "@$((TARGETFILES_BUILD_START-1))" -print)
[ "${#ZIP_CANDIDATES[@]}" -eq 1 ] || {
  echo "G1182_REFUSE: expected exactly one freshly created ZIP, got ${#ZIP_CANDIDATES[@]}" >&2; exit 20;
}
ZIP="${ZIP_CANDIDATES[0]}"
QA_RECEIPT="$PRODUCT/obj/PACKAGING/G1152-qualified-target-files-qa.json"
RELEASE_MANIFEST="$PRODUCT/obj/PACKAGING/G1152-qualified-ROM-source-manifest.json"
python3 "$QA/g1143-verify-target-files-zip-actual-artifacts.py" "$ZIP" --all-crc --receipt "$QA_RECEIPT"
python3 "$AOSP/device/samsung/lt013g/tools/g1152-write-qualified-release-manifest.py" \
  --target-files "$ZIP" --qa-receipt "$QA_RECEIPT" --output "$RELEASE_MANIFEST"
sha256sum "$ZIP"
echo "G1182_SOURCE_CANONICAL_SYSTEM_AND_TARGETFILES_OFFLINE_RELEASE_PASS (NOT flashed)"
