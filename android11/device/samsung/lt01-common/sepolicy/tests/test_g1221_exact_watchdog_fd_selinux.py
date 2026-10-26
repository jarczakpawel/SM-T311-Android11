#!/usr/bin/env python3
"""G1221 exact source-only SELinux FD bridge regression gate."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RULE = ROOT / "vendor/lt013g_g1221_modem_watchdog_fd.te"
WATCHDOG = ROOT.parent.parent / "lt013g/radio/t311-modem-watchdog.sh"
OLD = ROOT / "vendor/lt013g_modem_watchdog.te"
EXPECTED = "allow system_server lt013g_modem_watchdog:fd use;"

def check(text, watchdog, base):
    rules = [line.strip() for line in text.splitlines()
             if line.strip() and not line.lstrip().startswith("#")]
    if rules != [EXPECTED]:
        raise AssertionError("G1221 requires only exact system_server watchdog fd use")
    checks = [
        'no_active_modem_session()',
        'airplane="$(settings get global airplane_mode_on 2>/dev/null)" || return 1',
        '[ "$airplane" = 0 ] || return 1',
        'sim="$(getprop gsm.sim.state)" || return 1',
        'no_active_modem_session || return 0',
        'claim_recovery_once',
    ]
    if any(s not in watchdog for s in checks):
        raise AssertionError("G1221 lost original fail-closed modem guards")
    if "permissive lt013g_modem_watchdog;" in base:
        raise AssertionError("G1221 broad permissive domain detected")
    if EXPECTED in base:
        raise AssertionError("G1221 duplicate allow rule detected")

def main():
    data, watchdog, base = RULE.read_text(), WATCHDOG.read_text(), OLD.read_text()
    check(data, watchdog, base)
    mutants = [
        EXPECTED.replace(":fd use", ":file read"),
        EXPECTED.replace("system_server", "domain"),
        EXPECTED.replace("lt013g_modem_watchdog", "rild"),
        EXPECTED.replace(" use", " { use read }"),
        EXPECTED + "\npermissive lt013g_modem_watchdog;",
        EXPECTED + "\nallow system_server sysfs:file write;",
    ]
    for unsafe in mutants:
        try:
            check(unsafe, watchdog, base)
        except AssertionError:
            continue
        raise AssertionError("G1221 unsafe mutation accepted")
    print("PASS G1221_EXACT_WATCHDOG_FD_USE_AND_6_FAILCLOSED_POLICY_MUTANTS")
    print("G1221_SOURCE_ONLY_NO_MODEM_GPS_SIM_AUDIO_MEDIA_OR_FLASH")

if __name__ == "__main__":
    main()
