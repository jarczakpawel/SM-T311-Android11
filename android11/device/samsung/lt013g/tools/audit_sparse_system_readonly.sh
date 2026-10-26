#!/usr/bin/env bash
# Offline integrity check for the actual SYSTEM image in freshly built target-files.
# This must never write to or mount the SM-T311 tablet's block devices.
set -eo pipefail
if [[ $# -ne 2 ]]; then echo "Usage: $0 <target-files staging root> <audit directory>" >&2; exit 2; fi
root="$(readlink -f "$1")"
report="$2"
AOSP="$(readlink -f "$(dirname "${BASH_SOURCE[0]}")/../../../..")"
image="$root/IMAGES/system.img"
if [[ ! -f "$root/META/misc_info.txt" || ! -f "$image" ]]; then
  echo 'Missing target-files staging image or metadata' >&2; exit 3
fi
mkdir -p "$report"
report="$(readlink -f "$report")"
raw="$report/G268-SYSTEM-UNSPARSE-TEST-ONLY.img"
trap 'rm -f -- "$raw"' EXIT
if [[ -e "$raw" ]]; then echo 'Unsafe: temporary destination already exists' >&2; exit 4; fi
"$AOSP/out/host/linux-x86/bin/simg2img" "$image" "$raw"
expected=2235564032
actual="$(stat -c %s "$raw")"
if [[ "$actual" != "$expected" ]]; then
  echo "SYSTEM expanded size wrong: $actual expected $expected" >&2; exit 5
fi
# -fn checks a readonly snapshot. Output may contain errors that must be
# reviewed; do not use automatic repair or operate on physical partitions.
set +e
"$AOSP/out/host/linux-x86/bin/e2fsck" -fn "$raw" > "$report/G268-SYSTEM-EXT4-FSCK-READONLY.log" 2>&1
rc=$?
set -e
printf 'expanded_bytes=%s\ne2fsck_exit=%s\n' "$actual" "$rc" > "$report/G268-EXT4-CHECK-RESULT.txt"
tail -20 "$report/G268-SYSTEM-EXT4-FSCK-READONLY.log"
if [[ "$rc" != 0 ]]; then
  echo "G268 SYSTEM ext4 check failed rc=$rc; inspect log" >&2
  exit "$rc"
fi
echo 'G268 SYSTEM EXT4 READONLY PASS'
