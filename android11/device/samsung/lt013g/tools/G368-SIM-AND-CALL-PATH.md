# G368: SM-T311 Samsung RIL safety, voice call path, and Plus SIM

## Deployed SIM protection (Android 11, 2026-10-16)

Original Android 7.1.2 donor binary (SHA256 libsec-ril `432658fd...`) was actually inspected. Samsung's compiled Pin/Puk/PinChange constructors copy fixed widths: 9 bytes for PIN/PUK and 39 for AID. Directly installing the Android 7.1.2 `libril.so` in Android 11 would break the API25/API30 boundary. G350 contains only the original OEM mutex compatibility patch; Samsung OEM libsec-ril in the running tablet remains SHA256 `0a6739936a0981f6541012f55948f8e5b70f2c17f64c25bd5dabbb8e3f1452e9`.

The first protection G362 remains: validates lengths and decimal codes, refuses empty/malformed PIN1/PIN2/PUK1/PUK2/CHANGE_PIN1/CHANGE_PIN2 BEFORE Samsung OEM sees data, pads 9-byte and 39-byte buffers, SystemUI Keyguard skips empty PIN/PUK retry queries. Earlier original Orange card incident followed >=6 empty PIN requests and then PUK requests. No PIN or PUK is ever automatically guessed or sent in G368.

New incremental HARDWARE/RIL commit `dd216b8` adds a second layer: an atomic per-process, per-request **one attempt only** fuse for six classes of PIN/PUK/change requests. The guard is reached only after the G362 formatting and fixed-width protection. Further attempts are rejected with INVALID_ARGUMENTS and logged with the request ID only, no code/value. The fuse prevents a runaway framework or device service from exhausting attempts rapidly in one RIL process; it DOES NOT guarantee no SIM can ever be blocked, cannot prevent invalid manual first entry or attempts across reboots/external devices. It also means a legitimate second manual attempt requires RIL daemon restart; this is a deliberate conservative safety trade-off.

Real extracted C++ helper tested with host AddressSanitizer + UBSan: valid/invalid PIN, PUK, change cases; six counters; 100 duplicate requests each rejected; 48 concurrent claim attempts result in exactly 1 success. Test source device/samsung/lt013g/tools/test_g368_sim_attempt_guard.py, executes without any SIM/modem interaction. ARM32 Android11 libril build job da9fef02c779 exit0.

Signed TWRP narrow package:
`/home/debian/Projekty/samsung/artifacts/SM-T311-G368-SIM-ONCE-20261016/SM-T311-G368-SIM-ONCE-TWRP-SIGNED.zip`
SHA256 `ddcf018b493bb561afeeffe03a2b5bfd242dcc626361c450aa22c09ccf934feb`.
Preflight exact old G362 RIL SHA, OEM G350, G365 SystemUI three hashes, BOOT G333 and SEPolicy. Backup of old `libril.so` to `/cache/SM-T311-G368-PRE-RESTORE/libril.so` SHA `e259974...`. Only `/vendor/lib/libril.so` is replaced, no OEM binary, kernel, BOOT, EFS, recovery, GPS, baseband firmware or data rewritten. On real TWRP3.1.1 install script SUCCESS and post-files SHA check PASS.

G368 current live `/vendor/lib/libril.so` SHA `719750b0f544238c0986ecbbc8c7d4b59b822f00c7ba1c883809b1b54fcee491`; postboot Android11 `sys.boot_completed=1`, `init.svc.ril-daemon=running`, `gsm.sim.state=LOADED`, `gsm.network.type=UMTS`, SELinux globally `Enforcing`, SystemUI active. No PIN/PUK transactions, no PASSWORD_INCORRECT, no crash. Actual SIM first call and incoming acceptance remains pending, the radio update is a safety fuse not a voice fix.

## Incoming/outgoing calls: evidence, not speculation

