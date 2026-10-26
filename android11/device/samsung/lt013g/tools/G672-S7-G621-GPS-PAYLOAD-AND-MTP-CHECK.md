# G672 — G621 GNSS na tablecie zgodny z bieżącym Android build i signed OTA

**Status urządzenia:** Samsung SM-T311, Android 11, kernel `7.3.0-pjarczak` BOOT G650, SELinux Enforcing. Bez nowego flasha, backupów, restartów, dźwięków czy kamery.

G672 potwierdza **bitową zgodność** wszystkich czterech kopii GPS HAL (35 908 bajtów):
1. `/vendor/lib/hw/gps.default.so` na fizycznym tablecie (nowy odczyt ADB),
2. `out/target/product/lt013g/system/vendor/lib/hw/gps.default.so` aktualnego Android build,
3. `payload/gps.default.so` w podpisanym `G621-G620-STRICT-NMEA-GPS-ONLY-TWRP-SIGNED.zip`,
4. hash po instalacji G621 udokumentowanej w `G621_PHYSICAL_ACCEPTANCE_20261020.json` (TWRP RC0).

Wszystkie SHA256: `102b7ea8f6536baa6a7db7fc8a946f5d29f49e387519bc7d8580489a2f671df2`. Podpisany ZIP SHA256: `e6adff00204333e5b2f53db27bd23e801fe6e52666b87f17d175f7ac06c083b1`.

Skrypt odtwarzający: `/home/debian/Projekty/samsung/linux-7.3-port/tools/g672_gps_physical_payload_gate.py`; surowy raport `G672_GPS_G621_PHYSICAL_PAYLOAD_PARITY.json`, test pięciu zmodyfikowanych wariantów bajtów/hashów prawidłowo je odrzuca. Oryginalny preinstall manifest G621 ma historyczne `SIGNED_HOST_ONLY_NOT_INSTALLED` i dodatkowy dosłowny ogon `\\n`; to NIE jest obecny status instalacji. Nie przerabiać manifestu historycznego ani nie flashować ponownie identycznego HAL.

**GNSS realna jakość FIX/TTFF/SUPL/AGPS jest nadal NIETESTOWANA fizycznie w warunkach terenowych.** `vendor.lt013g-gps-legacy-v195=running` potwierdza tylko stan usługi.

Pasywna diagnostyka USB: `dumpsys usb current_functions=MTP, applied=true, connected=true, configured=true, kernel_state=CONFIGURED`; `sys.usb.state=adb`, `adbd=running`, `/storage/emulated` to FUSE. Zdalne środowisko developmentu nie ma `/dev/bus/usb` ani GVFS; **kopiowanie MTP PC→tablet→PC przez Dolphin NIE ZOSTAŁO przetestowane**, nie oznaczać całej funkcji jako `PHYSICAL PASS`. Nie włączać eksperymentalnego USB OTG BOOST nocą.

**S0 PASS, S1 ACTIVE, S2–S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.** Audio/kamera/voice calls na sam koniec; GitHub publikuje sam właściciel.
