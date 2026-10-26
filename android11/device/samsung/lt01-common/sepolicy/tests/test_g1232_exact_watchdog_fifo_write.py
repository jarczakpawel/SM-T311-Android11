#!/usr/bin/env python3
"""G1232 exact-only system_server watchdog FIFO write policy source safety."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RULE = ROOT / "vendor/lt013g_g1232_modem_watchdog_fifo_write.te"
OLD = ROOT / "vendor/lt013g_g1221_modem_watchdog_fd.te"
WATCH = ROOT.parent.parent / "lt013g/radio/t311-modem-watchdog.sh"
EXPECTED = "allow system_server lt013g_modem_watchdog:fifo_file write;"
G1221 = "allow system_server lt013g_modem_watchdog:fd use;"

def check(candidate, earlier, watchdog):
    rules = [x.strip() for x in candidate.splitlines()
             if x.strip() and not x.lstrip().startswith("#")]
    if rules != [EXPECTED]:
        raise AssertionError("exactly one low-privilege watchdog FIFO writer is required")
    previous = [x.strip() for x in earlier.splitlines()
                if x.strip() and not x.lstrip().startswith("#")]
    if previous != [G1221]:
        raise AssertionError("previous accepted G1221 FD-use rule changed")
    tokens = ['no_active_modem_session()', 'claim_recovery_once',
              'airplane="$(settings get global airplane_mode_on 2>/dev/null)" || return 1',
              '[ "$airplane" = 0 ] || return 1',
              'sim="$(getprop gsm.sim.state)" || return 1']
    if not all(x in watchdog for x in tokens):
        raise AssertionError("watchdog missing modem safety gates")
def main():
    candidate, earlier, watch = RULE.read_text(), OLD.read_text(), WATCH.read_text()
    check(candidate, earlier, watch)
    for bad in [EXPECTED.replace(":fifo_file", ":file"),
                EXPECTED.replace("system_server", "domain"),
                EXPECTED.replace("lt013g_modem_watchdog", "rild"),
                EXPECTED.replace(" write", " { write execute }"),
                EXPECTED + "\npermissive lt013g_modem_watchdog;",
                EXPECTED + "\nallow system_server sysfs:file write;"]:
        try: check(bad, earlier, watch)
        except AssertionError: continue
        raise AssertionError("G1232 accepted dangerous policy scope change")
    print("PASS G1232_SINGLE_SYSTEM_SERVER_WATCHDOG_FIFO_WRITE_ONLY_AND_6_MUTANTS")
    print("G1232_SOURCE_ONLY_NO_MODEM_GPS_SIM_AUDIO_MEDIA_OR_FLASH")
if __name__ == "__main__":
    main()
