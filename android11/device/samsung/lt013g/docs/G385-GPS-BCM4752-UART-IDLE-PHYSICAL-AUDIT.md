# SM-T311 G385 — GPS BCM4752 first, actual UART/power/runtime audit

Date: 2026-10-16. Evidence is **source and physical IDLE enumeration**, NOT satellite/fix acceptance, **no active navigation request**.

## Exact factory SM-T311 3G8 comparator
- `kernel-stock-samsung/arch/arm/configs/tab3_3G8_01_defconfig`: CONFIG_TARGET_TAB3_3G8=y, CONFIG_GPS_BCMxxxxx=y, **CONFIG_GPS_BCM4752=y** and `# CONFIG_GPS_BCM47521 is not set`.
- Selected actual Samsung `arch/arm/mach-exynos/sec_gps_bcm4752.c` controls UART AF2, RX pull-up, TX/CTS/RTS, GPL2(2) GPS_PWR_EN; it does **not** install the sibling BCM47521 geofence hostwake IRQ on shared PMIC GPX1(5).
- `gpio-rev00-tab3.h`: GPA0(4)=GPS_RXD AF2, GPA0(5)=GPS_TXD AF2, GPA0(6)=CTS AF2, GPA0(7)=RTS AF2. GPS_PWR_EN=GPL2(2).
- Linux6.12 exact `arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dts`: `&serial_1`, AF2 GPA0(4..7), hardware RTS/CTS (`uart-has-rtscts`, `samsung,uart-force-hw-flow-control`), RX pull-up, compatible `samsung,tab3-bcm4752`, enable GPIO GPL2(2). `drivers/misc/samsung-tab3-bcm4752.c` gets clock `lpo` enabled at 32768Hz and exposes only the GPIO power node. No wrong-model wake IRQ port.

## Physical G379 runtime — no coordinate capture
Read-only ADB snapshot: `artifacts/SM-T311-G385-GPS-FIRST-20261016/G385-GPS-READONLY-RUNTIME.txt`. The **real** tablet:
- `ro.product.device=lt013g`, `sys.boot_completed=1`, SELinux global `Enforcing`; `init.svc.vendor.lt013g-gps-legacy-v195=running`.
- Three live processes: `android.hardware.gnss@1.0-service`, `lt013g-gps-bridge-host`, `gpsd`. Device `/dev/ttySAC1` present as serial character device. HAL's private Binder node proven in earlier G379 source/live test and preserved; not repeated here.
- Read-only `/sys/class/sec/gps/GPS_PWR_EN/value` = `0`, Android `dumpsys location` says `mStarted=false`, `GNSS_KPI Total number of sv status messages processed=0`. **When Android is not requesting navigation fixes, zero satellite SV messages is NOT a GPS hardware failure**. Do not overwrite the GPS HAL for this idle result.
- Current location setting on Android is enabled (secure location_mode=3), but no location request, NMEA trace, latitude, longitude, cell/SIM data or real user position is read/persisted. No gps power toggle, no assisting reset, no NTP/server modifications, no user location settings changed.
- G382 strict GPS Binder SELinux type/allow was successfully built and signed, SHA`1c969cae...`, **staged not installed**. Do not imply G382 has passed physical policy validation.
- Follow-up diagnostic for actual SV/fix should start a *user-permission respecting* GNSS listener, collect satellite count and C/N0 only (NOT lat/long), cleanly stop it, check GPS power returns idle, no forced physical reboot. If controlled non-disruptive diagnostic cannot be implemented, keep active-fix acceptance PENDING.

## G385 tests
`python3 device/samsung/lt013g/tools/test_g385_gps_stock_uart_idle_contract.py --stock /home/debian/Projekty/samsung/kernel-stock-samsung --kernel /home/debian/Projekty/samsung/kernel --live /home/debian/Projekty/samsung/artifacts/SM-T311-G385-GPS-FIRST-20261016/G385-GPS-READONLY-RUNTIME.txt --json-output /home/debian/Projekty/samsung/artifacts/SM-T311-G385-GPS-FIRST-20261016/G385-GPS-STOCK-LIVE-GATE.json`.

Result PASS original selected hardware and modern implementation match + **7/7 deliberately wrong stock pin/variant/RTSCTS/device fixtures rejected**. No per-domain SELinux Enforcing, satellite lock, TTFF or reception benchmark has yet been proven by G385.

## Autonomous execution
The new 20-item ordered matrix is `docs/G385-AUTONOMOUS-GPS-FIRST-PRIORITY-MATRIX.md`, mirrored in `chat-history/AUTONOMOUS_PRIORITY_QUEUE.md`: GPS first, GPS SELinux second, factory stock parity third; publication/licensing LAST and non-blocking. Restore complete source history and updated plans via root-only SHA-verified private archive on tablet after each significant batch.

No source driver change was necessary after parity PASS. The user explicitly wants correct working hardware, not pointless porting of the old Samsung kernel or a speculative rewrite of an already enumerated interface.

