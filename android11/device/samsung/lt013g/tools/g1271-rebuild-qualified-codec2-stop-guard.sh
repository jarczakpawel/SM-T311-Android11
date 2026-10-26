#!/usr/bin/env bash
set -euo pipefail
[ "${1:-}" = "--from-qualified-G1262" ] || exit 21
AOSP=/home/debian/android/lineage-18.1-sm-t311
PROJECT=/home/debian/Projekty/samsung
OUT="$AOSP/out/target/product/lt013g"
exec 9>"$PROJECT/artifacts/G1163-android11-rom-finalizer.lock"
flock -n 9 || exit 23
FILES=(
 "$OUT/obj/ETC/system_build_prop_intermediates/build.prop"
 "$OUT/system/build.prop"
 "$OUT/system/vendor/build.prop"
)
SHAS=(
 a450a7713b2c55873078d0727c74b32a851c1c8845021370f2781f31fc02e1a3
 ee55ecef52eb936dbdd5dcaf90cfd94ca9a7c23a9bd9b4d9384d9e39b9969604
 99f0f7e8384f93a9b82c0be201b4f01b5767fb83fb162cd5394f555ecea216b1
)
for i in 0 1 2; do
 [ -f "${FILES[$i]}" ] && [ ! -L "${FILES[$i]}" ] || exit 24
 [ "$(sha256sum "${FILES[$i]}" | cut -d ' ' -f1)" = "${SHAS[$i]}" ] || exit 25
 grep -Fq SMT311G1262 "${FILES[$i]}" || exit 26
done
[ "$(git -C "$AOSP/external/v4l2_codec2" rev-parse HEAD)" = 1c70a5449b3ebd6f7bc60a611e4710435916a334 ] || exit 27
[ -z "$(git -C "$AOSP/external/v4l2_codec2" status --porcelain --untracked-files=no)" ] || exit 28
[ "$(sha256sum "$OUT/system/vendor/lib/libv4l2_codec2_components.so" | cut -d ' ' -f1)" = f20f54cc244e2954ce333f22a03b94764be49ac457153aa6fece245a1d81cf3f ] || exit 29
python3 "$AOSP/external/v4l2_codec2/tests/test_g1270_stop_failure_schedule_error.py"
python3 "$PROJECT/linux-7.3-port/tools/g1271-verify-codec2-schedule-error-system.py" --staging
for p in "${FILES[@]}"; do rm -- "$p"; done
cd "$AOSP"
export BUILD_NUMBER=SMT311G1271
export MKE2FS_CONFIG="$AOSP/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
export PATH="$PROJECT/tools/smt311/aosp-python3-shim:$PATH"
set +u
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
m -j4 systemimage
WANT=samsung/lineage_lt013g/lt013g:11/RQ3A.211001.001/SMT311G1271:userdebug/test-keys
grep -Fxq "ro.system.build.fingerprint=$WANT" "$OUT/system/build.prop"
grep -Fxq "ro.vendor.build.fingerprint=$WANT" "$OUT/system/vendor/build.prop"
grep -Fxq ro.build.version.incremental=SMT311G1271 "$OUT/system/build.prop"
python3 "$PROJECT/linux-7.3-port/tools/g1271-verify-codec2-schedule-error-system.py" --audit-image
echo G1271_NEW_CODEC2_STOP_SCHEDULE_FULL_EXT4_ARM32_SYSTEM_HOST_PASS_NOT_FLASHED
