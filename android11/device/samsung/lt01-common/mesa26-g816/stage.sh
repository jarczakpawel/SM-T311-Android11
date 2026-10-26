#!/usr/bin/env bash
# G816: fail-closed stage for G815 Mesa 26.2.5 six-library release.
# --verify-only (default) validates but does not write. --stage writes ONLY
# locally ignored Android build prebuilt inputs; never the tablet or partitions.
set -Eeuo pipefail
MODE=${1:---verify-only}
case "$MODE" in --verify-only|--verify-staged|--stage) ;; *) echo 'Usage: stage.sh [--verify-only|--verify-staged|--stage]' >&2; exit 2;; esac
ROOT=${SMT311_PROJECT_ROOT:-/home/debian/Projekty/samsung}
ROM=${SMT311_ANDROID_ROOT:-/home/debian/android/lineage-18.1-sm-t311}
SRC=${SMT311_MESA2625_INPUT_LIBDIR:-"$ROOT/artifacts/SM-T311-MESA-26.2.5-G815-VERIFIED-UPSTREAM-20261022/isolated-libs"}
DST="$ROM/device/samsung/lt01-common/prebuilt/pjarczak/mesa26-2625"
MANIFEST="$(dirname "${BASH_SOURCE[0]}")/mesa2625-arm32.sha256"
[ -f "$MANIFEST" ] || { echo "G867 missing pinned manifest $MANIFEST" >&2; exit 9; }
if [ "$MODE" = --verify-staged ]; then
  while read -r source dest expected; do
    [ -n "$source" ] || continue
    [ -f "$DST/$dest" ] || { echo "G867 missing staged $DST/$dest" >&2; exit 14; }
    # Kati/Soong uses a hermetic PATH which may omit `file`, `cut` and even
    # `sha256sum`. Use fixed system binaries; never silently skip the check.
    [[ "$(/usr/bin/file "$DST/$dest")" == *"ELF 32-bit LSB shared object, ARM, EABI5"* ]] || { echo "G867 bad ARM ABI $dest" >&2; exit 15; }
    actual=$(/usr/bin/sha256sum "$DST/$dest")
    [ "${actual%% *}" = "$expected" ] || { echo "G867 staged SHA mismatch $dest" >&2; exit 16; }
  done < "$MANIFEST"
  echo G867_SIX_STAGED_ARM32_SHA256_PASS
  exit 0
fi
while IFS=' ' read -r source dest expected; do
  [ -n "$source" ] || continue
  test -f "$SRC/$source" || { echo "Missing $SRC/$source" >&2; exit 10; }
  file "$SRC/$source" | grep -Eq 'ELF 32-bit LSB shared object, ARM, EABI5' || { echo 'Wrong ARM ABI' >&2; exit 11; }
  actual=$(sha256sum "$SRC/$source" | cut -d' ' -f1)
  [ "$actual" = "$expected" ] || { echo "G816 SHA mismatch $source" >&2; exit 12; }
done < "$MANIFEST"
echo G816_SIX_ARMSHARED_LIBRARY_HASHES_PASS
if [ "$MODE" = --verify-only ]; then
  echo G816_VERIFY_ONLY_NO_WRITE
  exit 0
fi
mkdir -p "$DST"
while IFS=' ' read -r source dest expected; do
  [ -n "$source" ] || continue
  install -m 0644 "$SRC/$source" "$DST/$dest"
  actual=$(sha256sum "$DST/$dest" | cut -d' ' -f1)
  [ "$actual" = "$expected" ] || { echo "G816 copied SHA mismatch $dest" >&2; exit 13; }
done < "$MANIFEST"
echo G816_ARM32_SIX_LIB_ARTIFACTS_STAGED_FOR_OPTIONAL_ROM_BUILD_NO_DEVICE_INSTALL