Postregistration Plus user personally attempted outgoing first activation call and incoming calls to tablet. Passive Android logs from outgoing at 17:15:
- GsmCdmaPhone selected non-IMS 3G CS path.
- Android RIL requested DIAL; OEM returned SUCCESS ~270ms.
- GET_CURRENT_CALLS correctly decoded `index1,DIALING` then `ALERTING`; Android Telecom and the default Dialer displayed the outgoing InCallActivity. No CONNECTED/ACTIVE result in that observed call; approx 21 sec later list becomes empty.
- LastCallFailCause OEM returned a malformed NULL with apparently RIL_E_SUCCESS, Android 11 HIDL correctly reported INVALID_RESPONSE. Its log text mistakenly says getCurrentCallsResponse; this is from getLastCallFailCauseResponse. This occurs after disconnect, NOT proof it caused inbound no-UI.
- There are three UNSOL_RESPONSE_CALL_STATE_CHANGED but during the outgoing call. No proven incoming `GET_CURRENT_CALLS state=INCOMING/WAITING` in inspected capture.
- Several Samsung-specific vendor unsolicited IDs `11003,11010,11017,11022` unsupported by generic AOSP HIDL. They are NOT mapped to `CALL_RING` because their semantics/payload ABI are not yet established. Doing so blindly would flood call events or corrupt the call state.
- Two multipart SMS segments were received by radio in that time window, but *not* complete multipart assemblies. Outbound SMS ACK still requires its own test; never fake ACK or resend a possibly delivered message.

Original Android 7.1.2 OEM symbols verified from stock binary: CallManager::OnCallIncoming, ::OnCallStatusChanged, ::OnGetCurrentCallsDone, ::DoDial, ::DoLastCallFailCause. The Samsung modem and vendor callbacks for call status still need a real fresh incoming-call test to establish whether (1) Plus routes mobile-terminated call to modem/CS domain, (2) Samsung OEM emits 1001/1018 for INCOMING, (3) Android HIDL maps the returned RIL_Call as INCOMING, and (4) Dialer presents the UI. Merely hearing a ringing tone on the calling handset proves none of (2)-(4).

The default Dialer is `com.android.dialer`, Telecom records normal InCallService binding without crash, and outgoing in-call Activity displayed; there is no reason at present to blindly reinstall the Dialer.

Kernel real read-only transport counters available in `/sys/class/misc/umts_ipc0/fmt_stats`, tracking RX URBs/bytes/frames, parser errors, TX successes/errors and modem suspend/resume. Baseline captured after G368 boot: RX URBs82, RX bytes3817, frames117, TX OK78; RX errors0, parse errors0, TX errors0. After ONE user-initiated fresh incoming call compare counter deltas and filtered Android RIL logs; no need to capture caller number or raw PDU, no automatic dialing. This is the next REQUIRED physical acceptance before claiming voice fixed.

## Plus 3G lifetime and registration

As of Oct 16 2026, Plus currently reports 3G UMTS CS and PS registered in the live Android registry. Plus published plans to START phased shutdown of 3G in December 2026, and old SM-T311 has no LTE/VoLTE. Hardware cannot gain LTE via a ROM change. This is separate from the current missing inbound ring UI. Official Plus policy may treat *attempted* first call as service activation, but only Plus can confirm provisioning status on their side.

## Release blockers not changed

G367 full targetfiles 44/45 byte match with live before G368, missing0, but now original G367 RIL has old hash and is not even coherent with new G368 live without rebuild. Further blocks: candidate kernel differs from tested G333 despite SAME source Git revision 4c02b9c9b, four SELinux permissive domains su/rild/lt013g_gps_legacy/backuptool, OEM firmware redistribution rights. DO NOT flash G367 whole-ROM candidate.

## Next

1. User manually attempts ONE call to Plus tablet from another phone; explicitly notes whether voice network rings while tablet displays nothing.
2. After that event, read fresh `fmt_stats` and sanitized radio/Telecom logs, distinguish mobile-terminated call reach CP, OEM indications and Android UI; fix the real broken layer with source-level stock ABI evidence.
3. Real user-observed activation status cannot be inferred solely from READY/LOADED/RIL DIAL success.
4. Neither model nor tooling automatically issues PIN/PUK, performs test calls/SMS or resets modem.
