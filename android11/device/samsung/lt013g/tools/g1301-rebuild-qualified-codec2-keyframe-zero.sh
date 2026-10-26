#!/usr/bin/env bash
set -euo pipefail
[ "${1:-}" = "--from-qualified-G1291" ] || exit 21
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
 d8abcb3e5bf5399c48979ae5e78bb09700ec58a96d6091b759f62fe4127f615d
 2b4f35b24bfbfdae374f7325d290522bdd4fd25e7bb9d38044b2ea0b1d77bfd3
 ad8b449aedf7251e561d277a0c30353d5cd03652b0297b18e6ba78249bd4e3ae
)
for i in 0 1 2; do
 [ -f "${FILES[$i]}" ] && [ ! -L "${FILES[$i]}" ] || exit 24
 [ "$(sha256sum "${FILES[$i]}" | cut -d ' ' -f1)" = "${SHAS[$i]}" ] || exit 25
 grep -Fq SMT311G1291 "${FILES[$i]}" || exit 26
done
[ "$(git -C "$AOSP/external/v4l2_codec2" rev-parse HEAD)" = 75095bb40f215764740558cd98d4db8d2fecd398 ] || exit 27
[ -z "$(git -C "$AOSP/external/v4l2_codec2" status --porcelain --untracked-files=no)" ] || exit 28
[ "$(sha256sum "$OUT/system/vendor/lib/libv4l2_codec2_components.so" | cut -d ' ' -f1)" = 6e6ea3aaa02f69da3471e41e15ea9bbed22b66d06e6c3abc90a8b29f829a9a06 ] || exit 29
python3 "$PROJECT/linux-7.3-port/tools/g1301-verify-codec2-keyframe-period-system.py" --staging
for p in "${FILES[@]}"; do rm -- "$p"; done
cd "$AOSP"
export BUILD_NUMBER=SMT311G1301
export MKE2FS_CONFIG="$AOSP/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
export PATH="$PROJECT/tools/smt311/aosp-python3-shim:$PATH"
set +u
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
m -j4 systemimage
WANT=samsung/lineage_lt013g/lt013g:11/RQ3A.211001.001/SMT311G1301:userdebug/test-keys
grep -Fxq "ro.system.build.fingerprint=$WANT" "$OUT/system/build.prop"
grep -Fxq "ro.vendor.build.fingerprint=$WANT" "$OUT/system/vendor/build.prop"
grep -Fxq ro.build.version.incremental=SMT311G1301 "$OUT/system/build.prop"
python3 "$PROJECT/linux-7.3-port/tools/g1301-verify-codec2-keyframe-period-system.py" --audit-image
echo G1301_KEYFRAME_ZERO_GUARD_FULL_EXT4_ARM32_SYSTEM_HOST_PASS_NOT_FLASHED
