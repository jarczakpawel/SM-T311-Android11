# G583 GNSS physical retest (2026-10-20)

Tested on SM-T311 on a desk indoors with the existing previously verified
70-second foreground GNSS status probe G385. The source-built APK was
temporarily installed, fine and background location permission explicitly
granted, probe started and completed, then the APK was uninstalled.

Observed GNSS_REQUEST_STARTED and GNSS_STARTED.
GNSS_FINAL: 115 satellite status events, 0 location fixes,
maximum 11 satellites listed, maximum C/N0 10 dB-Hz, zero signals
at or above 25 dB-Hz. Some 11-slot satellite reports had zero C/N0,
and numerous empty (0 satellites) status callbacks alternated with them.

After probe mStarted=false, GPS legacy HAL running, Android boot_completed1,
USB ADB connected. No coordinates, SVID, raw NMEA or user data recorded.

Previous G385 (Oct16) 70s run had 71 callbacks, max7 visible, max2 signals
>=25dB-Hz, max C/N0 32dB-Hz, zero fix. This is a large observed difference
but tests were on different dates with uncontrolled RF conditions and kernel
versions. NOT PROOF of a Linux7.3 kernel regression. Current 11 slots are
not equivalent to 11 strong RF signals. Indoor fix NOT EXPECTED.

Possible software explanation under investigation: gps-v195/gps_proxy.c
merges NMEA GP/GL GSV status while separately forwarding native GNSS_SV
callbacks, potentially with empty native frames. Compare GSV carrier
strength and native callbacks, UART/AGPS before changing voltage or
inventing GPS coordinates/fixes. G573 Android SYSTEM still NOT installed.

## G585 precise positive C/N0 experiment, indoor desk (Oct20)

New GnssProbeService.java Git 17585eb, full Android ARM32 APK build PASS
SHA256 345508e5835b6b1a31e5d0efc4d1d980b72e440bab96fccd09966814eb515303.
Source now counts satellite slots separately from positive carrier signals.
Second physical 70-second GNSS run: 90 callbacks, max12 SV entries but
at most 3 with positive C/N0; max11dB-Hz, none >=25, 0 fixes.
Returned to idle, APK removed, USB ADB remained live.
The 12 entries are not 12 acquired RF satellites, and zero fix indoors
is NOT a failure. Compared with Oct16 max C/N0 32, cause still uncertain,
may depend on environment, antenna, GNSS native vs NMEA status or assistance.
Do not synthesize satellite signals or position fixes.

G585 also repaired G323 NMEA callback source-extraction fixture that failed
to compile after G346 counters were added; Git e9b648d, GCC16 4/4 PASS.
