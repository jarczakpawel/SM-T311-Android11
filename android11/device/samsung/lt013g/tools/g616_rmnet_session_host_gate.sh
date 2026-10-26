#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
cc -std=c11 -O2 -Wall -Wextra -Werror \
    "$root/radio/tests/t311_mobile_session_probe_host_test.c" \
    -o "$temporary/rmnet-test"
"$temporary/rmnet-test"
cc -std=c11 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$root/radio/tests/t311_mobile_session_probe_host_test.c" \
    -o "$temporary/rmnet-asan"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 "$temporary/rmnet-asan"
echo G616_RMNET_IPV4_IPV6_HOST_GATE_PASS
