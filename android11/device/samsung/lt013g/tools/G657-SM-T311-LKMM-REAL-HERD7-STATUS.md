# G657 — LKMM/herd7 działa na Debianie (SM-T311)

Data 20.10.2026. Repozytorium Androida otrzymuje ten wpis, aby status badań kernela nie zginął przy publikacji GitHub przez właściciela.

Właściciel zainstalował herd7 7.58. Początkowo program nie widział linux-kernel.cat; rozwiązanie to wskazanie oryginalnych modeli z drzewa kernela przez opcję -I. Kernel commit 0ac43dfe1 poprawia tools/testing/selftests/smt311/lkmm_prepare.py, weryfikuje standardowy smoke LKMM, dodaje dwa uproszczone testy 32-bitowego seqcount i sprawdza oczekiwany wynik wszystkich pięciu modeli.

Rzeczywiste wyniki:
- bariera smp_wmb/smp_rmb: Never 0/3;
- celowo brak bariery: Sometimes 1/3;
- abstrakcyjny reuse adresu folio / ABA: Sometimes 6/66;
- 32-bitowy i_size z kontrolą seqcount: Never 0/18;
- celowo brak ponownej kontroli seqcount: Sometimes 3/9.

**To nie jest dowód pełnego bezpieczeństwa RCU ON.** Modele upraszczają folio lifetime, XA lock, free/grace, seqcount retry i HighMem. Fizyczny BOOT pozostaje G650, RCU default OFF, Linux 7.3.0-pjarczak; nie wykonywano flasha, backupów, zmian Androida ani automatyzacji PIN.

Repozytorium ledger kernela: linux-7.3-port/G657_HERD7_LKMM_REAL_TESTS_20261020.md, G657_HERD7_SM_T311_ALL5_MACHINE_RESULTS_20261020.json; kernel documentation Documentation/mm/sm-t311-rcu-lkmm-g657.rst.

## Obowiązkowy status S0-S7 do kolejnych kontynuacji
S0 PASS; S1 ACTIVE (23 lokalizacje i 17 kontroli strukturalnych); S2 BLOCKED (herd działa i 5 modeli zweryfikowane, ale brak dowodu pełnego ABA/lifetime); S3 BLOCKED (HighMem); S4 BLOCKED (RCU ON); S5 BLOCKED (ABBA); S6 BLOCKED (decyzja); S7 PARTIAL (GitHub publikuje właściciel).

W każdej następnej wiadomości projektowej ponownie czytać plan, powtarzać ZROBIONE / BLOKADY / DALEJ / FIZYCZNY TABLET oraz S0-S7 i przypominać, żeby powtórzyć również następnym razem.
