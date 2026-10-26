# G588: physical Android11 postflash USB and TLS ADB acceptance

Run from USB ADB as shell UID; read-only, no network authorization.
Excludes SSID, BSSID, ADB keys and personal data from printed report.

Distinguish USB Gadget HAL current_functions=MTP and configured=true,
MTP server/storage event, and actual PC Dolphin file transfer. Only the
last fully accepts desktop MTP. Android11 sys.usb.state may differ from
Gadget HAL service state; record both rather than silently editing it.

A zero trusted AP count plus system wireless debugging confirmation UI
explains why adb_wifi_enabled resets to zero. Do not bypass trust dialog
by editing keystore or silently fall back to plaintext ADB.
screen_unlocked_functions=MTP is a user preference, not proof it
persists through reboot until physically checked.
