# G728 — G727 fizycznie zainstalowany, start Androida i laptop USB PASS

## ZROBIONE

21 października 2026 użytkownik podłączył laptop. Na SM-T311 przez ROOT ADB Wi-Fi i USB potwierdzono G725 BOOT p9 SHA `56249bdd0fe20235d2ebe4c3da027b9c13c45ddc7f66eb28a5f1e1c06501a063`; plik G727 8MiB SHA `dcc7aafe81bb947e9afb4c8d1c4d49bbbeb413f65164c014cb02d392ae57f9bc`; bateria 96%, BOOT rozmiar8MiB, RECOVERY p10 SHA `f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f`. G727 **wgrany do fizycznego BOOT p9** (`dd bs=4096 count=2048 conv=fsync`), ponownie odczytany p9 SHA **identyczny z G727**. RECOVERY nietknięte.

Po reboocie Android11 system `sys.boot_completed=1`, `dev.bootcomplete=1`, SELinux Enforcing, `gsm.sim.state=LOADED`, `init.svc.ril-daemon=running`, kernel `7.3.0-pjarczak`, fizyczne USB ADB oraz `dumpsys usb current_functions=MTP connected=true configured=true`. Nowy G727 read-only `t311_otg_interlock`: `blocked=0 safe_off=1 pmic_boost=0 detach_edges=0 detach_ack=0 fault_latched=0 block_reason=0`. Przy laptopie `extcon_usb=1 extcon_host=0 vbus_present=1 mode_switch_host=0`, BOOST prawidłowo OFF. Ponownie uruchomiono ADB Wi-Fi `192.168.2.238:5555` i potwierdzono root oraz gotowość Androida.

Starsze raporty G727 określające ten kernel jako wyłącznie staged /data są HISTORIĄ, nie stanem obecnym. Źródło G727 jest w eksperymentalnej gałęzi kernela HEAD `68d0456de`; 21/21 source/model + 17/17 safety guards PASS, ale NIE stanowi to testu fizycznego pendrive.

## W TOKU / BLOKADY — S0–S8

S0 PASS Android G727 fizyczny start + SIM/RIL + USB MTP/ADB. S1–S3 RETIRED stare no-ref RCU. S4 REMOVED. S5 PARTIAL G727 p9 poprawnie uruchomiony, USB HOST z pendrivem jeszcze niezweryfikowany. S6 PARTIAL TWRP awaryjne nieprzetestowane. S7 ACTIVE tylko OTG (inne funkcje user PASS). S8 ACTIVE Git/test/ledger.

## DALEJ

Użytkownik otrzymał komunikat: odłączyć laptop USB i podłączyć adapter OTG z pendrivem, nie jednocześnie. ADB Wi-Fi root aktywne. Następnie odczytać `extcon_host`, role PHY, `blocked`, `detach_edges/ack`, `block_reason`, `fault_latched`, stan PMIC, USB enumeration i `sm list-disks`. Nie uznawać USB OTG za zaliczone bez faktycznego dysku USB. Nie uruchamiać BOOST na podłączonym PC.

## FIZYCZNIE NA TABLECIE

BOOT p9 G727 SHA `dcc7aafe81bb947e9afb4c8d1c4d49bbbeb413f65164c014cb02d392ae57f9bc`, nie tylko plik. Android działa, bateria 96% przed restartem, RECOVERY niezmienione. ADB Wi-Fi root. W tej części pracy nie było backupów, SIM PIN, połączeń, dźwięków, GNSS ani prób podania 5 V.
