#!/usr/bin/env bash
# Install the two exact exported Samsung device source snapshots, not vendor blobs.
set -euo pipefail
if [[ $# -ne 1 ]]; then echo "Usage: $0 /path/to/android11" >&2; exit 2; fi
WORKSPACE=$(realpath "$1")
REPO=$(cd "$(dirname "$0")/../.." && pwd)
test -d "$WORKSPACE/.repo"
mkdir -p "$WORKSPACE/device/samsung"
for t in lt013g lt01-common; do
    src="$REPO/android11/device/samsung/$t"
    dst="$WORKSPACE/device/samsung/$t"
    test -d "$src"
    if [[ -e "$dst" ]]; then
        echo "Refusing to overwrite existing $dst" >&2
        exit 3
    fi
    cp -a "$src" "$dst"
    echo "Source installed: device/samsung/$t"
done
echo "Device source installed. Firmware/Mesa private inputs are NOT included."
