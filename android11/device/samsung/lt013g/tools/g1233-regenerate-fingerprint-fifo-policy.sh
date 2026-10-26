#!/usr/bin/env bash
# G1233: regenerate ONLY three cached Android11 GENERATED fingerprints together.
# Host-only; preserves Android sources and historical G1148 OTA + QA reports.
# Does not back up, flash, wipe, reboot, or interact with a tablet.
set -euo pipefail
[ "${1:-}" = "--old-generated-g1224-props-sha-verified" ] || {
  echo "G1233_REFUSE: explicit verified old cached build.props required" >&2;exit 21;
}
AOSP=/home/debian/android/lineage-18.1-sm-t311
OUT="$AOSP/out/target/product/lt013g"
PROJECT=/home/debian/Projekty/samsung
LOCK="$PROJECT/artifacts/G1163-android11-rom-finalizer.lock"
[ ! -L "$LOCK" ] || exit 22
exec 9>"$LOCK"
flock -n 9 || { echo "G1233_REFUSE: concurrent source build/finalizer";exit 23; }
# Exact witnessed cached G1148 outputs; these are NOT source/vendor documents.
GENERATED=(
 "$OUT/obj/ETC/system_build_prop_intermediates/build.prop"
 "$OUT/system/build.prop"
 "$OUT/system/vendor/build.prop"
)
SHA=(
 7e776b6f23e8add938120547aa8855e47e8e9c4ef295f499c44afe0078cc31de
 db447e218d32dca9a6dc75cb48299ad265fe4b42f1d3f79ed37ad12c8f1277b0
 342a76f663115ffd4ee608f7dc68c4b542d625117650a86432f0452d912b60bb
)
for i in 0 1 2; do
  p="${GENERATED[$i]}"
  [ -f "$p" ] && [ ! -L "$p" ] || { echo "G1233_REFUSE: missing/symlinked $p";exit 24; }
  actual="$(sha256sum "$p" | awk '{print $1}')"
  [ "$actual" = "${SHA[$i]}" ] || {
    echo "G1233_REFUSE: cache content changed $p actual=$actual"; exit 25;
  }
  grep -Fq 'SMT311G1224' "$p" || { echo "G1233_REFUSE: not stale G1224: $p";exit 26; }
done
# The G1200 inventory validates the two real ARM32 experimental candidates,
# original G1148 signed OTA/manifest and clean committed source descendants.
python3 "$AOSP/device/samsung/lt01-common/sepolicy/tests/test_g1232_exact_watchdog_fifo_write.py"
python3 "$PROJECT/linux-7.3-port/tools/g1233-verify-next-fifo-policy-ext4.py" --self-test
# Avoid accidental deletion of arbitrary paths; every file is SHA-pinned above.
for p in "${GENERATED[@]}";do rm -- "$p";done
cd "$AOSP"
export BUILD_NUMBER=SMT311G1233
export MKE2FS_CONFIG="$AOSP/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
export PATH="$PROJECT/tools/smt311/aosp-python3-shim:$PATH"
set +u
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
m -j4 systemimage
# Check consistency in the new *generated* system/vendor fingerprints.
WANT='samsung/lineage_lt013g/lt013g:11/RQ3A.211001.001/SMT311G1233:userdebug/test-keys'
grep -Fxq "ro.system.build.fingerprint=$WANT" "$OUT/system/build.prop" ||
 { echo G1233_REFUSE_SYSTEM_FINGERPRINT;exit 27; }
grep -Fxq "ro.vendor.build.fingerprint=$WANT" "$OUT/system/vendor/build.prop" ||
 { echo G1233_REFUSE_VENDOR_FINGERPRINT;exit 28; }
grep -Fxq 'ro.build.version.incremental=SMT311G1233' "$OUT/system/build.prop" ||
 { echo G1233_REFUSE_INCREMENTAL_VERSION;exit 29; }
python3 "$PROJECT/linux-7.3-port/tools/g1233-verify-next-fifo-policy-ext4.py" --staging
echo G1233_POLICY_AND_FINGERPRINTS_REBUILT_COHERENT_SYSTEM_VENDOR_HOST_ONLY
