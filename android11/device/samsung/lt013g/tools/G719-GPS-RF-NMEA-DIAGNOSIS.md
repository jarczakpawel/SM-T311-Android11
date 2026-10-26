> **CURRENT G720 CORRECTION:** G719 all-blank C/N0 was from G718's historical logcat, NOT proof the hardware can no longer track satellites. With SAME G717 HAL at SAME window G720 GPSTest has GPS C/N0 peak 23 dB-Hz + GLONASS 24, no fix after 2m42s, app stopped. Real vendor LTO bridge transfer `bytes=73775 rc=0` occurred in G718, although GNSS internal usage remains unverified. The source `tools/g719-readonly-nmea-health.sh` now optionally accepts 1–3600 `recent_seconds` to filter logs by epoch. On-device updated script SHA `9a26bec9ac24b610e9dbe9054fd490659b3a7fc2ff73f78b5fd41f529baa3989`; 900s returned 182 positive C/N0, max24; idle 5s correctly gives INSUFFICIENT. See `tools/G720-GPS-WINDOW-INTERMITTENT-RF.md` and `tools/G720-PHYSICAL-GNSS-RF-COMPARE.json`. No new GNSS HAL/firmware/system change.

# G719 — NMEA RF diagnostic for installed G717 GPS

**No new firmware, HAL, modem, kernel or Android system changes.**

- Added `tools/g719_gps_nmea_health.awk` — parses ONLY GPGSV/GLGSV/GPGSA/GPGGA/GPRMC and related messages directly from `GpsOutputNmea` logcat; prints only fix flags, UTC time and aggregates of C/N0, never coordinates.
- Added `tools/g719-readonly-nmea-health.sh` — physical read-only ADB shell harness which does not start GNSS, reset aiding data, clear logcat or use root. It outputs from the existing **buffer**, not a new sample.
- Added `tools/test_g719_gps_nmea_health.py` — actual host AWK subprocess with five scenarios, **5/5 PASS**, and explicit privacy gate that coordinates in input never appear in output.
- Uploaded both read-only diagnostics to physical tablet under `/data/local/tmp/`, device SHA256 exactly matches source:
  - AWK `59b968d503029340d152ab1f61514ca423e1f400b52e2987329a844060966434`
  - shell `1f33976d1a5dd437d34916f3bce68925fa7cfc7c58682fad33ac3abbfe603122`.
- Actual device result in `tools/G719-GPS-RF-HEALTH-20261021.json`: archived G718 logs 135 GSV, 495 PRN entries all 495 SNR fields blank, zero positive C/N0, 135 GSA mode1, 44 GGA quality0/00, 45 RMC invalid V, still no fix. 11 satellites described as *in view* is **not evidence of receiving/tracking 11 signals**. Under NMEA GSV specification blank SNR means not tracked. Receiver date/time appears compatible with Android UTC but does not prove successfully injected time, ephemeris or valid position.
- **G719 corrects G718's wrong inference from `LocationManager` stats:** Android11 `GnssMetrics.logSvStatus` increments `mNumSvStatus` only for satellites reporting `HAS_CARRIER_FREQUENCY`, while legacy BCM4752 HIDL1.0 does not populate it. Zero framework metric alone does not prove missing status callback or broken JNI/HIDL.
- Historical G385 on same tablet measured two satellites >=25dB-Hz (max32), but different source/time/environment. Further controlled outside test needed before hardware/antenna diagnosis.
- Stock source contract: BC4752 via UART `ttySAC1`, enable GPL2(2), PMIC 32768Hz LPO, stock gnss daemon sends NMEA. The stock daemon/serial bridge is alive. This does NOT prove RF input reception. AGPS LTO `debug.lt013g.gps.v357.lto` is enabled and framework got XTRA request, but no proof of accepted proprietary LTO data. Do not inject arbitrary modern PSDS formats or attempt a speculative decompile/reimplementation.

### Repeat on a user-initiated GPS session under open sky

```sh
adb push tools/g719_gps_nmea_health.awk /data/local/tmp/g719_gps_nmea_health.awk
adb push tools/g719-readonly-nmea-health.sh /data/local/tmp/g719-readonly-nmea-health.sh
# Start a normal GPS test for a representative period using GPSTest.
adb shell sh /data/local/tmp/g719-readonly-nmea-health.sh
```

Logcat is a bounded ring buffer; the summary is not necessarily fresh unless GPSTest was just used. It reads no coordinates. Compare C/N0 positive signals and quality/validity rather than a framework KPI with unavailable carrier frequency.

Next if outdoor C/N0 also blank: antenna/connectors/LPO/power/legacy gpsd RF; if C/N0 positive and no fix: time, compatible LTO, ephemeris, SUPl and actual GGA/RMC nav state. Neither RF failure nor firmware defect is proven yet. Current tablet after G718 is GPS HAL G717 installed, Android G650 and GPS provider idle.
