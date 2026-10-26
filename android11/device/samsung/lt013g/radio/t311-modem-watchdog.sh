#!/vendor/bin/sh
# SM-T311 XMM6262 bounded cold-boot recovery.
# Never touch PIN, SIM, EFS or NV. Do not interfere with ONLINE modem.
PATH=/vendor/bin:/system/bin:/system/xbin
DEV=/sys/devices/platform/xmm6262-modem-control/state
DIAG_ROOT=/cache/smt311-radio-diagnostics
tag=t311-modem-watchdog

say() { log -t "$tag" -p i "$*" 2>/dev/null; }
# Passive HSIC stage: no USB reset, SIM access or power-state modifications.
usb_stage() {
    v=/sys/bus/usb/devices/1-2/idVendor
    p=/sys/bus/usb/devices/1-2/idProduct
    [ -r "$v" ] && [ -r "$p" ] || { echo UNENUMERATED; return; }
    vendor="$(cat "$v" 2>/dev/null)"
    product="$(cat "$p" 2>/dev/null)"
    case "$vendor:$product" in
        058b:0041) echo BOOTROM ;;
        1519:0020) echo RUNTIME ;;
        *) echo OTHER_USB ;;
    esac
}
healthy() {
    [ "$(getprop ril.cbd.boot_done)" = "1" ] || return 1
    [ -r "$DEV" ] || return 1
    case "$(cat "$DEV" 2>/dev/null)" in
        state=ONLINE\ *)
            # Logical CP ONLINE can outlive a failed HSIC USB enumeration.
            # Never declare success until the real runtime USB identity is
            # visible on port 2. Read-only sysfs check (no reset commands).
            [ "$(usb_stage)" = RUNTIME ]
            ;;
        *) return 1 ;;
    esac
}
wait_healthy() {
    count="$1"
    i=0
    while [ "$i" -lt "$count" ]; do
        if healthy; then
            return 0
        fi
        sleep 10
        i=$((i + 1))
    done
    healthy
}

# Preserve one small failure trace per Android boot, inside root-only /cache.
# This collects only kernel USB/HSIC traces and state flags; it does not read
# subscriber identifiers, radio logcat, SIM files, EFS, NV or modem firmware.
# Capped to 8 MiB total across boots. Never interrupt radio recovery if this
# optional capture fails (e.g. /cache not mounted or nearly full).
snapshot_failure_once() {
    why="$1"
    (
        umask 077
        ROOT="$DIAG_ROOT"
        [ -d /cache ] && [ ! -L "$ROOT" ] || exit 0
        mkdir -p "$ROOT" 2>/dev/null || exit 0
        chmod 0700 "$ROOT" 2>/dev/null || exit 0
        [ ! -L "$ROOT" ] || exit 0
        used="$(du -sk "$ROOT" 2>/dev/null | cut -f1)"
        case "$used" in ''|*[!0-9]*) exit 0;; esac
        [ "$used" -lt 8192 ] || exit 0
        bootid="$(cat /proc/sys/kernel/random/boot_id 2>/dev/null | tr -cd '0123456789abcdef-')"
        case "$bootid" in ''|*[!0-9a-f-]*) exit 0;; esac
        dir="$ROOT/$bootid"
        [ ! -L "$dir" ] || exit 0
        mkdir -p "$dir" 2>/dev/null || exit 0
        chmod 0700 "$dir" 2>/dev/null || exit 0
        # claim_recovery_once may have created the boot-ID directory.
        # Only one snapshot per boot, regardless of repeated invocations.
        [ ! -e "$dir/status.txt" ] || exit 0
        {
            printf 'reason=%s\n' "$why"
            printf 'uptime_seconds='; cut -d' ' -f1 /proc/uptime
            printf 'modem_state='; if healthy; then echo ONLINE; else echo UNHEALTHY; fi
            printf 'usb_stage='; usb_stage
            printf 'radio_hal='; if radio_hal_registered; then echo PRESENT; else echo ABSENT; fi
            printf 'cbd_service='; getprop init.svc.cpboot-daemon
            printf 'ril_service='; getprop init.svc.ril-daemon
            printf 'sim_state='; getprop gsm.sim.state
        } > "$dir/status.txt"
        lsusb > "$dir/usb.txt" 2>/dev/null
        dmesg 2>/dev/null | grep -E 'XMM|T311_|xmm6262|ehci|usb 1-2' |
            tail -n 400 > "$dir/kernel-usb.txt"
        sys="${DEV%/*}"
        # A sysfs show() attribute is limited to PAGE_SIZE (4096 bytes).
        # The XMM handoff recorder holds up to 256 entries (~15 KiB); the
        # sysfs trace silently loses most of the record during boot faults.
        # The existing proc seq_file exports the entire captured ring.
        full=/proc/tab3_xmm6262_handoff_trace
        if [ -r "$full" ]; then
            head -c 65536 "$full" > "$dir/handoff_trace.txt" 2>/dev/null
        fi
        if [ -s "$dir/handoff_trace.txt" ]; then
            printf 'handoff_export=proc_full\n' >> "$dir/status.txt"
        elif [ -r "$sys/handoff_trace" ]; then
            head -c 16384 "$sys/handoff_trace" > "$dir/handoff_trace.txt" 2>/dev/null
            printf 'handoff_export=sysfs_fallback_limited\n' >> "$dir/status.txt"
        else
            printf 'handoff_export=unavailable\n' >> "$dir/status.txt"
        fi
        for name in diag_snapshots link_trace; do
            [ -r "$sys/$name" ] || continue
            head -c 16384 "$sys/$name" > "$dir/$name.txt" 2>/dev/null
        done
    ) >/dev/null 2>&1 || true
}

