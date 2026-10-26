#!/usr/bin/env python3
"""G1241: exact-only source policy for Android Settings watchdog callback."""
from pathlib import Path
R = Path(__file__).resolve().parents[1]
NEW = R / "vendor/lt013g_g1241_modem_watchdog_binder_callback.te"
OLD_FD = R / "vendor/lt013g_g1221_modem_watchdog_fd.te"
OLD_FIFO = R / "vendor/lt013g_g1232_modem_watchdog_fifo_write.te"
WATCH = R.parent.parent / "lt013g/radio/t311-modem-watchdog.sh"
CORE = R / "vendor/lt013g_modem_watchdog.te"
LINE = "allow system_server lt013g_modem_watchdog:binder call;"
PREVIOUS = (
    "allow system_server lt013g_modem_watchdog:fd use;",
    "allow system_server lt013g_modem_watchdog:fifo_file write;",
)
def allow_only(s):
    return [x.strip() for x in s.splitlines() if x.strip() and not x.lstrip().startswith("#")]
def gate(candidate, fd, fifo, watchdog, core):
    if allow_only(candidate) != [LINE]:
        raise AssertionError("G1241 needs exactly one narrow reverse Binder call rule")
    if (allow_only(fd),allow_only(fifo)) != ([PREVIOUS[0]],[PREVIOUS[1]]):
        raise AssertionError("G1241 accepted live G1233 policy changed")
    if "binder_call(lt013g_modem_watchdog, system_server)" not in core:
        raise AssertionError("watchdog original source-to-platform Binder contract missing")
    required = [
        'no_active_modem_session()', 'claim_recovery_once',
        'airplane="$(settings get global airplane_mode_on 2>/dev/null)" || return 1',
        '[ "$airplane" = 0 ] || return 1',
        'sim="$(getprop gsm.sim.state)" || return 1'
    ]
    if any(x not in watchdog for x in required):
        raise AssertionError("watchdog bounded safety gates changed")
def main():
    fd,fifo,watch,core=OLD_FD.read_text(),OLD_FIFO.read_text(),WATCH.read_text(),CORE.read_text()
    gate(NEW.read_text(),fd,fifo,watch,core)
    for bad in [
        LINE.replace(":binder",":process"),
        LINE.replace("system_server","domain"),
        LINE.replace("lt013g_modem_watchdog","rild"),
        LINE.replace(" call"," { call transfer }"),
        LINE + "\npermissive lt013g_modem_watchdog;",
        LINE + "\nallow system_server sysfs:file write;",
        LINE + "\nallow system_server lt013g_modem_watchdog:binder transfer;",
    ]:
        try:gate(bad,fd,fifo,watch,core)
        except AssertionError:continue
        raise AssertionError("unsafe mutation was accepted")
    print("PASS G1241_EXACT_ONE_REVERSE_BINDER_CALL_SEVEN_UNSAFE_MUTANTS_REFUSED")
    print("G1241_NO_RADIO_CAMERA_AUDIO_GPS_OR_FLASH")
if __name__=="__main__":
    main()
