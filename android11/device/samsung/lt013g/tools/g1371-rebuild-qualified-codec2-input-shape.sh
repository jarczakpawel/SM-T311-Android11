#!/usr/bin/env bash
set -euo pipefail
[ "${1:-}" = "--from-qualified-G1351" ] || exit 21
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
 b55a1cc1479365c1c503d41b5f79187cbb7580c8d63a48b12f04e71ac1c25d13
 757a8aa3b78bd4cfa06824e068af5ecc6c0428ef146798fd6d40c719c618b557
 a5559bbff6269c3b6667361c78e66782b104e94046b1331273712828e20c619b
)
for i in 0 1 2; do
 [ -f "${FILES[$i]}" ] && [ ! -L "${FILES[$i]}" ] || exit 24
 [ "$(sha256sum "${FILES[$i]}" | cut -d ' ' -f1)" = "${SHAS[$i]}" ] || exit 25
 grep -Fq SMT311G1351 "${FILES[$i]}" || exit 26
done
[ "$(git -C "$AOSP/external/v4l2_codec2" rev-parse HEAD)" = 67478b1cbc283e68a589b4f3180b74b6ce79b481 ] || exit 27
[ -z "$(git -C "$AOSP/external/v4l2_codec2" status --porcelain --untracked-files=no)" ] || exit 28
[ "$(sha256sum "$OUT/system/vendor/lib/libv4l2_codec2_components.so" | cut -d ' ' -f1)" = e7ac0d01f8591327a7ae1ec80ba549d33474f02506e36c84f31fd14bdcc278c7 ] || exit 29
python3 "$AOSP/external/v4l2_codec2/tests/test_g1370_encoder_accepted_input_shape.py"
python3 "$PROJECT/linux-7.3-port/tools/g1371-verify-codec2-input-shape-system.py" --staging
for p in "${FILES[@]}"; do rm -- "$p"; done
cd "$AOSP"
export BUILD_NUMBER=SMT311G1371
export MKE2FS_CONFIG="$AOSP/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
export PATH="$PROJECT/tools/smt311/aosp-python3-shim:$PATH"
set +u
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
m -j4 systemimage
WANT=samsung/lineage_lt013g/lt013g:11/RQ3A.211001.001/SMT311G1371:userdebug/test-keys
grep -Fxq "ro.system.build.fingerprint=$WANT" "$OUT/system/build.prop"
grep -Fxq "ro.vendor.build.fingerprint=$WANT" "$OUT/system/vendor/build.prop"
grep -Fxq ro.build.version.incremental=SMT311G1371 "$OUT/system/build.prop"
python3 "$PROJECT/linux-7.3-port/tools/g1371-verify-codec2-input-shape-system.py" --audit-image
echo G1371_ENCODER_INPUT_SHAPE_GUARD_FULL_EXT4_ARM32_SYSTEM_HOST_PASS_NOT_FLASHED