# Guard any destructive recovery with an atomic claim shared by repeated
# invocations of this oneshot init service. The claim is boot-ID scoped, so
# manual init restarts and accidental duplicate launches cannot reset the CP
# twice on one Android boot. Fail closed when /cache is not writable.
claim_recovery_once() {
    (
        umask 077
        ROOT="$DIAG_ROOT"
        [ -d /cache ] && [ ! -L "$ROOT" ] || exit 1
        mkdir -p "$ROOT" 2>/dev/null || exit 1
        chmod 0700 "$ROOT" 2>/dev/null || exit 1
        bootid="$(cat /proc/sys/kernel/random/boot_id 2>/dev/null | tr -cd '0123456789abcdef-')"
        case "$bootid" in ''|*[!0-9a-f-]*) exit 1;; esac
        dir="$ROOT/$bootid"
        [ ! -L "$dir" ] || exit 1
        mkdir -p "$dir" 2>/dev/null || exit 1
        chmod 0700 "$dir" 2>/dev/null || exit 1
        # mkdir is atomic even with two independent watchdog instances.
        mkdir "$dir/recovery-claimed" 2>/dev/null || exit 1
    ) >/dev/null 2>&1
}

# Confirm exact CBD executable AND kernel process birth timestamp. A PID can
# be recycled during the final safety check, so compare identity again before
# the exceptional SIGKILL. /proc/<pid>/stat field 22 is the start time in ticks.
cbd_pid_identity() {
    cbd_pid="$1"
    case "$cbd_pid" in ''|*[!0-9]*) return 1;; esac
    cbd_exe="$(readlink "/proc/$cbd_pid/exe" 2>/dev/null)" || return 1
    case "$cbd_exe" in
        /vendor/bin/t311-cbd|/system/vendor/bin/t311-cbd) ;;
        *) return 1 ;;
    esac
    cbd_stat="$(cat "/proc/$cbd_pid/stat" 2>/dev/null)" || return 1
    case "$cbd_stat" in
        "$cbd_pid (t311-cbd) "*) ;;
        *) return 1 ;;
    esac
    cbd_fields="${cbd_stat#"$cbd_pid (t311-cbd) "}"
    # Fields after PID/comm begin with kernel field 3. Field 22 = token 20.
    set -- $cbd_fields
    [ "$#" -ge 20 ] || return 1
    shift 19
    case "$1" in ''|*[!0-9]*) return 1;; esac
    printf '%s|%s\n' "$cbd_exe" "$1"
}

