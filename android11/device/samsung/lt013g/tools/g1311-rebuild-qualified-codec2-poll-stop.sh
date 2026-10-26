#!/usr/bin/env bash
set -euo pipefail
[ "${1:-}" = "--from-qualified-G1301" ] || exit 21
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
 7fd44e4e8e02d547e0e4059e5a42da72a3e0789582f1c1ca1e951e543ef7786e
 849692b763e4c6a930e3ee9f655955a8d16eb267c56776a539bbdfc953f1b7fb
 5a88fc0918db4b034288d3cbf118c11fc36c2c9e62f29ecdcb087f7067012966
)
for i in 0 1 2; do
 [ -f "${FILES[$i]}" ] && [ ! -L "${FILES[$i]}" ] || exit 24
 [ "$(sha256sum "${FILES[$i]}" | cut -d ' ' -f1)" = "${SHAS[$i]}" ] || exit 25
 grep -Fq SMT311G1301 "${FILES[$i]}" || exit 26
done
[ "$(git -C "$AOSP/external/v4l2_codec2" rev-parse HEAD)" = cefa9a963d7c3e191ca0f95cb56ad50eba81de2c ] || exit 27
[ -z "$(git -C "$AOSP/external/v4l2_codec2" status --porcelain --untracked-files=no)" ] || exit 28
[ "$(sha256sum "$OUT/system/vendor/lib/libv4l2_codec2_components.so" | cut -d ' ' -f1)" = 13545d6d439076518fcab98e4c20b74dd1f2feedebcdecf08e634c1129ef088c ] || exit 29
python3 "$AOSP/external/v4l2_codec2/tests/test_g1310_flush_poll_stop_guard.py"
python3 "$PROJECT/linux-7.3-port/tools/g1311-verify-codec2-poll-stop-system.py" --staging
for p in "${FILES[@]}"; do rm -- "$p"; done
cd "$AOSP"
export BUILD_NUMBER=SMT311G1311
export MKE2FS_CONFIG="$AOSP/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
export PATH="$PROJECT/tools/smt311/aosp-python3-shim:$PATH"
set +u
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
m -j4 systemimage
WANT=samsung/lineage_lt013g/lt013g:11/RQ3A.211001.001/SMT311G1311:userdebug/test-keys
grep -Fxq "ro.system.build.fingerprint=$WANT" "$OUT/system/build.prop"
grep -Fxq "ro.vendor.build.fingerprint=$WANT" "$OUT/system/vendor/build.prop"
grep -Fxq ro.build.version.incremental=SMT311G1311 "$OUT/system/build.prop"
python3 "$PROJECT/linux-7.3-port/tools/g1311-verify-codec2-poll-stop-system.py" --audit-image
echo G1311_KEYFRAME_ZERO_GUARD_FULL_EXT4_ARM32_SYSTEM_HOST_PASS_NOT_FLASHED
