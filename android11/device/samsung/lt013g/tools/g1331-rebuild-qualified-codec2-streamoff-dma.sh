#!/usr/bin/env bash
set -euo pipefail
[ "${1:-}" = "--from-qualified-G1321" ] || exit 21
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
 54db791e094c9f574707526c789b330111ff71cb838e6b7c038c2fae317c6793
 64f7045bb46fdd7546f1e91b044477b8002e7e587588903da82aabd9414a85f3
 af55b64dcd1289b389fcacb58457033dc7218ab612d4cc53a44b85e9834b8846
)
for i in 0 1 2; do
 [ -f "${FILES[$i]}" ] && [ ! -L "${FILES[$i]}" ] || exit 24
 [ "$(sha256sum "${FILES[$i]}" | cut -d ' ' -f1)" = "${SHAS[$i]}" ] || exit 25
 grep -Fq SMT311G1321 "${FILES[$i]}" || exit 26
done
[ "$(git -C "$AOSP/external/v4l2_codec2" rev-parse HEAD)" = a7d5f847b3a7a3976cc3dc1cf0d3fafa0accb53a ] || exit 27
[ -z "$(git -C "$AOSP/external/v4l2_codec2" status --porcelain --untracked-files=no)" ] || exit 28
[ "$(sha256sum "$OUT/system/vendor/lib/libv4l2_codec2_components.so" | cut -d ' ' -f1)" = d48a5cb0d22460d0134806d52d2a0ae72fb6d3a5afe1b0f4526f7bb8f5e6b356 ] || exit 29
python3 "$AOSP/external/v4l2_codec2/tests/test_g1330_flush_streamoff_dma_lifetime.py"
python3 "$PROJECT/linux-7.3-port/tools/g1331-verify-codec2-streamoff-dma-system.py" --staging
for p in "${FILES[@]}"; do rm -- "$p"; done
cd "$AOSP"
export BUILD_NUMBER=SMT311G1331
export MKE2FS_CONFIG="$AOSP/device/samsung/lt013g/tools/g1150-android11-mke2fs-2019-compat.conf"
export PATH="$PROJECT/tools/smt311/aosp-python3-shim:$PATH"
set +u
source build/envsetup.sh >/dev/null
lunch lineage_lt013g-userdebug >/dev/null
m -j4 systemimage
WANT=samsung/lineage_lt013g/lt013g:11/RQ3A.211001.001/SMT311G1331:userdebug/test-keys
grep -Fxq "ro.system.build.fingerprint=$WANT" "$OUT/system/build.prop"
grep -Fxq "ro.vendor.build.fingerprint=$WANT" "$OUT/system/vendor/build.prop"
grep -Fxq ro.build.version.incremental=SMT311G1331 "$OUT/system/build.prop"
python3 "$PROJECT/linux-7.3-port/tools/g1331-verify-codec2-streamoff-dma-system.py" --audit-image
echo G1331_STREAMOFF_DMA_OWNERSHIP_GUARD_FULL_EXT4_ARM32_SYSTEM_HOST_PASS_NOT_FLASHED