# A denied /system/bin/ip exec must NEVER look like a disconnected modem.
# The vendor native helper returns 0 only when rmnet1 is present and no
# numeric rmnet interface holds an IPv4/global IPv6 address. Any inventory
# error or active cellular address blocks recovery.
mobile_data_inactive() {
    [ -x /vendor/bin/t311-mobile-session-probe ] || return 1
    # A stalled netlink address dump cannot block this watchdog forever.
    # timeout, SIGKILL, inventory errors and active IP all fail closed.
    timeout -k 2 3 /vendor/bin/t311-mobile-session-probe rmnet1 >/dev/null 2>&1
}

# Recheck immediately before a restart or kill, not only at the start of
# the 30-second grace period. A new valid SIM or IPv4 route means another
# actor has recovered the radio in the meantime.
no_active_modem_session() {
    airplane="$(settings get global airplane_mode_on 2>/dev/null)" || return 1
    [ "$airplane" = 0 ] || return 1
    sim="$(getprop gsm.sim.state)" || return 1
    [ -z "$sim" ] || return 1
    mobile_data_inactive || return 1
    return 0
}

# When XMM6262 comes ONLINE very late, native Samsung rild may miss its
# BOOTING(0x3) state and never publish IRadio/slot1 despite ONLINE(0x4).
# Never restart a healthy radio; only recover a *missing* HAL while there is
# no SIM state, no active mobile session and airplane mode is off.
radio_hal_registered() {
    # HIDL service introspection may itself block when a legacy RIL dies.
    # Bound its child process; a timeout is treated as NOT healthy rather
    # than hanging the only recovery opportunity indefinitely.
    [ -x /vendor/bin/t311-radio-hal-probe ] || return 0
    timeout -k 2 3 /vendor/bin/t311-radio-hal-probe >/dev/null 2>&1
    result=$?
    [ "$result" = 0 ] && return 0
    [ "$result" = 1 ] || return 0
    return 1
}
radio_hal_confirmed_present() {
    [ -x /vendor/bin/t311-radio-hal-probe ] || return 1
    timeout -k 2 3 /vendor/bin/t311-radio-hal-probe >/dev/null 2>&1
}
final_recovery_guard() {
    case "$1" in
        missing_hal)
            healthy && ! radio_hal_registered && no_active_modem_session
            ;;
        empty_sim)
            healthy && radio_hal_confirmed_present &&
                no_active_modem_session
            ;;
        offline)
            ! healthy && no_active_modem_session
            ;;
        *) return 1 ;;
    esac
}
hal_missing_safe_to_recover() {
    healthy || return 1
    [ "$(getprop sys.boot_completed)" = 1 ] || return 1
    [ "$(getprop init.svc.ril-daemon)" = running ] || return 1
    [ "$(getprop init.svc.cpboot-daemon)" = running ] || return 1
    [ "$(getprop gsm.sim.state)" = '' ] || return 1
    [ "$(settings get global airplane_mode_on 2>/dev/null)" = 0 ] || return 1
    mobile_data_inactive || return 1
    radio_hal_registered && return 1
    return 0
}
recover_missing_radio_hal_once() {
    # Real boot recovery must be bounded and one-shot: no boot loop,
    # modem reset GPIO writes, SIM PIN retries or EFS/NV modification.
    i=0
    while [ "$i" -lt 9 ]; do
        radio_hal_registered && return 0
        [ -n "$(getprop gsm.sim.state)" ] && return 0
        healthy || return 0
        sleep 10
        i=$((i + 1))
    done
    hal_missing_safe_to_recover || return 0
    no_active_modem_session || return 0
    claim_recovery_once || { say "recovery already claimed or /cache inaccessible"; return 0; }
    say "ONLINE modem but radio HAL absent after 90s: one bounded CBD restart"
    snapshot_failure_once online_missing_radio_hal
    final_recovery_guard missing_hal || return 0
    setprop ctl.restart cpboot-daemon
    sleep 30
    [ "$(getprop init.svc.cpboot-daemon)" = stopping ] || return 0
    [ "$(getprop gsm.sim.state)" = '' ] || return 0
    radio_hal_registered && return 0
    # Exact executable and unique PID required. Some legacy t311-cbd builds
    # ignore SIGTERM and block init's requested restart in 'stopping'.
    pid="$(pidof t311-cbd 2>/dev/null)"
    case "$pid" in
        ''|*[!0-9]*) return 0 ;;
    esac
    cbd_identity="$(cbd_pid_identity "$pid")" || return 0
    if [ "$(getprop init.svc.cpboot-daemon)" = stopping ] &&
       no_active_modem_session &&
       ! radio_hal_registered &&
       [ "$(cbd_pid_identity "$pid")" = "$cbd_identity" ] &&
       [ -x /vendor/bin/t311-cbd-pidfd-signal ]; then
        # pidfd pins the actual process; no unsafe kill(pid) fallback.
        /vendor/bin/t311-cbd-pidfd-signal "$pid" "$cbd_identity" || return 0
        say "one pidfd SIGKILL for verified stuck t311-cbd PID=$pid"
    fi
    return 0
}

