#!/usr/bin/env bash
# Source-only G815 Mesa26.2.5 Lima ARMv7 on Android 11, no installation.
set -Eeuo pipefail
# Offline build of six libraries from reconstructed official sources.
# G872: Mesa embeds relative __FILE__ paths into .rodata/.text.
# A Meson build folder nested one extra level changes EGL/Gallium/GBM SHA.
# Two fresh 1020-step builds verified that the original relative source path
# ../../worktrees/mesa-26.2.5 restores all six byte-identical ARMv7 outputs.
: "${SMT311_MESA_PROJECT_ROOT:?Set canonical SM-T311 project root containing worktrees/ and out/}"
PROJECT="$(realpath -m "$SMT311_MESA_PROJECT_ROOT")"
BUILD_ROOT=${SMT311_MESA_BUILD_DIR:-"$PROJECT/out/SM-T311-MESA2625-LOG"}
MESA=${SMT311_MESA_SRC:-$PROJECT/worktrees/mesa-26.2.5}
OUT=${SMT311_MESA_OUT:-$PROJECT/out/SM-T311-MESA2625-ARMV7}
REL_SOURCE=$(realpath --relative-to="$OUT" "$MESA")
if [ "$REL_SOURCE" != '../../worktrees/mesa-26.2.5' ]; then
  echo "G872 REFUSING NONREPRODUCIBLE MESA SOURCE PATH: $REL_SOURCE (expected ../../worktrees/mesa-26.2.5)" >&2
  exit 14
fi
LOG=$BUILD_ROOT/meson-setup-armv7.log
: "${SMT311_MESA_CROSS_FILE:?Specify an Android ARMv7/NDK Meson cross file}"
mkdir -p "$BUILD_ROOT"
test "$(cat "$MESA/VERSION")" = '26.2.5'
test -e "$MESA/subprojects/libdrm-2.4.133/meson.build"
python3 -c 'import mako,yaml; assert mako.__version__=="1.3.10"'
args=(
 --cross-file "$SMT311_MESA_CROSS_FILE"
 -Dallow-fallback-for=libdrm
 --wrap-mode=nodownload
 -Dplatforms=android -Dplatform-sdk-version=30 -Dandroid-stub=true
 -Dgallium-drivers=lima -Dvulkan-drivers= -Dllvm=disabled
 -Dgbm=enabled -Degl=enabled -Dgles1=enabled -Dgles2=enabled
 -Dopengl=false -Dxmlconfig=disabled -Dshader-cache=disabled
 -Dexpat=disabled -Dzstd=disabled -Dzlib=disabled -Dbuild-tests=false
 -Dbuildtype=release
)
if [ -f "$OUT/meson-private/coredata.dat" ]; then
 meson setup --reconfigure "$OUT" "$MESA" "${args[@]}" >"$LOG" 2>&1 || { tail -65 "$LOG";exit 10; }
else
 meson setup "$OUT" "$MESA" "${args[@]}" >"$LOG" 2>&1 || { tail -65 "$LOG";exit 11; }
fi
printf 'G815_MESA_26_2_5_ARMV7_MESON_SETUP_PASS\n'
if [ "${1:-}" = '--setup-only' ]; then exit 0; fi
# Only device runtime libraries; full Ninja `all` also builds irrelevant Nouveau
# libdrm test programs, which cannot compile against Android Bionic's
# overloaded ioctl declaration. Do not interpret that as a Mesa runtime failure.
targets=(
 src/egl/libEGL.so
 src/mesa/glapi/es1api/libGLESv1_CM.so
 src/mesa/glapi/es2api/libGLESv2.so
 src/gallium/targets/dri/libgallium_dri.so
 src/gbm/libgbm_mesa.so
 subprojects/libdrm-2.4.133/libdrm_mesa26.so
)
ninja -C "$OUT" -j3 "${targets[@]}" >"$BUILD_ROOT/targeted-six-libs-build-armv7.log" 2>&1 || {
 tail -90 "$BUILD_ROOT/targeted-six-libs-build-armv7.log"; exit 12;
}
tail -20 "$BUILD_ROOT/targeted-six-libs-build-armv7.log"
echo G872_TARGETED_SIX_LIBS_ARMV7_BUILD_PASS_NO_INSTALL
# Export six runtime outputs under the exact manifest filenames used by stage.sh.
# This copies only host ARMv7 build outputs, NEVER writes to a tablet or ROM.
LIBDIR=$BUILD_ROOT/isolated-libs
mkdir -p "$LIBDIR"
for entry in \
    "src/egl/libEGL.so:libEGL.so" \
    "src/mesa/glapi/es1api/libGLESv1_CM.so:libGLESv1_CM.so" \
    "src/mesa/glapi/es2api/libGLESv2.so:libGLESv2.so" \
    "src/gallium/targets/dri/libgallium_dri.so:libgallium_dri.so" \
    "src/gbm/libgbm_mesa.so:libgbm_mesa.so" \
    "subprojects/libdrm-2.4.133/libdrm_mesa26.so:libdrm_mesa26.so"; do
    from=${entry%%:*}
    name=${entry#*:}
    install -m 0644 "$OUT/$from" "$LIBDIR/$name"
done
(cd "$LIBDIR" && sha256sum libEGL.so libGLESv1_CM.so libGLESv2.so libgallium_dri.so libgbm_mesa.so libdrm_mesa26.so) > "$BUILD_ROOT/G870_REBUILT_SIX_SHA256SUMS.txt"
echo "G872_SOURCE_REBUILT_SIX_EXPORT_ONLY: $LIBDIR"
# Fail closed in the source build itself: a successful Meson compilation is not
# a qualified SM-T311 graphics release until all six device-live SHA match.
STAGE_SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/stage.sh"
SMT311_MESA2625_INPUT_LIBDIR="$LIBDIR" "$STAGE_SCRIPT" --verify-only >/dev/null || {
  echo 'G872 REBUILT ARM32 MESA SHA DO NOT MATCH PHYSICAL TABLET' >&2
  exit 15
}
echo 'G872_MESA2625_SIX_OF_SIX_SOURCE_REBUILT_SHA_MATCH_PHYSICAL_PASS'
echo "To stage, set SMT311_MESA2625_INPUT_LIBDIR=$LIBDIR and run stage.sh --verify-only before --stage."
