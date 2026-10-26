#!/usr/bin/env python3
"""Test actual watchdog mobile_data_inactive() in a mocked Android shell."""
import argparse
import subprocess
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--emit", type=Path)
args = parser.parse_args()
source = (Path(__file__).resolve().parent.parent / "radio/t311-modem-watchdog.sh").read_text()
start = source.index("mobile_data_inactive() {")
end = source.index("\n}\n", start) + 3
fn = source[start:end]
assert "timeout -k 2 3 /vendor/bin/t311-mobile-session-probe rmnet1" in fn
fn = fn.replace("[ -x /vendor/bin/t311-mobile-session-probe ]", '[ "$MOCK_PRESENT" = 1 ]')
fn = fn.replace("/vendor/bin/t311-mobile-session-probe rmnet1", "mock_native_probe rmnet1")

harness = """#!/system/bin/sh
set -u
MOCK_PRESENT=1
MOCK_PROBE_RESULT=0
MOCK_ARG_FAILURE=0
MOCK_TIMEOUT_ERROR=0
timeout() {
 [ "$1" = "-k" ] && [ "$2" = "2" ] && [ "$3" = "3" ] || return 29
 shift 3
 "$@"
}
mock_native_probe() {
 [ "$1" = "rmnet1" ] || return 29
 [ "$MOCK_ARG_FAILURE" = 0 ] || return 29
 return "$MOCK_PROBE_RESULT"
}
"""
tests = """
count=0
for status in 0 1 2 124 125 126 127 137 143 255; do
 MOCK_PRESENT=1
 MOCK_PROBE_RESULT="$status"
 if mobile_data_inactive; then actual=0; else actual=1; fi
 expected=1
 [ "$status" -eq 0 ] && expected=0
 if [ "$actual" -ne "$expected" ]; then
   echo "G617_FAIL exit_status=$status actual=$actual expected=$expected"
   exit 1
 fi
 count=$((count + 1))
done
MOCK_PRESENT=0
MOCK_PROBE_RESULT=0
if mobile_data_inactive; then echo G617_FAIL_missing_helper;exit 1;fi
count=$((count+1))
MOCK_PRESENT=1
MOCK_ARG_FAILURE=1
if mobile_data_inactive; then echo G617_FAIL_wrong_argument;exit 1;fi
count=$((count+1))
[ "$count" -eq 12 ] || exit 1
echo G617_MOBILE_PROBE_12_TIMEOUT_ERROR_CASES_PASS
"""
script = harness + fn + "\n" + tests
if args.emit:
    args.emit.parent.mkdir(parents=True, exist_ok=True)
    args.emit.write_text(script)
    print("G617_EMITTED", args.emit)
else:
    with tempfile.TemporaryDirectory(prefix="g617-probe-") as temp:
        test = Path(temp) / "test.sh"
        test.write_text(script)
        subprocess.run(["sh", "-n", str(test)], check=True)
        subprocess.run(["sh", str(test)], check=True)
