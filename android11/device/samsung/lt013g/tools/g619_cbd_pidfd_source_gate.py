#!/usr/bin/env python3
"""Release gate for G619: no recycled-PID kill fallback anywhere."""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
script = (root / "radio/t311-modem-watchdog.sh").read_text()
helper = (root / "radio/t311_cbd_pidfd_signal.c").read_text()
android_bp = (root / "radio/Android.bp").read_text()
packages = (root / "t311-platform.mk").read_text()
file_contexts = (root.parent / "lt01-common/sepolicy/vendor/file_contexts").read_text()

assert 'kill -9 "$pid"' not in script
assert script.count('/vendor/bin/t311-cbd-pidfd-signal "$pid" "$cbd_identity"') == 3
assert script.count('[ -x /vendor/bin/t311-cbd-pidfd-signal ]') == 3
assert script.count('[ "$(cbd_pid_identity "$pid")" = "$cbd_identity" ]') == 3
assert "SYS_pidfd_open" in helper and "SYS_pidfd_send_signal" in helper
assert "signal_no = check_only ? 0 : SIGKILL" in helper
assert helper.index("SYS_pidfd_open") < helper.index("verified_identity(pid, identity)")
assert helper.index("verified_identity(pid, identity)") < helper.index("SYS_pidfd_send_signal")
assert "\n    kill(" not in helper and "SYS_kill" not in helper
assert "T311_CBD_EXEC_A" in helper and "T311_CBD_EXEC_B" in helper
assert 'name: "t311-cbd-pidfd-signal"' in android_bp
assert 'name: "t311-cbd-pidfd-native-test"' in android_bp
assert "t311-cbd-pidfd-signal" in packages
assert "/bin/t311-cbd-pidfd-signal u:object_r:lt013g_modem_watchdog_probe_exec:s0" in file_contexts
print("G619_CBD_PIDFD_THREE_PATHS_NO_FALLBACK_BUILD_AND_LABEL_GATE_PASS")
