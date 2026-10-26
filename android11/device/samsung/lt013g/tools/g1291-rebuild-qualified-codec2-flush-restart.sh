#!/usr/bin/env bash
set -euo pipefail
[ "${1:-}" = "--from-qualified-G1281" ] || exit 21
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
 064cd707784d621831fd1e914855f22aed3906ac41a91fb89788d0b7f4ca74d8
 73a0ff19fe680a8d9e3c32bd4322b5b9d99d27a1d6fb75c84849fbdf1ddaea58
 7bbe3dba6ba85637c0b2f85857576ea65ce6feaa7390c6a29dd5510a7382081b
)
for i in 0 1 2; do
 [ -f "${FILES[$i]}" ] && [ ! -L "${FILES[$i]}" ] || exit 24
 [ "$(sha256sum "${FILES[$i]}" | cut -d ' ' -f1)" = "${SHAS[$i]}" ] || exit 25
 grep -Fq SMT311G1281 "${FILES[$i]}" || exit 26
done
[ "$(git -C "$AOSP/external/v4l2_codec2" rev-parse HEAD)" = 38fe6979f18237e0f177d10441c86ba8065718a4 ] || exit 27
[ -z "$(git -C "$AOSP/external/v4l2_codec2" status --porcelain --untracked-files=no)" ] || exit 28
[ "$(sha256sum "$OUT/system/vendor/lib/libv4l2_codec2_components.so" | cut -d ' ' -f1)" = e0a27d52f4560e0f60ca7f2ff460a684f8a35a7d322bf599d062188b06cf6447 ] || exit 29
python3 "$AOSP/external/v4l2_codec2/tests/test_g1290_flush_restart_ownership.py"
python3 "$PROJECT/linux-7.3-port/tools/g1291-verify-codec2-flush-restart-system.py" --staging
for p in "${FILES[@]}"; do rm -- "$p"; done
cd "$AOSP"
export BUILD_NUMBER=SMT311G1291
export MKE2FS_CONFIG="$AOSP/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
export PATH="$PROJECT/tools/smt311/aosp-python3-shim:$PATH"
set +u
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
m -j4 systemimage
WANT=samsung/lineage_lt013g/lt013g:11/RQ3A.211001.001/SMT311G1291:userdebug/test-keys
grep -Fxq "ro.system.build.fingerprint=$WANT" "$OUT/system/build.prop"
grep -Fxq "ro.vendor.build.fingerprint=$WANT" "$OUT/system/vendor/build.prop"
grep -Fxq ro.build.version.incremental=SMT311G1291 "$OUT/system/build.prop"
python3 "$PROJECT/linux-7.3-port/tools/g1291-verify-codec2-flush-restart-system.py" --audit-image
echo G1291_NEW_CODEC2_FLUSH_RESTART_FULL_EXT4_ARM32_SYSTEM_HOST_PASS_NOT_FLASHED
