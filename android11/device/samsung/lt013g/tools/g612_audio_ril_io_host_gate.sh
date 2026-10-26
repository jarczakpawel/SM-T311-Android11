#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT HUP INT TERM
cc -std=c11 -O2 -Wall -Wextra -Werror -pthread \
    "$root/radio/tests/t311_audio_ril_io_host_test.c" -o "$temp/io-test"
"$temp/io-test"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -pthread \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$root/radio/tests/t311_audio_ril_io_host_test.c" -o "$temp/io-test-sanitized"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 "$temp/io-test-sanitized"
echo G612_AUDIO_BRIDGE_HOST_GATE_PASS
