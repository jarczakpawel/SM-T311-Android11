#!/usr/bin/env bash
# G1148: gated, one-shot Android11 SM-T311 production SYSTEM/target-files finalize.
# Never flash/reboot/back up tablet, enable sound, touch modem PIN or push GitHub.
# Run ONLY after the currently active native build job6974d254eda6 has STOPPED
# and its real status/logs were independently inspected (the first job
# 86a241898a10 FAILED on host APEX mke2fs orphan_file).
# No engineering camera/GNSS APK permitted in production image.
set -euo pipefail
ROOT=/home/debian/android/lineage-18.1-sm-t311
QA=/home/debian/Projekty/samsung/linux-7.3-port/tools
OUT="$ROOT/out/target/product/lt013g"
CLEAN="$ROOT/device/samsung/lt013g/tools/g1147-prune-generated-engineering-apks.py"
cd "$ROOT"

if [[ "${1:-}" != "--native-build-confirmed-stopped" ]]; then
  echo "G1148_REFUSE: use --native-build-confirmed-stopped AFTER independently checking the long build job status" >&2
  exit 20
fi
# G1163: independent of ps, a second G1148 run must NEVER mutate the
# same product while an earlier G1148 finalizer is alive. flock holds the
# lock for the entire outer shell and its sequential AOSP sub-builds.
# Receipt G1159 proves the MAIN native job stopped; flock protects against
# another FINALIZER using the same successful receipt concurrently.
LOCK=/home/debian/Projekty/samsung/artifacts/G1163-android11-rom-finalizer.lock
[[ -L "$LOCK" ]] && { echo "G1163_REFUSE: finalizer lock symlink" >&2; exit 29; }
exec 9>"$LOCK"
if ! flock -n 9; then
  echo "G1163_REFUSE: another ROM finalizer holds the same artifact lock" >&2
  exit 30
fi
echo "PASS G1163_EXCLUSIVE_ROM_FINALIZER_LOCK_HELD"
# G1159: workspace_run is process-isolated from workspace_start_build.
# 'ps' below can see ZERO ninja processes even while job6974d254eda6
# is actively compiling! Require a separate exact terminal SUCCEEDED
# receipt written only AFTER external workspace_build_status confirms it.
# The file does NOT exist during an active native build.
python3 "$QA/g1159-verify-native-job-terminal-receipt.py"
# No two concurrent Soong/Ninja builds mutating the same output tree.
if ps -eo comm= | grep -E '^(ninja|soong_ui|soong_ui.bash)$' >/dev/null; then
  echo "G1148_REFUSE: another Android build appears active" >&2
  exit 21
fi
if [[ -L "$OUT" || -L "$OUT/system" || ! -d "$OUT/system" ]]; then
  echo "G1148_REFUSE: wrong or symlinked target product tree" >&2
  exit 22
fi
# Must match physically accepted G1130 kernel and native canonical G1146
# sources before any generated output is removed.
python3 "$QA/g1137-verify-g1130-kernel-android-publication-gate.py" --staging-gate
# G1156: at this point the in-flight native product can legitimately
# contain the OLD G1153 binary; require immutable source now, compiled
# exact hashes only AFTER the systemimage build has updated the output.
python3 "$QA/g1146-verify-mfc-policy-source-pruning.py" --source-only
python3 "$CLEAN"  # default DRY RUN; rejects unknown files/symlinks

# G1161: all preconditions for future generated-output deletion MUST be
# checked before invoking --apply or rm. Prior order could remove the
# diagnostic APK and build.prop and then discover a missing system.img.
# This is a fail-closed preflight, not a backup or a rollback.
declare -a PROP_OUTPUTS=(
  "$OUT/obj/ETC/system_build_prop_intermediates/build.prop"
  "$OUT/system/build.prop"
  "$OUT/system/vendor/build.prop"
)
for file in "${PROP_OUTPUTS[@]}"; do
  [[ -L "$file" ]] && { echo "G1148_REFUSE: property output symlink $file" >&2; exit 23; }
done
IMAGE="$OUT/system.img"
if [[ -L "$IMAGE" || ! -f "$IMAGE" ]]; then
  echo "G1148_REFUSE: system image absent or symlinked; inspect before rebuild" >&2
  exit 24
fi
# G1150: inherit explicit ext4 defaults from source, NOT Debian testing's
# /etc/mke2fs.conf orphan_file feature unsupported by bundled 1.45.4.
export MKE2FS_CONFIG="$ROOT/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
[[ -f "$MKE2FS_CONFIG" && ! -L "$MKE2FS_CONFIG" ]] ||
  { echo "G1150_REFUSE: missing source-committed e2fsprogs config" >&2; exit 28; }
