# SM-T311 — AKTUALNY STAN, ZROBIONE / DO ZROBIENIA (2026-10-17)

**Źródło prawdy przy KAŻDYM dalszym etapie:** ten dokument, CURRENT_RELEASE.json, NEXT_TASK.md oraz MASTER_PRIORITY_EXECUTION.json. Archiwum rozmów i starsze handoffy są historyczne.

## TERAZ NA TABLECIE

- **G408 FIZYCZNIE ZAINSTALOWANY**, Android 11 / Linux 6.12.112, boot_completed=1, SELinux Enforcing. G379 to poprzednie wydanie.
- **adb reboot recovery: NAPRAWIONE I POTWIERDZONE**, normalnym ADB uruchomiono TWRP 3.1.1-0, następnie wrócono do Androida.
- G408 TESTKEY signed OTA 484330484 bajty SHA256 5ae3595004b2043715408eed2e3f33f1ded920a3061f9f42504f0303e12c7a79. TWRP install exit 0; nowy BOOT cad5e76... i niezmieniony TWRP p10 f818c29f...
- G408 postinstall dwa razy: **14 PASS, 0 FAIL**. Działają: /misc p11+SELinux, prywatny GPS Binder, gpsd, ZRAM 768MiB LZ4 oraz modemowy /32 z host-gateway/default route.
- /data **49% zajęte, około 6 GiB wolnego** po usunięciu starych technicznych plików. Brak wipe DATA/EFS/CP, brak komend PIN/PUK, brak automatycznych połączeń/SMS.
- **GPS ma działające elementy sprzętowe**, ale pełny fix pozycji 3D nie został potwierdzony. Sama obecność procesu nie dowodzi lokalizacji.
- **G410–G414 nie są na urządzeniu**: wyłącznie osobne eksperymenty hostowe. Nie flashować jako ukończony ROM.

## PRIORYTETY — tylko to, co rzeczywiście pozostaje

1. **GPS i komórkowy internet** — NETWORK VALIDATED, DNS/NTP/SUPL, aktualny UTC i prawdziwa pozycja GPS/GLONASS 3D.
2. **Telefonia** — jedna rzeczywista próba połączenia przychodzącego (UI i dźwięk) oraz SMS ACK, wyłącznie gdy użytkownik sam wywoła test; bez PIN/PUK/RIL/EFS/CP ingerencji.
3. **SELinux** — pozostałe permissive GNSS/RIL tylko jeśli nowe AVC, z wąskimi regułami i testami.
4. **Rzeczywiście niesprawdzone szczegółowe tryby urządzeń** — bez ponawiania zaakceptowanych GPU/MFC/FUSE/kamer/RAM/sensorów.
5. **Opcjonalny rozwój** — dokładne mA PowerProfile i /proc/uid_time_in_state G414; NIE blokuje działającego G408. Licencje publiczne na końcu.

## 20 oryginalnych zadań — czytelny status

