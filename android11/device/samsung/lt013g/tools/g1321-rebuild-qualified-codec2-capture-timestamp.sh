#!/usr/bin/env bash
set -euo pipefail
[ "${1:-}" = "--from-qualified-G1311" ] || exit 21
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
 b762748062a9666658e16cd15b7c87b2f0bf0a33d23e880ef8e3b6715f436d41
 241749102a71a4a3a4cca698f6507794bf3b143da1d1e34d3e2ae38093105922
 10e168a33e1d122ee0113432cb05ac760a25f142985979ad834aa7af601352ea
)
for i in 0 1 2; do
 [ -f "${FILES[$i]}" ] && [ ! -L "${FILES[$i]}" ] || exit 24
 [ "$(sha256sum "${FILES[$i]}" | cut -d ' ' -f1)" = "${SHAS[$i]}" ] || exit 25
 grep -Fq SMT311G1311 "${FILES[$i]}" || exit 26
done
[ "$(git -C "$AOSP/external/v4l2_codec2" rev-parse HEAD)" = 04b56e868f3f976753817dcdc715354b78f99c57 ] || exit 27
[ -z "$(git -C "$AOSP/external/v4l2_codec2" status --porcelain --untracked-files=no)" ] || exit 28
[ "$(sha256sum "$OUT/system/vendor/lib/libv4l2_codec2_components.so" | cut -d ' ' -f1)" = b5c2392f6bcfa0c6c5f15773b85486a07407205b5a6dede04adaaf8028a5a59e ] || exit 29
python3 "$AOSP/external/v4l2_codec2/tests/test_g1320_capture_negative_timestamp.py"
python3 "$PROJECT/linux-7.3-port/tools/g1321-verify-codec2-capture-timestamp-system.py" --staging
for p in "${FILES[@]}"; do rm -- "$p"; done
cd "$AOSP"
export BUILD_NUMBER=SMT311G1321
export MKE2FS_CONFIG="$AOSP/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
export PATH="$PROJECT/tools/smt311/aosp-python3-shim:$PATH"
set +u
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
m -j4 systemimage
WANT=samsung/lineage_lt013g/lt013g:11/RQ3A.211001.001/SMT311G1321:userdebug/test-keys
grep -Fxq "ro.system.build.fingerprint=$WANT" "$OUT/system/build.prop"
grep -Fxq "ro.vendor.build.fingerprint=$WANT" "$OUT/system/vendor/build.prop"
grep -Fxq ro.build.version.incremental=SMT311G1321 "$OUT/system/build.prop"
python3 "$PROJECT/linux-7.3-port/tools/g1321-verify-codec2-capture-timestamp-system.py" --audit-image
echo G1321_CAPTURE_TIMESTAMP_GUARD_FULL_EXT4_ARM32_SYSTEM_HOST_PASS_NOT_FLASHED
