#!/usr/bin/env bash
set -euo pipefail
[ "${1:-}" = "--from-qualified-G1271" ] || exit 21
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
 9e584b021b69e59474a7915497f2825031259e89d493c534435c445d191e2212
 e5df5d81a3c61802dd890c0f154d5b1ba20ff0d89dde36255fd4959dc91d9569
 f5edabb591c8743537ee72065ee8a394b8faab4082fa6f88df401e0e1dcd4afe
)
for i in 0 1 2; do
 [ -f "${FILES[$i]}" ] && [ ! -L "${FILES[$i]}" ] || exit 24
 [ "$(sha256sum "${FILES[$i]}" | cut -d ' ' -f1)" = "${SHAS[$i]}" ] || exit 25
 grep -Fq SMT311G1271 "${FILES[$i]}" || exit 26
done
[ "$(git -C "$AOSP/external/v4l2_codec2" rev-parse HEAD)" = 8acf516c7ddc1321be064ea19069b97c0f653ccd ] || exit 27
[ -z "$(git -C "$AOSP/external/v4l2_codec2" status --porcelain --untracked-files=no)" ] || exit 28
[ "$(sha256sum "$OUT/system/vendor/lib/libv4l2_codec2_components.so" | cut -d ' ' -f1)" = 4e7b8d57f8031353f7ac6ff53db3a09f16c81f044beafea7d985f0ccf07b4a48 ] || exit 29
python3 "$AOSP/external/v4l2_codec2/tests/test_g1270_stop_failure_schedule_error.py"
python3 "$PROJECT/linux-7.3-port/tools/g1281-verify-codec2-input-error-system.py" --staging
for p in "${FILES[@]}"; do rm -- "$p"; done
cd "$AOSP"
export BUILD_NUMBER=SMT311G1281
export MKE2FS_CONFIG="$AOSP/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
export PATH="$PROJECT/tools/smt311/aosp-python3-shim:$PATH"
set +u
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
m -j4 systemimage
WANT=samsung/lineage_lt013g/lt013g:11/RQ3A.211001.001/SMT311G1281:userdebug/test-keys
grep -Fxq "ro.system.build.fingerprint=$WANT" "$OUT/system/build.prop"
grep -Fxq "ro.vendor.build.fingerprint=$WANT" "$OUT/system/vendor/build.prop"
grep -Fxq ro.build.version.incremental=SMT311G1281 "$OUT/system/build.prop"
python3 "$PROJECT/linux-7.3-port/tools/g1281-verify-codec2-input-error-system.py" --audit-image
echo G1281_NEW_CODEC2_STOP_SCHEDULE_FULL_EXT4_ARM32_SYSTEM_HOST_PASS_NOT_FLASHED