| ID | Status | Zrobione | Nadal potrzebne |
|---|---|---|---|
| P0-1 | CZĘŚCIOWO | Global Enforcing i nowy GNSS Binder/OTA p11 label PASS. | Pozostałe domeny GPS/RIL permissive tylko po AVC. |
| P0-2 | WDROŻONE | G408 kompletne podpisane OTA, TWRP instalacja rc0 i Android boot1 dwukrotnie. | Pełne wydanie czeka na GPS3D/telefon. |
| P0-3 | ZAMKNIĘTE | AudioFX usunięty z ROM. | Brak ponownego testu bez regresji. |
| P0-4 | ZAMKNIĘTE | Lineage Trust zbędny, natywne zabezpieczenia zachowane. | Brak. |
| P0-5 | OTWARTE | Prywatne źródła i dokumentacja w Git. | Licencje OEM/publiczna dystrybucja na sam koniec. |
| P0-6 | ZAMKNIĘTE | BOOT G408 SHA cad5e76..., recovery p10 oryginalny f818c29f..., TWRP preflight V2 PASS, install=0, dwa boot1. | Brak w samym kontrakcie instalacji. |
| P1-7 | ZAMKNIĘTE | GPU Mali/FIMD/HWC/G2D poprzednie fizyczne PASS. | Nie wracać bez konkretnego błędu. |
| P1-8 | ZAMKNIĘTE | SystemUI/SurfaceFlinger i render wcześniej PASS. | Nie wracać bez regresji. |
| P1-9 | ZAMKNIĘTE | FUSE/MediaProvider przyjęte we wcześniejszych testach. | Tylko nowe błędy. |
| P1-10 | CZĘŚCIOWO | RIL żyje; rmnet1 /32, host-gateway/default route PASS w G408. | Internet VALIDATED; ekran połączenia przychodzącego, audio rozmowy, SMS ACK, tylko testy użytkownika. |
| P1-11 | CZĘŚCIOWO | BCM4752 i do 14 satelitów wcześniej; nowy G408 GPS ELF/daemon/private Binder pass. | Ostateczny GPS/GLONASS 3D fix na zewnątrz, DNS/NTP/SUPL i native UTC. |
| P1-12 | ZAMKNIĘTE | MFC H.264 1080p i testy kamer z archiwum (m.in. 46/46) PASS. | Tylko konkretny nowy regres. |
| P1-13 | CZĘŚCIOWO | WiFi/BT, wejścia, sensory i część urządzeń fizycznie badane. | Jedynie niezaliczone konkretne tryby, nie cały sprzęt od początku. |
| P1-14 | CZĘŚCIOWO | PMIC/thermal MAX77693/MAX17050, termika i suspend już badane. | Długoterminowa kalibracja mA, tylko realne problemy. |
| P1-15 | ZAMKNIĘTE | W G408 fizyczny ZRAM 768MiB, LZ4 i /proc/swaps PASS (786428KiB). | Nie mierzyć od zera; regresje tylko jeśli wystąpią. |
| P2-16 | ZAMKNIĘTE | Podstawowa obsługa dotyku i czujników oraz oryginalna Samsung BSP badane. | Tylko brakujące tryby P1-13. |
| P2-17 | ZAMKNIĘTE | Linux 6.12.112 działający fizycznie G408; zbudowane G413/G414 ARM32 host. | Prototypy nie należą do systemu. |
| P2-18 | PRZYSZŁE | G410–G414 testy host i pełny linker ARM32. | /proc/uid_time_in_state i model G411 prądów bez realnych danych nie wdrażać. |
| P2-19 | ZAMKNIĘTE | Odchudzony ROM; /data 96% do 49%, ponad 5GiB odzyskane z technicznych plików. | Brak. |
| P2-20 | CZĘŚCIOWO | G408 boot/testy 14 PASS 0 FAIL, naprawione adb reboot recovery, powrót do Androida. | GPS3D, Internet VALIDATED, użytkownikowe telekom testy i końcowe niesprawdzone tryby. |

## Dlaczego wcześniej nie działał recovery?

G379 posiadał aktywny vendor fstab bez /misc i Android init odrzucał bootloader_message. Oryginalne sprawdzone jądrowe narzędzie G341 weszło do istniejącego TWRP3.1.1. G408 aktualizuje vendor fstab OTA-p11 /misc i SELinux. Początkowy pinned preflight miał fałszywie zahardkodowany SYSTEM major:minor 179:20; w starszym TWRP ta sama fizyczna partycja p20 ma 259:4, dlatego utworzono NIEZAPISUJĄCY skrypt V2 weryfikujący rzeczywistą identyczność sysfs. V2 PASS, pełna instalacja G408 rc0. Następnie normalne adb reboot recovery fizycznie PASS. TWRP recovery p10 zostało nietknięte.

## Następny krok przy każdym nowym poleceniu

Najpierw adb.workspace_info i adb.devices; odczytaj ten dokument i CURRENT_RELEASE.json. Wykonuj punkt 1 otwarty samodzielnie tam, gdzie można pasywnie przez ADB, bez powtarzania zakończonych testów. Po każdej zmianie uaktualnij DONE/OPEN, QA i zsynchronizuj małe archiwum na tablet.

Raport rzeczywistej instalacji: artifacts/SM-T311-G408-ZRAM-20261017/G408-PHYSICALLY-INSTALLED-RECOVERY-BCB-FIX-20261017.json. Poprzedni master zachowano jako MASTER_DONE_TODO_BEFORE_G408_INSTALL_20261017.md.
