#!/usr/bin/env python3
"""Test source-extracted CBD PID identity parser without accessing /proc or signaling."""
import argparse
import subprocess
import tempfile
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument("--emit", type=Path)
args = ap.parse_args()

source = (Path(__file__).resolve().parent.parent / "radio/t311-modem-watchdog.sh").read_text()
start = source.index("cbd_pid_identity() {")
end = source.index("\n}\n", start) + 3
actual_fn = source[start:end]
assert source.count('[ "$(cbd_pid_identity "$pid")" = "$cbd_identity" ]') == 3
assert 'kill -9 "$pid"' not in source
assert source.count('/vendor/bin/t311-cbd-pidfd-signal "$pid" "$cbd_identity"') == 3
assert "shift 19" in actual_fn

def record(pid="305", comm="t311-cbd", start_time="613"):
    after_comm = ["S"] + [str(i) for i in range(4, 22)] + [start_time, "1", "1"]
    assert len(after_comm) == 22
    return f"{pid} ({comm}) " + " ".join(after_comm)

def q(s):
    return "'" + s.replace("'", "'\\''") + "'"

script = """#!/system/bin/sh
set -u
readlink() {
 [ "$1" = "/proc/305/exe" ] || return 7
 [ "$MOCK_READLINK_OK" = 1 ] || return 7
 printf '%s\\n' "$MOCK_EXE"
}
cat() {
 [ "$1" = "/proc/305/stat" ] || return 7
 [ "$MOCK_CAT_OK" = 1 ] || return 7
 printf '%s\\n' "$MOCK_RECORD"
}
""" + actual_fn + """
test_count=0
check() {
 label="$1" expected="$2" requested_pid="$3"
 if output="$(cbd_pid_identity "$requested_pid")"; then rc=0; else rc=1; fi
 if [ "$rc" -ne "$expected" ]; then
  echo "G618_FAIL $label rc=$rc expected=$expected output=$output"
  exit 1
 fi
 test_count=$((test_count+1))
}
MOCK_READLINK_OK=1
MOCK_CAT_OK=1
MOCK_EXE=/vendor/bin/t311-cbd
MOCK_RECORD=""" + q(record()) + """
check valid_vendor 0 305
[ "$(cbd_pid_identity 305)" = "/vendor/bin/t311-cbd|613" ] || exit 1
MOCK_EXE=/system/vendor/bin/t311-cbd
check valid_system_vendor 0 305
MOCK_EXE=/system/bin/other-program
check wrong_executable 1 305
MOCK_EXE=/vendor/bin/t311-cbd
MOCK_RECORD=""" + q(record(comm="unrelated")) + """
check wrong_process_name 1 305
MOCK_RECORD=""" + q(record(pid="306")) + """
check recycled_pid_field 1 305
MOCK_RECORD=""" + q(record(start_time="bad")) + """
check malformed_starttime 1 305
MOCK_RECORD=""" + q("305 (t311-cbd) S 1 2") + """
check short_proc_record 1 305
MOCK_RECORD=""" + q(record()) + """
MOCK_READLINK_OK=0
check readlink_failure 1 305
MOCK_READLINK_OK=1
MOCK_CAT_OK=0
check stat_read_failure 1 305
MOCK_CAT_OK=1
check bad_pid 1 "305 306"
check empty_pid 1 ""
check nonnumeric_pid 1 abc
original="$(cbd_pid_identity 305)"
MOCK_RECORD=""" + q(record(start_time="614")) + """
[ "$(cbd_pid_identity 305)" != "$original" ] || exit 1
test_count=$((test_count+1))
MOCK_EXE=/system/vendor/bin/t311-cbd
[ "$(cbd_pid_identity 305)" != "$original" ] || exit 1
test_count=$((test_count+1))
[ "$test_count" -eq 14 ] || exit 1
echo G618_CBD_PID_IDENTITY_14_CASES_PASS
"""
if args.emit:
    args.emit.parent.mkdir(parents=True, exist_ok=True)
    args.emit.write_text(script)
    print("G618_EMITTED", args.emit)
else:
    with tempfile.TemporaryDirectory(prefix="g618-pid-") as d:
        path = Path(d) / "test.sh"
        path.write_text(script)
        subprocess.run(["sh", "-n", str(path)], check=True)
        subprocess.run(["sh", str(path)], check=True)
