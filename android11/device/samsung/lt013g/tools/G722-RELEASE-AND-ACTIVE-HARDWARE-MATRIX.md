> **G723 ACTUAL USER TEST / 2026-10-21:** Incoming/outgoing calls, hangup, SMS send+receive, MTP, ADB, GPS, Developer options and graphical UI/HWC currently PASS by user's physical report. The older unresolved tasks listed below are not current reproduced failures. USB OTG pendrive NOW plugged: MAX77693 MUIC `adc:0x00` ID grounded events, G650 physical `/proc/config.gz` OPT-IN OTG BOOST OFF, Android UsbHostManager only internal modem1/external USB0, no USB SCSI disk. Wi-Fi ADB 192.168.2.238:5555 connected with no USB PC cable. Main new report `tools/G723-OTG-UNPOWERED-PASSIVE-20261021.md` and project G723 report. No 5V source, no kernel flash, no GNSS tests.

# G722 — tablety SM-T311: aktywne wdrożenia i bramki wydania (21.10.2026)

**Uwaga:** aktualny i kanoniczny plan prac: `/home/debian/Projekty/samsung/linux-7.3-port/G722_CURRENT_DEPLOYMENT_AND_BUG_MATRIX_20261021.md`. Starsze wiersze w `docs/SM-T311-HARDWARE-FUNCTION-MATRIX.md` opisują okres G379–G585 i mogą być nieaktualne.

## Co rzeczywiście jest na urządzeniu?

- Android 11 działa z **kernel G650**, Linux `7.3.0-pjarczak`, SELinux `Enforcing`.
- GNSS G717 **jest zainstalowany**: `/vendor/lib/hw/gps.default.so` SHA256 `c85b7b9e28f6bc8ac35d94d9b56011efde66b7d555dab1e7f7e3a5d90e7215a8`. W G721 faktycznie uzyskał 3D fix, TTFF 6s i finalnie hAcc 12m. Użytkownik uznał temat za ZAMKNIĘTY; nie włączać GPSTest na biurku.
- RIL/SIM LOADED i `t311-modem-watchdog` oraz `t311-mobile-session-probe` obecne na tablecie mają SHA zgodne z zbudowanymi wersjami hosta. Nie podmieniać bez potrzeby. Zero auto-PIN.
- `sys.usb.state=adb` mimo `dumpsys usb current_functions=MTP`. Polecenie framework `svc usb setFunctions mtp` nie zmieniło fizycznego `sys.usb.state`. MTP był wcześniej potwierdzony przez użytkownika, lecz teraz brak aktualnego testu transferu; OTG pendrive nadal niezweryfikowany. Nie przestawiać 5V BOOST na ślepo.
- ADB Wi-Fi obecnie wyłączone; nie otwierać plaintext 5555 bez zgody użytkownika.

## Co poprawiono w źródłach i w buildzie?

- G716 całkowicie usunął eksperymentalny port no-ref RCU, zachował dwie niezależne optymalizacje `filemap_read`, normalny Linux RCU, FUSE/MGLRU i aktualną konfigurację sprzętu. Nowy BOOT G716 ma dokładne 8 MiB i SHA256 `5d275fb7977d95e9e3515ff571efe675359e0874e1f8c5aef4b9c925d19182c1`.
- G722 naprawił **rzeczywiście zepsutą bramkę zgodności Androida**: stary `tools/verify-android11-release-abi.py` wskazywał nieistniejący build G513. Teraz sprawdza G716; **56/56 PASS**.
- G722 `m -j2 bootimage-nodeps` **PASS**, `out/target/product/lt013g/boot.img` ma teraz właściwy G716 SHA. Nie zapisywać do p9/BOOT bez zweryfikowanego TWRP.
- G722 naprawił stary `tools/package-android11-offline-boot.py`, który wskazywał G513/G541. Teraz przekazuje do bezpiecznego canonical G716 builder/stager.
- Plik G716 wgrany do tabletu **WYŁĄCZNIE NIEAKTYWNIE** do `/data/local/tmp/BOOT-G716-CLEAN-8MiB-DO-NOT-FLASH.img` i zweryfikowany SHA, tryb 0600.
- **STARY ROM ZIP** `out/target/product/lt013g/lineage_lt013g-ota-eng.debian.zip` nadal zawiera stary **G541 BOOT SHA `81b01abb5612a9a777330705c02c4bc1f49386da4d5df0950f955246f9fa6bce`**. W żadnym razie nie nazywać tego ZIP-a pełnym nowym G716. Trzeba go przebudować przed użytkowym flashowaniem.
- Nowe `tools/g722_release_payload_gate.py` i `tools/test_g722_release_payload_gate.py` w repo projektu odrzucają taki przestarzały OTA (faktyczny stan 7/8 kontroli źródłowych PASS, ZIP BLOCKED, 5/5 scenariuszy testowych PASS). Gating porównuje realny plik BOOT wewnątrz ZIP-a, nie polega tylko na nazwie.
- To jest kontrola **samej integralności BOOT**, nie sprawdza podpisów, całego SYSTEM/VENDOR ani możliwości bezpiecznego flashowania. Każde przyszłe wydanie potrzebuje kompletnej kontroli OTA i nadzorowanego recovery.

## Zaległości (bez retestów GPS)

1. **RIL/telefon/SMS:** „Rozłączam...” UI, fałszywy SMS ACK, rozmowy przychodzące i dwukierunkowe audio — wymaga prawdziwych prób użytkownika, nigdy automatyczny PIN.
2. **USB MTP i OTG:** rozbieżność menedżera i gadget, rola PC/host, bezpieczny VBUS.
3. **TWRP:** potwierdzone SHA p9/p10, przynajmniej jeden wcześniejszy sukces reboot recovery, ale zdarzał się czarny ekran — ścieżka nie jest bezpiecznie powtarzalna.
4. **Mali400/HWC/SystemUI:** sporadyczne glitche/lag, źródłowa analiza z rzeczywistymi metrykami przed zmianą DVFS.
5. **ADB Wi-Fi:** zapamiętywanie wyboru z odpowiednim uwierzytelnieniem, aktualnie OFF.
6. **Developer options / UI:** opóźnienie „Zaczekaj”; inne BT/microSD/czujniki — częściowo niezaliczone.

Wszelkie stare raporty G718–G720 o braku fixa GPS są HISTORYCZNE i zostały unieważnione jako bieżąca diagnoza przez udany G721. Historyczne anomalie PMIC/VDD pozostają do oceny tylko przy nowym rzeczywistym błędzie; dziś urządzenie działa stabilnie.

## S0–S8

S0 PASS (fizyczny system), S1–S3 RETIRED (no-ref RCU wycofane), S4 REMOVED, S5 BLOCKED (G716 BOOT fizycznie jeszcze niewgrany), S6 PARTIAL (p9/p10 SHA znane, TWRP niestabilne), S7 ACTIVE/PARTIAL (GPS DONE, inne wymienione), S8 ACTIVE (kod, Git, tabela, release gates).

## FIZYCZNIE NA TABLECIE

Bez nowych testów GPS, dźwięku, rozmów, SMS, PIN/PUK, restartu/TWRP i backupów w G722. Android nie rebootowany, partycje BOOT/RECOVERY/ROM bez zmian. Tylko bezpieczny 8-MiB kandydat w `/data/local/tmp`. Użytkownik publikuje GitHub samodzielnie.
