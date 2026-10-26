# G671 — fizyczna zgodność vendor kontra historyczny status NOT-INSTALLED

**Ważna korekta S7, bez zmiany kodu wykonywalnego.** Odczyt wyłącznie przez USB ADB na działającym SM-T311 wykazał, że cztery stare pliki nazywane *kandydatami niewdrożonymi* są obecnie w `/vendor/bin` bitowo identyczne ze starymi kopiami `/data/local/tmp/G*-NOT-INSTALLED`:

| Plik /vendor/bin | SHA256 fizyczny | Wcześniejszy kandydat |
|---|---|---|
| `t311-modem-watchdog` | `f8b203ff22d101894e7ffa7c0c48cd4d7ca93c9b2bdf3a71c5926a35adfff86c` | G619 |
| `t311-cbd-pidfd-signal` | `e9b08ba4055d8ee3f988d80bc3bfb8df7b0af204ec4214102704c5f5178ef048` | G619 |
| `t311-mobile-session-probe` | `8b7f15f0f58ef225c56a39dccec9dea232d6cb3937eaea818e56aafe058fd330` | G616 |
| `t311-audio-ril` | `afcfb90944f53746c05e3bd033d5421782a0428eccf473b8060fe133603f8e65` | G612 |

Dodatkowo SHA `radio/t311-modem-watchdog.sh` w aktualnym Android Git jest identyczne z fizycznym /vendor/bin/t311-modem-watchdog. Plik helpera PIDFD i probe także mają hash fizycznie zgodny ze starszymi kandydatami. **Wnioski: ON-DISK HASH VERIFIED**, nie `NOT INSTALLED`, ale jednocześnie **funkcjonalne testy końcowe nadal niezaliczone**. Nie wiadomo, w którym momencie te cztery pliki zostały skopiowane na vendor.

`/vendor` jest symlinkiem do `/system/vendor`. Fingerprint `SMT311G591` nie stanowi sam w sobie dowodu wersji pojedynczych binariów vendor.

Stan usług: `init.svc.t311-audio-ril=running`, `init.svc.t311-modem-watchdog=stopped`; watchdog jest zadeklarowany jako `oneshot`, dlatego jego zakończenie nie jest dowodem awarii. `init.svc.ril-daemon=running`, `gsm.sim.state=LOADED`. **Żadnych prób resetowania CP, wysyłania PIDFD SIGKILL, połączeń telefonicznych, PIN ani audio**.

Dodatkowy GNSS kontrolny SHA fizyczny `/vendor/lib/hw/gps.default.so` = `102b7ea8f6536baa6a7db7fc8a946f5d29f49e387519bc7d8580489a2f671df2`. Przed oznaczeniem GNSS jako zgodny ze źródłem trzeba porównać z bieżącym artefaktem kompilacji, nie z samym historycznym hashem G608.

Reprodukowalny raport `/home/debian/Projekty/samsung/linux-7.3-port/G671_S7_PHYSICAL_VENDOR_HASH_AUDIT.json` i skrypt indeksu treści rozmów `tools/g671_s7_conversation_user_intent.py` w ledger. Przegląd: 45 zindeksowanych rozmów, 650 wystąpień wypowiedzi użytkownika po deduplikacji wewnątrz rozmowy; jedno archiwum DOM-fallback ma zero dostępnych wypowiedzi użytkownika i pozostaje brakującym dowodem, nie PASS.

Etapy: S0 PASS / S1 ACTIVE / S2–S6 BLOCKED / S7 PARTIAL (treść+Git+akceptacja) / S8 ACTIVE (tablica dalszych zadań). **GitHub poza etapami, push wyłącznie właściciel.** Audio, kamera, realne telefony na sam koniec. G650 BOOT bez zmian, ref-free RCU domyślnie OFF. Nie robiono backupu ani flasha.
