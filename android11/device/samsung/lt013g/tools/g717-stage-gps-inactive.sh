#!/usr/bin/env bash
# G717: deploy ONLY the inactive GPS candidate into /data/local/tmp.
# Does not restart any service, remount /vendor, flash or alter SELinux.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ANDROID_TOP="$(cd -- "$ROOT/../../.." && pwd)"
SOURCE="$ANDROID_TOP/out/target/product/lt013g/system/vendor/lib/hw/gps.default.so"
EXPECTED="c85b7b9e28f6bc8ac35d94d9b56011efde66b7d555dab1e7f7e3a5d90e7215a8"
REMOTE="/data/local/tmp/g717-gps-candidate.so"
test -f "$SOURCE"
test "$(stat -c %s "$SOURCE")" = "36032"
test "$(sha256sum "$SOURCE" | cut -d' ' -f1)" = "$EXPECTED"
test "$(adb devices | awk '$2 == "device" {print $1}' | wc -l)" = "1"
test "$(adb shell getprop ro.product.model | tr -d '\r')" = "SM-T311" ||
    test "$(adb shell getprop ro.product.model | tr -d '\r')" = "SM_T311"
test "$(adb shell getprop sys.boot_completed | tr -d '\r')" = "1"
adb push "$SOURCE" "$REMOTE"
ACTUAL="$(adb shell sha256sum "$REMOTE" | awk '{print $1}')"
test "$ACTUAL" = "$EXPECTED"
echo "G717 SHA VERIFIED: $ACTUAL"
echo "STAGED ONLY: $REMOTE; active /vendor/lib/hw/gps.default.so unchanged"
