# G594 — silent read-only health audit

Only checks current boot, kernel, SELinux, fingerprint parity, presence
of radio and GPS services, USB/MTP/secure ADB status, CPU and memory and
counts of selected kernel-crash/warning lines. Never reads or saves user
location, SSID, BSSID, IMEI, call logs, phone numbers, raw radio packets,
ADB keys, photographs or audio. It never starts any app/service or toggles
USB/Wi-Fi. It does not request camera, microphone, GNSS or notifications.

Host static contract:
`python3 tools/g594-silent-acceptance/test_silent_audit.py`.

Execution on Android as shell UID:
`sh /data/local/tmp/g594_readonly_sm_t311.sh`.

Interpretation: two DWC2 endpoint-stop warnings during a gadget restart,
followed by success and USB CONFIGURED, alone are not proof of a live MTP
failure. Do not change timing or kernel power based on warnings alone.
USB MTP on PC and encrypted Wi-Fi ADB TLS after explicit trusted-network
approval still need their own acceptance. The script cannot test them.
