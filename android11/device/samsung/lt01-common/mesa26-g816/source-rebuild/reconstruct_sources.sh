#!/usr/bin/env bash
# G815: reproducibly reconstruct patched Mesa26.2.5 + private libdrm2.4.133.
# Uses only pinned official tarballs and source-only patches; NEVER touches Android or recovery.
set -Eeuo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Identical source-relative paths are required for byte-identical Mesa ELFs.
: "${SMT311_MESA_PROJECT_ROOT:?Set canonical SM-T311 project root containing worktrees/ and out/}"
PROJECT="$(realpath -m "$SMT311_MESA_PROJECT_ROOT")"
BUILD_ROOT=${SMT311_MESA_BUILD_DIR:-"$PROJECT/out/SM-T311-MESA2625-LOG"}
PATCHES="$HERE/patches"
QPATCH="$PATCHES/mesa-android-queuebuffer-failure-ownership.OFFLINE.patch"
: "${SMT311_MESA_TARBALL:?Set official Mesa 26.2.5 tar.xz input path}"
: "${SMT311_LIBDRM_TARBALL:?Set official libdrm 2.4.133 tar.xz input path}"
MESA_ARCHIVE=$SMT311_MESA_TARBALL
DRM_ARCHIVE=$SMT311_LIBDRM_TARBALL
DEST=${SMT311_MESA_STAGE:-$PROJECT/worktrees/mesa-26.2.5}
sha(){ sha256sum "$1" | cut -d' ' -f1; }
[ "$(sha "$MESA_ARCHIVE")" = 794b3d1729075dae7ebd47c7283b7ca0cdb5dec668f7f11cc0a5d82f6138a0a5 ] || { echo 'Wrong Mesa release digest' >&2; exit 11; }
[ "$(sha "$DRM_ARCHIVE")" = fc68f9d0ba2ea63c9432a299e14fea09fad7a8a66e8039fcd7802ca59f77b4f5 ] || { echo 'Wrong libdrm digest' >&2; exit 12; }
[ ! -e "$DEST" ] || { echo 'Destination exists, refusing to overwrite' >&2; exit 13; }
mkdir -p "$(dirname "$DEST")"
TMP=$(mktemp -d "$(dirname "$DEST")/.g815-reproduce.XXXXXXXX")
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/libdrm"
tar -xJf "$MESA_ARCHIVE" -C "$TMP"
tar -xJf "$DRM_ARCHIVE" -C "$TMP/libdrm"
MESASRC="$TMP/mesa-26.2.5"
DRMSRC="$TMP/libdrm/libdrm-2.4.133"
[ "$(cat "$MESASRC/VERSION")" = 26.2.5 ]
for p in "$PATCHES"/mesa-build-id-android-libdl.patch "$PATCHES"/mesa-loader-android-sysmacros.patch "$QPATCH"; do
 patch --batch --fuzz=0 -p1 -d "$MESASRC" -i "$p" > /dev/null
done
for p in "$PATCHES"/libdrm-2.4.133-android-sysmacros.patch "$PATCHES"/libdrm-2.4.133-android-visibility.patch "$PATCHES"/libdrm-private-soname.patch; do
 patch --batch --fuzz=0 -p1 -d "$DRMSRC" -i "$p" > /dev/null
done
mv "$DRMSRC" "$MESASRC/subprojects/libdrm-2.4.133"
mv "$MESASRC" "$DEST"
echo G815_MESA26_2_5_LIBDRM_REPRODUCED
printf 'Mesa source: %s\n' "$DEST"
sha "$DEST/src/egl/drivers/dri2/platform_android.c" "$DEST/src/loader/loader.c" "$DEST/src/util/build_id.h"
