#!/usr/bin/env python3
import argparse
import subprocess
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--emit", type=Path)
args = parser.parse_args()

source = (Path(__file__).resolve().parent.parent / "radio/t311-modem-watchdog.sh").read_text()
marker = "no_active_modem_session() {"
start = source.index(marker)
end = source.index("\n}\n", start) + 3
function = source[start:end]
assert "getprop gsm.sim.state" in function
assert "settings get global airplane_mode_on" in function
assert "mobile_data_inactive || return 1" in function

header = """#!/system/bin/sh
set -u
settings() {
    [ "$*" = "get global airplane_mode_on" ] || return 25
    printf '%s' "$MOCK_AIR"
    return "$MOCK_SETTINGS_RC"
}
getprop() {
    [ "$*" = "gsm.sim.state" ] || return 25
    printf '%s' "$MOCK_SIM"
    return "$MOCK_GETPROP_RC"
}
mobile_data_inactive() {
    return "$MOCK_DATA_RC"
}
"""
tests = """
check() {
    label="$1"
    expected="$2"
    MOCK_AIR="$3"
    MOCK_SETTINGS_RC="$4"
    MOCK_SIM="$5"
    MOCK_GETPROP_RC="$6"
    MOCK_DATA_RC="$7"
    if no_active_modem_session; then actual=0; else actual=1; fi
    if [ "$actual" -ne "$expected" ]; then
        printf 'G614_FAIL %s expected=%s actual=%s\\n' "$label" "$expected" "$actual"
        exit 1
    fi
    printf 'G614_PASS %s\\n' "$label"
}
check safe_empty_no_data 0 0 0 "" 0 0
check airplane_enabled 1 1 0 "" 0 0
check airplane_unknown 1 null 0 "" 0 0
check airplane_read_error_even_if_zero 1 0 1 "" 0 0
check missing_airplane_value 1 "" 0 "" 0 0
check whitespace_airplane_value 1 " 0" 0 "" 0 0
check sim_loaded 1 0 0 LOADED 0 0
check sim_unknown 1 0 0 UNKNOWN 0 0
check sim_property_error_even_if_empty 1 0 0 "" 1 0
check sim_property_error_with_text 1 0 0 LOADED 1 0
check mobile_data_active 1 0 0 "" 0 1
check mobile_data_probe_error 1 0 0 "" 0 2
echo G614_WATCHDOG_FAIL_CLOSED_12_CASES_PASS
"""
script = header + function + "\n" + tests
if args.emit:
    args.emit.parent.mkdir(parents=True, exist_ok=True)
    args.emit.write_text(script)
    print("G614_EMITTED", args.emit)
else:
    with tempfile.TemporaryDirectory(prefix="g614-watchdog-") as d:
        generated = Path(d) / "test.sh"
        generated.write_text(script)
        subprocess.run(["sh", "-n", str(generated)], check=True)
        subprocess.run(["sh", str(generated)], check=True)