echo "PASS G1161_ALL_SOURCE_ARTIFACT_PRECONDITIONS_BEFORE_OUTPUT_MUTATION"

# Only after all G1161 checks PASS may generated QA app output be pruned.
python3 "$CLEAN" --apply --native-build-finished

# G591: use a single BUILD_NUMBER to avoid inconsistent system/vendor
# properties. Remove only three product-generated build.prop artifacts.
for file in "${PROP_OUTPUTS[@]}"; do
  if [[ -f "$file" ]]; then rm -- "$file"; fi
done

# Remove only previously checked generated SYSTEM image so native m
# systemimage can regenerate a coherent current source-canonical image.
rm -- "$IMAGE"
export BUILD_NUMBER=SMT311G1148
export PATH="/home/debian/Projekty/samsung/tools/smt311/aosp-python3-shim:$PATH"
# AOSP envsetup.sh references unset ZSH_VERSION and others; disable
# nounset ONLY at the Android build-script boundary, preserve errexit.
set +u
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
# Two sequential targets, not parallel siblings: a packaging task must
# never race the newly regenerated SOURCE-CANONICAL system.img.
m -j4 systemimage
# G1156: after compiling, strict source-canonical binary policy and
# vendor API30-versioned FMQ CIL SHA MUST agree with tracked Android
# sources; no bypass, permissive mode or old host acceptance allowed.
python3 "$QA/g1146-verify-mfc-policy-source-pruning.py"
# G1151: catch a stale binary SELinux, RIL/Codec2/GPS/Mesa binary or
# leftover QA app inside the real sparse ext4 image BEFORE spending time
# producing a full target-files archive. Read-only e2fsck -fn and debugfs.
python3 "$QA/g1139-verify-android-system-ext4-image.py"
# The ZIP name can be generated from either build number or the cached
# Ninja product configuration. Require a freshly created target-files ZIP.
TARGETFILES_BUILD_START=$(date +%s)
m -j4 target-files-package

# Read-only qualification of actual finished output: no more edits below.
python3 "$QA/g1137-verify-g1130-kernel-android-publication-gate.py" --release-gate
python3 "$QA/g1139-verify-android-system-ext4-image.py"

mapfile -t ZIP_CANDIDATES < <(find "$OUT/obj/PACKAGING/target_files_intermediates" -maxdepth 1 \
    -type f -name 'lineage_lt013g-target_files-*.zip' \
    -newermt "@$((TARGETFILES_BUILD_START-1))" -print)
if [[ "${#ZIP_CANDIDATES[@]}" -ne 1 ]]; then
  echo "G1150_REFUSE: expected exactly one NEW target-files ZIP from this packaging pass, found ${#ZIP_CANDIDATES[@]}" >&2
  exit 25
fi
ZIP="${ZIP_CANDIDATES[0]}"
# Build number must be embedded in BOTH fingerprints, not just matching.
for prop in "$OUT/system/build.prop" "$OUT/system/vendor/build.prop"; do
  [[ -f "$prop" ]] || { echo "G1150_REFUSE: missing $prop" >&2; exit 26; }
  grep -Eq '^ro\.(system|vendor)\.build\.fingerprint=.*SMT311G1148' "$prop" ||
    { echo "G1150_REFUSE: stale build fingerprint $prop" >&2; exit 27; }
done
# G1152: save the actual strict-ZIP verifier receipt + reproducible Git
# revision manifest ONLY on success (no physical flash/backup/restore).
QA_RECEIPT="$OUT/obj/PACKAGING/G1152-qualified-target-files-qa.json"
RELEASE_MANIFEST="$OUT/obj/PACKAGING/G1152-qualified-ROM-source-manifest.json"
python3 "$QA/g1143-verify-target-files-zip-actual-artifacts.py" "$ZIP" \
  --all-crc --receipt "$QA_RECEIPT"
python3 "$ROOT/device/samsung/lt013g/tools/g1152-write-qualified-release-manifest.py" \
  --target-files "$ZIP" --qa-receipt "$QA_RECEIPT" \
  --output "$RELEASE_MANIFEST"
sha256sum "$ZIP"
echo "G1148_NATIVE_PRODUCTION_SYSTEM_AND_TARGET_FILES_OFFLINE_RELEASE_PASS (NOT flashed; NO OTA runtime claim)"
