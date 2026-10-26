#!/usr/bin/env bash
set -euo pipefail
[ "${1:-}" = "--from-qualified-G1331" ] || exit 21
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
 631de46187c3f4a28912f1b484b93a0623362f9d62a0e853cf23e41c1e4e2cd1
 a7a615e2c053ff7688d80105ec1933668fe7cae2a203810cb54daaba9c2bd80c
 645d283e1ec9add7246355651204b7ae9e0f17cf697d6ed9a4f483182dacf2cb
)
for i in 0 1 2; do
 [ -f "${FILES[$i]}" ] && [ ! -L "${FILES[$i]}" ] || exit 24
 [ "$(sha256sum "${FILES[$i]}" | cut -d ' ' -f1)" = "${SHAS[$i]}" ] || exit 25
 grep -Fq SMT311G1331 "${FILES[$i]}" || exit 26
done
[ "$(git -C "$AOSP/external/v4l2_codec2" rev-parse HEAD)" = 6ea8a3b0b404d54dd1b140abfb8c9ddd1c7bcf11 ] || exit 27
[ -z "$(git -C "$AOSP/external/v4l2_codec2" status --porcelain --untracked-files=no)" ] || exit 28
[ "$(sha256sum "$OUT/system/vendor/lib/libv4l2_codec2_components.so" | cut -d ' ' -f1)" = 389ef7f0c241b2885a452dfd835527440cb05d07f18a92fadef535bea9918a2b ] || exit 29
python3 "$AOSP/external/v4l2_codec2/tests/test_g1350_accepted_work_fatal_error_completion.py"
python3 "$PROJECT/linux-7.3-port/tools/g1351-verify-codec2-accepted-work-system.py" --staging
for p in "${FILES[@]}"; do rm -- "$p"; done
cd "$AOSP"
export BUILD_NUMBER=SMT311G1351
export MKE2FS_CONFIG="$AOSP/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
export PATH="$PROJECT/tools/smt311/aosp-python3-shim:$PATH"
set +u
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
m -j4 systemimage
WANT=samsung/lineage_lt013g/lt013g:11/RQ3A.211001.001/SMT311G1351:userdebug/test-keys
grep -Fxq "ro.system.build.fingerprint=$WANT" "$OUT/system/build.prop"
grep -Fxq "ro.vendor.build.fingerprint=$WANT" "$OUT/system/vendor/build.prop"
grep -Fxq ro.build.version.incremental=SMT311G1351 "$OUT/system/build.prop"
python3 "$PROJECT/linux-7.3-port/tools/g1351-verify-codec2-accepted-work-system.py" --audit-image
echo G1351_ASYNC_ACCEPTED_WORK_ERROR_COMPLETION_FULL_EXT4_ARM32_SYSTEM_HOST_PASS_NOT_FLASHED
