> **G724 CURRENT UPDATE:** Exact G724 OPT-IN 5V test kernel compiled ARM32 vmlinux/zImage/DTB, separately SHA-pinned BOOT 06dc3e3567e5af5cd19acb160eab9bad312486e7504ed044a1c34f5f96931099 8MiB staged only /data/local/tmp/BOOT-G724-OTG-EXPERIMENTAL-8MiB-DO-NOT-FLASH.img with physical SHA check. Experimental source branch G724 commit 6a38a1a6f and 17/17 guard checks, new MAX77693 900mA safety refusal, extcon propagation. `tools/g724-otg-lab-supervisor-NOT-AUTORUN.sh` is manual lab-only, physically on /data, --status no side effects and invalid token refused. The user says laptop USB ADB+MTP WORK, and confirmed other functions working; do not disrupt. USB HOST 5V has NOT BEEN ACTIVATED OR QUALIFIED, nor boot flashed. Full `project/G724_OTG_MAX77693_FULL_SOURCE_RESEARCH_20261021.md`.

# G723 — bieżący fizyczny test OTG, bez uruchamiania BOOST

## ZROBIONE

Najnowszy raport użytkownika (21.10.2026): **rozmowy przychodzące/wychodzące i rozłączanie PASS, SMS wysyłane/odbierane PASS, MTP PASS, ADB PASS, ADB Wi-Fi 192.168.2.238:5555 włączone, GPS zakończony PASS, opcje programistyczne PASS, brak zawieszeń/artefaktów HWC/UI**. Nie dodawać ponownie do backlogu starożytnych awarii.

Przy fizycznym podpięciu pendrive’a OTG zniknęło ADB USB, dlatego połączono się samodzielnie przez dostarczony przez użytkownika ADB TCP `192.168.2.238:5555`. System boot1, SELinux Enforcing, RIL/SIM LOADED, GPS nietestowany.

### Warstwy OTG i dane

- Fizycznie uruchomiony G650 `/proc/config.gz`:
  - `CONFIG_USB_STORAGE=y`
  - `CONFIG_EXTCON_MAX77693=y`
  - `# CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL is not set`
- Logi kernela MAX77693 MUIC pokazują w czasie pomiaru `adc:0x00` attach x3 / detach x2, ostatnie attach. MAX77693 interpretuje ADC GROUND jako kabel host USB / MHL / AV zależnie od statusu grupy; wskazuje na wykrycie ID GROUND, ale bez odczytu extcon nie potwierdza ostatecznej aktywacji EXTCON_USB_HOST.
- Android `dumpsys usb` `host_connected=false`, `source_power=false`, `kernel_state=DISCONNECTED`; `sys.usb.state=adb`. Ramka host_manager ma tylko **wewnętrzny modem HSIC** (decimal vendor 5401, product 32), brak urządzeń zewnętrznych.
- `USB_SCSI_BLOCK_DEVICES=0`, `sm list-disks` `disk:179,0` — nie USB pendrive, `vold` running. Pendrive nie wszedł nawet w USB enumeration, to nie awaria aplikacji Pliki.
- `max77693-charger/online=0` oznacza brak wejściowego zasilania ładowarki, nie jest bezpośrednim pomiarem fizycznego wyjścia 5 V VBUS; jego stan należy dopiero zmierzyć.
- `/sys/.../t311_usb_role_diag`, surowe `idVendor` dla ADB shell są niedostępne w SELinux; nie interpretować tego jako zerową liczbę urządzeń. Skrypt raportuje dostępność zamiast kłamać.

### Wykonany kod diagnostyczny

`tools/g723-otg-passive-intake.sh` to wyłącznie read-only program `/system/bin/sh`:

- bez roota, USB reset, montowania, odpinania urządzeń, zapisu do sysfs/MAX77693, bez VBUS BOOST i bez odczytywania zawartości pendrive’a;
- rozpoznaje modem wewnętrzny w `dumpsys usb` jako `5401:32`, ignoruje wpisy `vendor_id=-1` w zagnieżdżonych deskryptorach USB; liczy external vs modem prawidłowo;
- raportuje zdarzenia MUIC w oknie 1–1800 s, jako agregaty bez prywatnych identyfikatorów;
- przy blokadach SELinux surowego sysfs raportuje `RESTRICTED_OR_UNAVAILABLE` zamiast fałszywego braku USB;
- skopiowany do `/data/local/tmp/g723-otg-passive-intake.sh` przez Wi‑Fi, SHA `2e6f17e92dcf0de7d5ec488ae834ba9e462f4123ee3679c6c61a1647c128c1ea` potwierdzony na urządzeniu;
- wykonany fizycznie **PASS** jako shell, nie zmienił konfiguracji.

Uruchom ponownie, tylko gdy chcesz pobrać aktualny log OTG:
```
adb -s 192.168.2.238:5555 shell sh /data/local/tmp/g723-otg-passive-intake.sh 900
```

### Źródłowa poprawka MUIC

Linux `drivers/extcon/extcon-max77693.c` w G716 ma błąd: `max77693_muic_adc_handler()` ignoruje ujemny return z `max77693_muic_adc_ground_handler()` po nieudanym AP_USB mux I2C. W G723 poprawiono oddzielnie, normalnie propaguje błąd; skompilowany ARM32 ELF obj PASS. Gałąź `smt311-g723-otg-muic-return-guard` `bba92cddc`, katalog project `experimental/G723-otg-muic-return-guard`. NIE uruchomiono na fizycznym tablecie, nie zastąpiono czystego G716, nie jest to gotowy elektryczny OTG.

## W TOKU / BLOKADY — S0–S8

S0 PASS Android11/G650 i działające wszystkie użytkownikowo potwierdzone funkcje; S1–S3 RETIRED stare no-ref RCU; S4 REMOVED; S5 BLOCKED nowy G716 niewgrany; S6 PARTIAL TWRP recovery niebezpieczne bez nadzoru; S7 ACTIVE **USB OTG nie wykrywa pendrive’a, host+5V BOOST**, pozostałe wymienione funkcje właściciel mówi PASS; S8 ACTIVE źródła/Git/diag.

## DALEJ

Potrzebne kwalifikowane źródło VBUS 5 V, USB-host PHY, PMIC MAX77693 oraz fizyczny test z zabezpieczeniem prądowym i rollbackiem. Kconfig tego eksperymentu jest OFF z powodu ryzyka przejścia ładowanie↔5 V i modem PHY. Możliwy kontrolny **hub USB z własnym zasilaniem** (po potwierdzeniu adaptera i braku konfliktu PC), który odróżni brak VBUS od blokady toru danych; nie zastępuje jednak konfiguracji hosta. Nowy kernel dopiero po bezpiecznej drodze recovery i testach źródłowych. Nie zmieniać działających RIL/SMS/MTP/GPS/UI.

## FIZYCZNIE NA TABLECIE

Bez modyfikacji BOOT/SYSTEM/RECOVERY, GPS, modemu/PIN, źródeł VBUS, funkcji USB czy SELinux; tylko plik pasywnej diagnostyki `/data/local/tmp`. ADB Wi‑Fi pozostawione aktywne i wybrany ten transport w narzędziach diagnostycznych. Nie twierdzić, że pendrive naprawiony — jest NIE WYKRYWANY.