# The radio HAL may register even when the native RIL never publishes ANY
# SIM state following an exceptionally slow USB/HSIC runtime enumeration.
# Reproduced on hardware: 30 PORTSC=1105 reset timeouts, runtime USB ONLINE,
# HAL registered, but SIM state stayed empty for >4 minutes. The old watchdog
# returned immediately because it checked only for a missing HAL. Wait 180s
# to exclude normal SIM initialization. Never act if SIM, cellular data or
# airplane mode changes. Only one atomic boot-scoped recovery is permitted.
recover_empty_sim_with_online_radio_once() {
    i=0
    while [ "$i" -lt 18 ]; do
        [ -n "$(getprop gsm.sim.state)" ] && return 0
        healthy || return 0
        # SettingsProvider may be unavailable until boot completed. Do not
        # abandon SIM observation merely because an early settings read
        # returns "null". All destructive actions below still require
        # boot_completed=1 and a successful airplane/session recheck.
        if [ "$(getprop sys.boot_completed)" = 1 ]; then
            no_active_modem_session || return 0
        fi
        if ! radio_hal_registered; then
            # The HAL itself disappeared; use the existing bounded recovery.
            recover_missing_radio_hal_once
            return 0
        fi
        sleep 10
        i=$((i + 1))
    done
    [ "$(getprop sys.boot_completed)" = 1 ] || return 0
    [ "$(getprop init.svc.cpboot-daemon)" = running ] || return 0
    [ "$(getprop init.svc.ril-daemon)" = running ] || return 0
    no_active_modem_session || return 0
    healthy || return 0
    radio_hal_registered || return 0
    claim_recovery_once || { say "recovery already claimed or /cache inaccessible"; return 0; }
    say "modem/HAL ONLINE but SIM state empty after 180s; one bounded CBD restart"
    snapshot_failure_once online_radio_hal_sim_empty_180s
    final_recovery_guard empty_sim || return 0
    [ "$(getprop init.svc.cpboot-daemon)" = running ] || return 0
    setprop ctl.restart cpboot-daemon
    sleep 30
    # With this failure mode the HIDL HAL can remain registered even though
    # init is stuck stopping an obsolete t311-cbd. A positive HAL alone is
    # therefore NOT proof that native modem state is usable.
    [ "$(getprop init.svc.cpboot-daemon)" = stopping ] || return 0
    no_active_modem_session || return 0
    healthy || return 0
    pid="$(pidof t311-cbd 2>/dev/null)"
    case "$pid" in
        ''|*[!0-9]*) return 0 ;;
    esac
    cbd_identity="$(cbd_pid_identity "$pid")" || return 0
    if [ "$(getprop init.svc.cpboot-daemon)" = stopping ] &&
       no_active_modem_session && healthy &&
       [ "$(cbd_pid_identity "$pid")" = "$cbd_identity" ] &&
       [ -x /vendor/bin/t311-cbd-pidfd-signal ]; then
        /vendor/bin/t311-cbd-pidfd-signal "$pid" "$cbd_identity" || return 0
        say "verified stuck CBD PID=$pid signaled through pidfd"
    fi
    return 0
}

