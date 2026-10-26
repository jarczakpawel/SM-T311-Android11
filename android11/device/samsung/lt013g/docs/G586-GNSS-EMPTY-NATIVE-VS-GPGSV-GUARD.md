# G586: avoid empty native GPS SV blanking fresh NMEA snapshot

20 October 2026, Samsung SM-T311 BCM4752, indoor desk.

Evidence from actual running G551-era Android:
- Existing G348 GNSS_SOURCE log shows native legacy gps_sv 130,
  modern native gnss_sv 0, NMEA 2083, GPGSV 384, GLGSV 0, GQGSV 0.
- NMEA decoder reported 11 then 12 visible GPS GSV entries.
- Android HIDL 1.0 hardware/interfaces/gnss/1.0/default/Gnss.cpp
  routes both legacy gpsSvStatusCb and modern gnssSvStatusCb to
  Android framework gnssSvStatusCb.
- Previous G585 70s physical data max12 slots but only max3 with
  positive C/N0, max11dBHz; no fix expected indoors.
- Some zero then 12 alternation might stem from empty legacy native
  callbacks, but exact individual native count was NOT logged.
  The guard fixes only this one condition, not any other cause.

New gps_proxy.c only suppresses legacy native GPS SV count0 if a
checksum-verified, complete, nonempty GPS NMEA GSV snapshot exists
within original 4000ms freshness window. Nonempty native updates,
expired/absent GSV native-only behavior, and all NMEA reporting remain
unchanged, as does prior multi-constellation precedence. No extra
satellites, synthetic C/N0 or fixes, no alteration to BCM4752 power,
UART, AGPS or position.

Tests: exact source-extracted C guard GCC16 -Werror, ASan+UBSan 10
paths, including threshold/stale/future/no-time/no-NMEA/native count.
Full Android ARM32 GNSS HAL build m gps.default.lt013g.v195 PASS.

Deployment: code and host binary ONLY. Existing G573 system slices
were generated BEFORE G586 and do not contain this patch. Prior
SYSTEM p20 write was blocked by tool safeguards and was NOT bypassed.
G586 must be merged into one coherent new SYSTEM release and tested
for callback quality; there is no physical acceptance until installed.
