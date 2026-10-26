#!/usr/bin/env python3
import argparse
import subprocess
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--emit", type=Path)
args = parser.parse_args()
source = (Path(__file__).resolve().parent.parent / "radio/t311-modem-watchdog.sh").read_text()
start = source.index("final_recovery_guard() {")
end = source.index("\n}\n", start) + 3
guard = source[start:end]
probe_start = source.index("radio_hal_confirmed_present() {")
probe_end = source.index("\n}\n", probe_start) + 3
probe = source[probe_start:probe_end]
assert probe.count("timeout -k 2 3 /vendor/bin/t311-radio-hal-probe") == 1
probe = probe.replace("radio_hal_confirmed_present()", "radio_hal_confirmed_present_actual_test()")
probe = probe.replace("[ -x /vendor/bin/t311-radio-hal-probe ]", "[ \"$MOCK_EXECUTABLE\" = 1 ]")
assert "healthy && ! radio_hal_registered && no_active_modem_session" in guard
assert "healthy && radio_hal_confirmed_present &&" in guard
assert "! healthy && no_active_modem_session" in guard
assert source.count("final_recovery_guard missing_hal || return 0") == 1
assert source.count("final_recovery_guard empty_sim || return 0") == 1
assert source.count("final_recovery_guard offline || exit 0") == 1

prefix = """#!/system/bin/sh
set -u
healthy() { [ "$MOCK_HEALTHY" = 1 ]; }
radio_hal_registered() { [ "$MOCK_HAL" = 1 ]; }
radio_hal_confirmed_present() { [ "$MOCK_PROBE" = 1 ]; }
no_active_modem_session() { [ "$MOCK_INACTIVE" = 1 ]; }
timeout() { return "$MOCK_PROBE_EXIT"; }
"""
test = """
count=0
for h in 0 1; do
 for r in 0 1; do
  for p in 0 1; do
   for i in 0 1; do
    MOCK_HEALTHY="$h"
    MOCK_HAL="$r"
    MOCK_PROBE="$p"
    MOCK_INACTIVE="$i"
    for mode in missing_hal empty_sim offline; do
     expected=1
     case "$mode" in
      missing_hal) [ "$h" = 1 ] && [ "$r" = 0 ] && [ "$i" = 1 ] && expected=0 ;;
      empty_sim) [ "$h" = 1 ] && [ "$p" = 1 ] && [ "$i" = 1 ] && expected=0 ;;
      offline) [ "$h" = 0 ] && [ "$i" = 1 ] && expected=0 ;;
     esac
     if final_recovery_guard "$mode"; then actual=0; else actual=1; fi
     if [ "$actual" -ne "$expected" ]; then
      echo "G615_FAIL $mode healthy=$h hal=$r probe=$p inactive=$i expected=$expected actual=$actual"
      exit 1
     fi
     count=$((count + 1))
    done
   done
  done
 done
done
if final_recovery_guard invalid_recovery_mode; then
 echo G615_FAIL unexpected_mode_authorized
 exit 1
fi
count=$((count + 1))
[ "$count" = 49 ] || exit 1
echo G615_FINAL_RESTART_GUARD_49_CASES_PASS
for status in 0 1 2 124 126 127; do
 MOCK_EXECUTABLE=1
 MOCK_PROBE_EXIT="$status"
 if radio_hal_confirmed_present_actual_test; then actual=0; else actual=1; fi
 if [ "$status" -eq 0 ] && [ "$actual" -ne 0 ]; then
  echo G615_FAIL probe_success
  exit 1
 fi
 if [ "$status" -ne 0 ] && [ "$actual" -eq 0 ]; then
  echo G615_FAIL probe_error_was_accepted
  exit 1
 fi
done
MOCK_EXECUTABLE=0
MOCK_PROBE_EXIT=0
if radio_hal_confirmed_present_actual_test; then
 echo G615_FAIL missing_probe_was_accepted
 exit 1
fi
echo G615_PROBE_STRICT_SUCCESS_7_CASES_PASS
"""
script = prefix + guard + "\n" + probe + "\n" + test
if args.emit:
    args.emit.parent.mkdir(parents=True, exist_ok=True)
    args.emit.write_text(script)
    print("G615_EMITTED", args.emit)
else:
    with tempfile.TemporaryDirectory(prefix="g615-watchdog-") as tmp:
        path = Path(tmp) / "selftest.sh"
        path.write_text(script)
        subprocess.run(["sh", "-n", str(path)], check=True)
        subprocess.run(["sh", str(path)], check=True)