# A prior bounded CBD restart has already been requested, so never start
# another one merely because the modem reached ONLINE before rild registered
# its HAL. Observe radio readiness for 120 seconds and then stop (one-restart
# policy), leaving a clear diagnostic instead of falsely declaring recovery.
wait_radio_hal_after_cbd_restart() {
    i=0
    while [ "$i" -lt 12 ]; do
        if radio_hal_registered || [ -n "$(getprop gsm.sim.state)" ]; then
            say "radio HAL/SIM available after bounded CBD recovery"
            return 0
        fi
        healthy || { say "modem lost ONLINE during HAL grace period"; return 1; }
        sleep 10
        i=$((i + 1))
    done
    if radio_hal_registered || [ -n "$(getprop gsm.sim.state)" ]; then
        say "radio HAL/SIM available at grace-period boundary"
        return 0
    fi
    say "modem ONLINE but radio HAL missing after CBD recovery; no second restart"
    return 1
}

# The stock t311-cbd can take 130+ seconds during a slow cold boot.
if wait_healthy 20; then
    say "modem ONLINE during initial 200s"
    if [ -z "$(getprop gsm.sim.state)" ]; then
        if ! radio_hal_registered; then
            recover_missing_radio_hal_once
        else
            recover_empty_sim_with_online_radio_once
        fi
    fi
    exit 0
fi

# One bounded recovery per Android boot. cpboot-daemon may be stuck in
# init's "stopping" state because legacy t311-cbd ignores SIGTERM.
# Don't disturb an intentionally disabled radio, or a still-configured
# mobile data session: a stale boot_done alone is not enough to take action.
if [ "$(settings get global airplane_mode_on 2>/dev/null)" != "0" ]; then
    say "airplane mode not confirmed off; do not restart modem"
    exit 0
fi
if ! mobile_data_inactive; then
    say "mobile data active or unverified; do not restart modem"
    exit 0
fi
if [ -n "$(getprop gsm.sim.state)" ]; then
    say "SIM has state; do not automatically restart modem"
    exit 0
fi
svc="$(getprop init.svc.cpboot-daemon)"
say "modem offline after 200s, CBD init state=$svc usb_stage=$(usb_stage)"
snapshot_failure_once modem_offline_after_200s
case "$svc" in
    running|restarting|stopping)
        no_active_modem_session || exit 0
        claim_recovery_once || { say "recovery already claimed or /cache inaccessible"; exit 0; }
        ;;
esac
case "$svc" in
    running|restarting)
        final_recovery_guard offline || exit 0
        [ "$(getprop init.svc.cpboot-daemon)" = "$svc" ] || exit 0
        setprop ctl.restart cpboot-daemon
        say "requested one CBD restart"
        ;;
    stopping)
        say "CBD already stopping; allow 30s before considering stuck process"
        ;;
    *)
        say "no safe automatic action for CBD state=$svc"
        exit 0
        ;;
esac

# Allow init to complete the stop/restart without intervention.
if wait_healthy 3; then
    say "modem ONLINE during restart grace period"
    wait_radio_hal_after_cbd_restart || true
    exit 0
fi

# Avoid any kill unless init is STILL stuck stopping, modem offline, and
# precisely one PID resolves to the expected legacy t311-cbd executable.
svc="$(getprop init.svc.cpboot-daemon)"
if [ "$svc" = "stopping" ] && ! healthy; then
    pid="$(pidof t311-cbd 2>/dev/null)"
    case "$pid" in
        ''|*[!0-9]*)
            say "CBD PID missing or ambiguous; no kill"
            ;;
        *)
            cbd_identity="$(cbd_pid_identity "$pid")" || exit 0
            if ! healthy && no_active_modem_session &&
               [ "$(getprop init.svc.cpboot-daemon)" = stopping ] &&
               [ "$(cbd_pid_identity "$pid")" = "$cbd_identity" ] &&
               [ -x /vendor/bin/t311-cbd-pidfd-signal ]; then
                /vendor/bin/t311-cbd-pidfd-signal "$pid" "$cbd_identity" || exit 0
                say "one pidfd SIGKILL to verified offline CBD PID=$pid"
            fi
            ;;
    esac
fi

# Last passive observation. No repeated ctl.restart, SIGKILL, or device reboot.
if wait_healthy 20; then
    say "modem ONLINE after bounded recovery"
    wait_radio_hal_after_cbd_restart || true
else
    say "modem remains offline; automatic recovery exhausted usb_stage=$(usb_stage)"
fi
exit 0
