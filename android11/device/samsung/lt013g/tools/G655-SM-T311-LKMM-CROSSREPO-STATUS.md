# G655 — SM-T311: synchronizacja audytu RCU z kodem Androida

Data: 20.10.2026. Ten plik jest celowo w źródłach `device/samsung/lt013g/tools`, aby przy publikacji Androida nie zginęło powiązanie z pracami nad kernelem.

**Fizyczny system Android 11 i kernel G650 pozostają bez zmian.** Kontynuacja G655 dotyczy audytu kodu; nie zawiera nowego ROM, biblioteki GPU/HWC, sterownika ani BOOT.

## Co zrobiono

Kernel Git:
- `4a8037330` — rozszerzenie audytu do **23 źródłowych lokalizacji i 17 warunków**, w tym FUSE pipe folio steal, memory-failure i `synchronize_rcu`, tagowanie przy split, shmem i 32-bitowy seqcount.
- `313da14a0` — kontrola prawdziwego herd7, najpierw standardowy test LKMM, również wykrywanie niekompletnej instalacji z brakującymi modelami.
- `7b14401cb` — 11 kategorii ścieżek reader/writer i otwartych zobowiązań bezpieczeństwa zapisanych w `Documentation/mm/sm-t311-rcu-review-g655.rst`.

**To nie jest zakończony dowód**: S1 pozostaje aktywny; S2-S6 pozostają zablokowane. Na obecnym Debianie brak herd7. `--probe` zwraca 2, a nie pozorne PASS. Zgłoszony był też problem brakujących modeli w wydaniu Debian herdtools7 7.58-1; należy sprawdzić uruchamianie standardowego testu, nie samą obecność programu.

## Stały plan i stan

| S0 | PASS: fizyczny BOOT G650 |
| S1 | ACTIVE: audyt 23 miejsc/17 warunków, analiza wszystkich możliwych interleavings nieukończona |
| S2 | BLOCKED: rzeczywisty herd7, LKMM, 32-bit seqcount, XArray ABA |
| S3 | BLOCKED: HighMem, cache alias, TLB, lifetime, presja pamięci |
| S4 | BLOCKED: rzeczywiste krótkie RCU ON |
| S5 | BLOCKED: kontrolowane ABBA realnych aplikacji, termika i energia |
| S6 | BLOCKED: bezpieczna decyzja ON/OFF |
| S7 | PARTIAL: finalny release i GitHub tylko właściciel |

Fizyczny BOOT: G650, ostatnia fizycznie zweryfikowana suma `2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3`, Linux `7.3.0-pjarczak`, Android 11. Domyślny refcount-free RCU OFF. Od G650 nie zmieniano kodu wykonywanego kernela. Kod EGL G243, Mesa26, HWC, FUSE, RIL/GNSS nietknięty.

Kanon:
- Kernel: `Documentation/mm/sm-t311-rcu-plan.rst`, `sm-t311-rcu-progress.json`, `sm-t311-rcu-review-g655.rst`.
- Debian: `linux-7.3-port/SMT311_LINUS_KIRYL_PLAN_G653.md` i raport G655.
- Android: `tools/G653-SM-T311-LINUS-KIRYL-MASTER-PLAN.md`.

**W każdej kolejnej wiadomości o SM-T311: powtórz ZROBIONE / BLOKADY / DALEJ / FIZYCZNIE NA TABLECIE oraz pełną tabelę S0–S7, i ponownie przypomnij o powtarzaniu tej reguły.** Nie rób backupów, nie automatyzuj PIN/PUK, nie włączaj RCU ON bez dowodu, nie uruchamiaj audio/kamery/rozmów podczas snu właściciela, nie publikuj na GitHub bez niego.
