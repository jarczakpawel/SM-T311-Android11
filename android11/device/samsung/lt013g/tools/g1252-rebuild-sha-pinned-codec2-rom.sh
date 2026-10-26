#!/usr/bin/env bash
# G1252: regenerate ONLY three cached Android11 GENERATED fingerprints together.
# Host-only; preserves Android sources and historical G1148 OTA + QA reports.
# Does not back up, flash, wipe, reboot, or interact with a tablet.
set -euo pipefail
[ "${1:-}" = "--old-generated-g1242-props-sha-verified" ] || {
  echo "G1252_REFUSE: explicit verified old cached build.props required" >&2;exit 21;
}
AOSP=/home/debian/android/lineage-18.1-sm-t311
OUT="$AOSP/out/target/product/lt013g"
PROJECT=/home/debian/Projekty/samsung
LOCK="$PROJECT/artifacts/G1163-android11-rom-finalizer.lock"
[ ! -L "$LOCK" ] || exit 22
exec 9>"$LOCK"
flock -n 9 || { echo "G1252_REFUSE: concurrent source build/finalizer";exit 23; }
# Exact witnessed cached G1148 outputs; these are NOT source/vendor documents.
GENERATED=(
 "$OUT/obj/ETC/system_build_prop_intermediates/build.prop"
 "$OUT/system/build.prop"
 "$OUT/system/vendor/build.prop"
)
SHA=(
 48efe3ef853ea739d09d28ab34e9aefbaefbdf8223e97036fbe8f11bdcb3b6ac
 f126f45b8949c2dc20f4d13c292fc29657714f2e60ade91e76364bc41cd34d36
 dc5a5b31bc7980675e78485eb128e517dd7c4673844ef6f3ddfd157656c4d86a
)
for i in 0 1 2; do
  p="${GENERATED[$i]}"
  [ -f "$p" ] && [ ! -L "$p" ] || { echo "G1252_REFUSE: missing/symlinked $p";exit 24; }
  actual="$(sha256sum "$p" | awk '{print $1}')"
  [ "$actual" = "${SHA[$i]}" ] || {
    echo "G1252_REFUSE: cache content changed $p actual=$actual"; exit 25;
  }
  grep -Fq 'SMT311G1242' "$p" || { echo "G1252_REFUSE: not stale G1224: $p";exit 26; }
done
# The G1200 inventory validates the two real ARM32 experimental candidates,
# original G1148 signed OTA/manifest and clean committed source descendants.
python3 "$AOSP/device/samsung/lt01-common/sepolicy/tests/test_g1241_exact_watchdog_binder_callback.py"
python3 "$AOSP/external/v4l2_codec2/tests/test_g1251_poll_and_input_error_propagation.py"
python3 "$AOSP/external/v4l2_codec2/tests/test_g1211_capture_completion_error_propagation.py"
python3 "$PROJECT/linux-7.3-port/tools/g1252-verify-g1251-codec2-system-ext4.py" --self-test
# Avoid accidental deletion of arbitrary paths; every file is SHA-pinned above.
for p in "${GENERATED[@]}";do rm -- "$p";done
cd "$AOSP"
export BUILD_NUMBER=SMT311G1252
export MKE2FS_CONFIG="$AOSP/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
export PATH="$PROJECT/tools/smt311/aosp-python3-shim:$PATH"
set +u
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
m -j4 systemimage
# Check consistency in the new *generated* system/vendor fingerprints.
WANT='samsung/lineage_lt013g/lt013g:11/RQ3A.211001.001/SMT311G1252:userdebug/test-keys'
grep -Fxq "ro.system.build.fingerprint=$WANT" "$OUT/system/build.prop" ||
 { echo G1252_REFUSE_SYSTEM_FINGERPRINT;exit 27; }
grep -Fxq "ro.vendor.build.fingerprint=$WANT" "$OUT/system/vendor/build.prop" ||
 { echo G1252_REFUSE_VENDOR_FINGERPRINT;exit 28; }
grep -Fxq 'ro.build.version.incremental=SMT311G1252' "$OUT/system/build.prop" ||
 { echo G1252_REFUSE_INCREMENTAL_VERSION;exit 29; }
python3 "$PROJECT/linux-7.3-port/tools/g1252-verify-g1251-codec2-system-ext4.py" --staging
echo G1252_POLICY_AND_FINGERPRINTS_REBUILT_COHERENT_SYSTEM_VENDOR_HOST_ONLY
