> **G719 correction:** GNSS Android 11 mNumSvStatus counter=0 cannot prove no HIDL callback: AOSP GnssMetrics increments only satellite entries WITH carrier frequency. Legacy BCM4752 HIDL1.0 does not provide it. Real raw G718 GPSD NMEA shows 495/495 satellite C/N0 fields EMPTY, GSA mode1, GGA quality0/00, RMC V. 11 in-view is NOT 11 tracked signals. See tools/G719-GPS-RF-NMEA-DIAGNOSIS.md and tools/G719-GPS-RF-HEALTH-20261021.json. Do NOT patch HIDL blindly. Outdoor sky reception check required.

# G718 — fizyczna instalacja G717 i pomiar GPS przy oknie

## ZROBIONE

- Na fizycznym SM-T311 wykonano `adb root`. Pierwszy restart adbd przejściowo pozostawił USB ADB w stanie `offline`; `adb reconnect offline` przywróciło dostęp. Cały system Android przez cały czas pozostawał uruchomiony.
- Przed zmianą odczytano źródło `/system/vendor/lib/hw/gps.default.so` i weryfikowano stary SHA `102b7ea8f6536baa6a7db7fc8a946f5d29f49e387519bc7d8580489a2f671df2`. Nowy G717 `/data/local/tmp/g717-gps-candidate.so`: SHA `c85b7b9e28f6bc8ac35d94d9b56011efde66b7d555dab1e7f7e3a5d90e7215a8` (36 032 bajty). Root potwierdził dokładne sumy obecnego BOOT p9 i TWRP RECOVERY p10.
- Na partycji SYSTEM dokonano podmiany TYLKO `/system/vendor/lib/hw/gps.default.so`, po uprzedniej kontroli sum obu wersji; nowe dane zapisano najpierw jako plik tymczasowy w tym samym katalogu, `root:root` `0644`, `u:object_r:vendor_file:s0`, potem atomowo nadano nazwę docelową. **Nie tworzono backupu**. Przejściowe `mount -o remount,ro /` zwróciło `EBUSY`, ale po restarcie wyłącznie usługi GNSS ponowienie operacji zakończyło się powodzeniem. SYSTEM na końcu jest **RO**.
- `setprop ctl.restart vendor.gnss_service`: Android GNSS HIDL PID **207 -> 4428**, obie usługi GPS `running`, Android `sys.boot_completed=1`, SELinux `Enforcing`, kernel nadal `7.3.0-pjarczak`.
- **Fizyczny pomiar przy oknie**: `am start` GPSTest; Android provider `mStarted=true`, żądanie co 1s przez 6 minut 41 sekund. Mostek `gps_proxy.c` odbierał NMEA i kompletne GSV; `G348_NMEA_GNSS_SV gps=11 glonass=0 qzss=0 sbas=0 total=11`. To 11 opisanych satelitów w NMEA, NIE 11 satelitów wykorzystanych w fixie. Licznik NMEA i GSV stale rósł. Mimo tego Android `Number of location reports=0`, `TTFF reports=0`, `Total number of sv status messages processed=0`. **Brak fixa**. Nie należy twierdzić, że nowe G717 poprawiło TTFF.
- Uruchomiono niekasujące danych `cmd location send-extra-command gps force_time_injection` i `force_psds_injection`. W logach były `G357_XTRA_DOWNLOAD_REQUEST_TO_FRAMEWORK gated=1` oraz `GnssLocationProvider WakeLock acquired by handleDownloadPsdsData()`, ale brak potwierdzenia skutecznego dostarczenia PSDS/NTP. Nie używano `delete_aiding_data`.
- W logach mostka wystąpiły komunikaty `G348_NMEA_GNSS_SV gps=11`, lecz metryki `LocationManager` pokazywały zero statusów SV. Weryfikacja źródeł pokazuje, że mostek woła `callbacks->gnss_sv_status_cb(&status)`, a AOSP HIDL `Gnss::gnssSvStatusCb()` powinien przekazać to przez `IGnssCallback`. Trzeba rozstrzygnąć, czy statystyka metryczna pozostaje zerowa z powodów wersji callbacku, czy dane nie trafiają do frameworka. Na razie **brak dowodu źródła usterki**, nie zmieniać HIDL na ślepo.
- Test zakończono przez `am force-stop com.android.gpstest.osmdroid`; provider ma `request=OFF`, `mStarted=false`. Usunięto pomocniczy plik testowy UI z `/data/local/tmp`. Nie pozostawiono aktywnego odbiornika na noc.
- Przywrócono `adb unroot`, USB ADB nadal `device`, `uid=2000(shell)`. Skrypt `tools/g718-postinstall-readonly-gps-acceptance.sh` uruchomiony na urządzeniu zarówno jako root, jak i shell. **PASS w obu przypadkach**; root dodatkowo sprawdził SHA p9/p10.

## W TOKU / BLOKADY — S0–S8

S0 PASS (G650 Android 11 wciąż uruchomiony, zmieniono wyłącznie GPS HAL), S1–S3 RETIRED (usunięty port no-ref RCU w G716), S4 REMOVED, S5 BLOCKED (niezweryfikowany fizycznie nowy kernel G716), S6 PARTIAL (fizyczne SHA BOOT p9 i RECOVERY p10 POTWIERDZONE; bezpieczny `adb reboot recovery`/powrót TWRP NIE przetestowany), S7 ACTIVE (G717 GPS zainstalowany, 11 satelitów w NMEA, brak fixa; modem/OTG/HWC/ADB pozostają), S8 ACTIVE (źródła, commity, manifest i komplet wyników).

## DALEJ

1. Przy znacznie bardziej otwartym niebie sprawdzić TTFF/fix (okno ogranicza sygnał GNSS), ale oddzielnie zdiagnozować zerowy licznik SV w Androidzie mimo NMEA SV=11.
2. Zweryfikować, czy NTP/PSDS/XTRA faktycznie zostały dostarczone do BCM4752 i dlaczego powtarzane są żądania AGPS-RIL referencyjnej komórki. Nie kasować assistance bez uzasadnienia.
3. Dalsze poprawki wykonywać z realnymi źródłowymi dowodami, osobnym binarnym buildem i zweryfikowaną instalacją.
4. Flash kernela G716 nadal wymaga nadzorowanego sprawdzenia wejścia/wyjścia z TWRP, pomimo znanych już fizycznych SHA BOOT/RECOVERY.

## FIZYCZNIE NA TABLECIE

**GPS G717 INSTALLED** i uruchomiony z poprawnym SHA; Android11/Linux7.3.0-pjarczak/G650, SELinux Enforcing, SYSTEM RO, GNSS i legacy usługi RUNNING, ADB root wyłączony ponownie. G716 BOOT nie wgrany. Zero backupów, zmian EFS/modem/PIN, restartów Androida, połączeń telefonicznych, kamery lub dźwięku. Dane lokalizacji i identyfikatory karty SIM nie zostały zapisane do raportu.
