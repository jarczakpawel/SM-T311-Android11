#!/usr/bin/env bash
set -Eeuo pipefail
P=/home/debian/Projekty/samsung
A=/home/debian/android/lineage-18.1-sm-t311
SRC=$P/worktrees/mesa-26.2.5
OUT=$P/out/SM-T311-G1747-MESA2625-GLPB-OPTIN
CROSS=$P/out/G872-MESA2625-LOG/generated-cross-verified.cross
ZLIB=$A/out/target/product/lt013g/system/lib/libz.so
export PYTHONPATH=$P/artifacts/SM-T311-MESA-26.2.4-GFX-20261009/python-deps${PYTHONPATH:+:$PYTHONPATH}
export PATH=$A/out/t311-hostbin:$PATH
[ "$(cat "$SRC/VERSION")" = 26.2.5 ] || exit 21
[ "$(realpath --relative-to="$OUT" "$SRC")" = ../../worktrees/mesa-26.2.5 ] || exit 22
[ -f "$CROSS" ] || exit 23
[ -e "$SRC/src/gallium/drivers/lima/lima_disk_cache.c" ] || exit 24
[ "$(sha256sum "$ZLIB" | cut -d " " -f1)" = 5f4b1502a24b91c939f7534b9f03a80480d63b9a81008977a933b87993b2e7d8 ] || exit 25
[ ! -e "$OUT/src/gallium/targets/dri/libgallium_dri.so" ] || exit 26
mkdir -p "$OUT/pkgconfig"
printf "prefix=%s\nlibdir=\${prefix}\nName: zlib\nDescription: Android 11 ARM32 zlib\nVersion: 1.3.2\nLibs: -L\${libdir} -lz\n" "$(dirname "$ZLIB")" > "$OUT/pkgconfig/zlib.pc"
export PKG_CONFIG_LIBDIR=$OUT/pkgconfig
args=( \
 --cross-file "$CROSS" \
 -Dallow-fallback-for=libdrm --wrap-mode=nodownload \
 -Dplatforms=android -Dplatform-sdk-version=30 -Dandroid-stub=true \
 -Dgallium-drivers=lima -Dvulkan-drivers= -Dllvm=disabled \
 -Dgbm=enabled -Degl=enabled -Dgles1=enabled -Dgles2=enabled \
 -Dopengl=false -Dxmlconfig=disabled \
 -Dshader-cache=enabled -Dshader-cache-default=false \
 -Dexpat=disabled -Dzstd=disabled -Dzlib=enabled -Dbuild-tests=false \
 -Dbuildtype=release
)
if [ -f "$OUT/meson-private/coredata.dat" ]; then
  meson setup --reconfigure --clearcache "$OUT" "$SRC" "${args[@]}"
else
  meson setup "$OUT" "$SRC" "${args[@]}"
fi
ninja -C "$OUT" -j3 src/gallium/targets/dri/libgallium_dri.so
LIB="$OUT/src/gallium/targets/dri/libgallium_dri.so"
[ -s "$LIB" ] || exit 27
file "$LIB"
sha256sum "$LIB"
printf '%s\n' G1747_ISOLATED_MESA2625_GL_SHADER_CACHE_BUILT_NOT_RELEASED_NOT_STAGED_NOT_FLASHED