## G385 new independently executed PHYSICAL GNSS test after the stock/idle audit

The user requested GPS FIRST and unattended safe engineering; the project already contained a purpose-built coordinate-free Android11 app `tools/t311-gnss-status`. Unlike intrusive direct HAL `setCallback()`, the app uses the supported Android LocationManager GNSS callback and never displaces the framework callback or writes latitude/longitude. It was freshly built `m -j3 T311GnssStatus` with Android 11 ARM32 signapk PASS, temporarily installed under /data, granted app-scoped fine/background permission while the user master location mode was already enabled (3) and left unchanged. It uses a foreground notification, starts GPS for 70 seconds, logs ONLY satellite counts, used-in-fix counts, TTFF, fix count and (after the second version) signal C/N0. No coordinates, NMEA or IMEI/SIM data are persisted or emitted. After each test the app stopped itself; it was explicitly uninstalled at end, so no app-specific location permissions remain.

### Physical first 70-second run
- Source: `artifacts/SM-T311-G385-GPS-FIRST-20261016/G385-GNSS-ACTIVE-70S-PHYSICAL.txt` (redacted T311GNSS-only event lines), SHA256 `147561b5899fa05ed145489cc62eee50ea16b17f61a885b7c2ab287fbb4d3d98`.
- Android reported `GNSS_REQUEST_STARTED callback=true`, then `GNSS_STARTED`. Real BCM4752 GPIO power rose to 1; Android `mStarted=true`.
- Final `GNSS_FINAL svEvents=132 fixes=0`, maximum **14 satellites** visible, **0 used in fix**. 70.003 seconds total.
- After stop, Android `mStarted=false`, real BCM4752 GPS power fell to `0` (not instant, but physically confirmed), boot1, Enforcing, Plus SIM LOADED.
- Host isolated outcome analyzer PASS and 7/7 fail-closed corrupt-input controls; JSON `G385-GNSS-ACTIVE-RESULT.json`.
- **This proves physical GNSS satellite status events in the Android 11 framework, NOT a location fix or strong RF conditions.**

### Physical second 70-second run — meaningful new signal metric, not a repeat
- Added private, coordinate-free `GnssStatus.getCn0DbHz()` to the test-only Java foreground service. No SVID or raw NMEA logged; strong25 counts signal C/N0 >=25 dB-Hz.
- Rebuilt ARM32 `m -j3 T311GnssStatus` (12/12 ninja tasks exit0), installed, granted same temporary app-specific permission, ran another bounded 70s and uninstalled.
- Source `artifacts/SM-T311-G385-GPS-FIRST-20261016/G385-GNSS-CNO-70S-PHYSICAL.txt` SHA256 `28db269bb12b702cbf9aaac09a81f1d3dc1b55abda5b773ed806c57ea96ca4ad`, no coordinates.
- Final `GNSS_FINAL svEvents=71 fixes=0 maxVisible=7 maxCn0=32.0`; maximum **7 satellites** visible, **2 above C/N0 25 dB-Hz**, **32 dB-Hz** strongest signal, 0 used, 0 fixes, 70.002 seconds total. Both GPS session and Android framework stopped. Read-after-stop GPS power0, Android boot1, enforcing, SIM LOADED.
- Host analyzer `tools/analyze_g385_gnss_satellite_run.py` revalidated both traces: first pass7/7 negative controls, C/N0 second pass9/9 negative controls; result JSONs `G385-GNSS-ACTIVE-RESULT.json` and `G385-GNSS-CNO-RESULT.json`.
- Existing app `org.pjarczak.t311gnssstatus` is **NOT left installed**, and Android location_mode and user's unrelated packages, ROM/BOOT/SYSTEM/modem remain unchanged.
- Do not conclude "GPS full fix PASS" from these two runs. Reception/status pipeline **PASS**, full GPS/GLONASS/SBAS 3D fix, position time, SUPL network and outdoor accuracy **PENDING**. Only two strong satellites in the second probe is compatible with poor reception/indoor testing; it does not prove RF hardware defective, nor prove future outdoor lock.

## Most useful autonomously executable NEXT GNSS work
1. Correlate genuine stock BCM4752 gpsd plugin behavior and AGPS network availability with GNSS status callbacks; inspect SUPL/A-GPS and XTRA time without corrupting receiver state, changing OEM modem or recording coordinates.
2. Extend source mocks to verify callbacks and SOLIB compatibility against exact G382 SELinux Binder policy. G382 install remains physically blocked by broken Android reboot-to-recovery; no speculative retries.
3. Controlled read-only GNSS power and satellite-count measurements only where a material new hypothesis exists; do not run 70-second generic repeats as they are already DONE. Later outdoor user-assisted fix test is a distinct physical acceptance.
4. Compare remaining GPS data files and HAL historical Samsung paths to protect source release. Do NOT port BCM47521 geofence IRQ GPX1(5) on a BCM4752 device; GPX1(5) MAX77693 IRQ owner is confirmed.
