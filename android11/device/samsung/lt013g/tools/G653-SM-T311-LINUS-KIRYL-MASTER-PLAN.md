# SM-T311 — obowiązkowy plan Linus/Kiryl (G653)

**PRZECZYTAJ PRZED KAŻDĄ DALSZĄ PRACĄ NA TYM TABLECIE.**

Repozytorium źródeł Android 11: `device/samsung/lt013g`. Ta instrukcja nie wprowadza żadnej zmiany kompilowanego Androida. Pełny plan i status bramek są w repozytorium kernela:

- `Documentation/mm/sm-t311-rcu-plan.rst`
- `Documentation/mm/sm-t311-rcu-progress.json`
- `tools/testing/selftests/smt311/plan_check.py`
- `tools/testing/selftests/smt311/rcu_aba_interleavings.py`
- `tools/testing/selftests/smt311/rcu_writer_sites.py`

Raport szczegółowy oraz kanoniczna tabela po polsku: `linux-7.3-port/SMT311_LINUS_KIRYL_PLAN_G653.md` w repozytorium dokumentacji projektu.

## W każdej kolejnej wiadomości o SM-T311 obowiązkowo powtórz:

**ZROBIONE:** Etapy S0–S7 ze stanami i dowodami (faktyczny commit, test, BOOT SHA).

**W TOKU / BLOKADY:** Co NIE zostało jeszcze zweryfikowane (bez deklarowania PASS z samego modelu).

**DALEJ:** Co konkretnie należy zrobić. Dopisz: „W następnej wiadomości ponownie przeczytaj plan i powtórz ZROBIONE / BLOKADY / DALEJ / TABLET”.

**FIZYCZNIE NA TABLECIE:** System boot_completed, kernel, RCU OFF/ON, stan modemu, fizyczne SHA BOOT. Odróżniaj HEAD źródeł od wgranego obrazu.

## Osiem etapów i bramek

| Etap | Stan G653 | Warunek zamknięcia |
|---|---|---|
| S0 — stan i repozytoria | PASS | Potwierdzony tablet G650 oraz źródła |
| S1 — recenzja writerów/readera | AKTYWNY | Pełny audyt wszystkich ścieżek XArray/folio |
| S2 — ARMv7 lifetime, RCU, ABA | BLOKADA | LKMM/herd7 + audyt niebezpiecznych przeplotów |
| S3 — HighMem, mapowanie, reclaim | BLOKADA | Dowód lifetime i brak regresji cache/Normal |
| S4 — krótkie fizyczne RCU ON | BLOKADA S2/S3 | 0 błędów, brak uszkodzenia danych i pamięci |
| S5 — kontrolowane OFF/ON | BLOKADA S4 | ABBA/BAAB, app p95/p99, energia, ≥3% praktycznego zysku |
| S6 — wybór ON/OFF | BLOKADA S5 | Bezpieczny, zmierzony najlepszy wariant |
| S7 — wydanie i publikacja | CZĘŚCIOWO | Tag, changelog, build, właściciel publikuje na GitHub |

**Aktualny fizyczny BOOT:** G650, SHA256 `2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3`. Linux `7.3.0-pjarczak`; refcount-free RCU skompilowane, ale domyślnie OFF. Nowe commity w kernelu zawierają wyłącznie dokumentację i testy: **nie oznacza to nowego flasha**.

**Ustalono:** G651 wykazał rywalizację o cache przy odczytach tej samej strony przez dwa rdzenie, ale 32,6-krotna różnica chybień cache NIE jest 32,6-krotną różnicą szybkości. G646 pokazał 54 HighMem i 47 Normal w 101 próbkach gorących dostępów ext4 podczas startu. Nie dowodzi to bezpieczeństwa HighMem RCU ON.

**Twarde zasady:** nigdy nie włączać RCU ON zanim S2/S3 nie przejdą; nie usuwać guardów; nie tworzyć nowych backupów; nie automatyzować PIN/PUK; nie uruchamiać dzwonków, audio i aparatu podczas snu użytkownika. Nie przywracać nieudanych G641/DMC60/WIN4. Zachować EGL G243, Mesa26, FUSE G638, HWC, RIL/GNSS. Użytkownik sam publikuje kod kernela i Androida.

**ZA KAŻDYM RAZEM POWTARZAĆ STATUS I PRZYPOMNIENIE O JEGO POWTARZANIU.**
