SM-T311 G673 — dokładny status wdrożenia serii Linusa i Kiryla
================================================================

Zadanie priorytetowe
--------------------
Właściciel wskazał, żeby kontynuować przede wszystkim poprawki Linusa
Torvaldsa i Kiryla Shutsemau dla odczytu page cache bez referencji.
S7 archiwum rozmów i S8 pozostała tablica zadań mają niższy priorytet
w tym kroku. Samo kolejne przeglądanie rozmów nie oznacza poprawy RCU.

W rzeczywistych źródłach SM-T311 jest już komplet 18/18 commitów
eksperymentalnej linii z października 2026 r. Weryfikacja bazuje na
historii lokalnego repozytorium Linux 7.3:

* Bezpośredni rodzic serii: 4432bceab
* Pierwszy commit: 05362c1a7
* Ostatni commit: 016e91411
* 18 kolejnych commitów: weryfikacja rzeczywistych obiektów git,
  ich kolejności i przodka tagu wydania G650, nie tylko opisów.
* Fizyczny kernel G650 pochodzi z kodu a2fca4558, w którego historii
  jest ten sam komplet commitów.
* Między a2fca4558 i HEAD dla mm/fs/kernel/drivers/arch/include/init
  NIE MA żadnej różnicy źródłowego kodu wykonywalnego. Kolejne commity
  G651–G673 zawierają m.in. testy i dokumentację, bez nowego BOOT.

Na fizycznym SM-T311 z /proc/config.gz odczytano:

  CONFIG_PAGE_RCU_FREE=y
  CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y
  CONFIG_PREEMPT_RCU=y
  CONFIG_SMP=y
  CONFIG_HIGHMEM=y
  CONFIG_DEBUG_FS=y

Oznacza to, że optymalizacja jest skompilowana do działającego jądra.
Nie oznacza, że jest aktywna. Lokalny kod SM-T311 używa
static DEFINE_STATIC_KEY_FALSE(lockless_read_key), więc *domyślnie OFF*.
W G673 debugfs switch nie był bezpośrednio dostępny do odczytu;
nie raportować tego jako zmierzonego stanu przełącznika. Zerowe
liczniki /proc/vmstat pgfree_rcu* są jedynie dodatkową poszlaką,
nie odczytem samego static key.

Dostosowania dla 2 x Cortex-A9 ARMv7
-----------------------------------
Lokalne poprawki poza oryginalną serią:
* ograniczenie do HOT order-0 ext4 folio w ZONE_NORMAL;
* weryfikacja PG_rcu_free, barrier smp_rmb, xas_reload, i_size,
  folio->mapping i strefy po RCU lookup;
* sekcja rcu_read_lock + pagefault_disable podczas _copy_to_iter,
  maksymalnie 32 KiB w jednym przebiegu;
* atomowe oznaczanie gorącej strony przy normalnej referencji,
  odroczone zwolnienie i queue_rcu_work/expedited grace;
* ochrona przed włączeniem eksperymentu, kiedy pamięci strefy Normal
  jest zbyt mało lub w kolejce zalega odroczone zwalnianie;
* G648/G650 zmniejszyły narzut normalnych odczytów z RCU OFF.

Od strony budowy i pochodzenia zmian jest to port wykonany. Natomiast
nie ma zgody na uruchomienie no-ref RCU ON na tablecie.

Co nadal blokuje RCU ON
----------------------
1. S2: konkretna sytuacja ABA: stary niezaznaczony folio może zostać
   zdjęty z XArray i ponownie wykorzystany pod tym samym adresem,
   zanim reader sprawdzi flagę PG_rcu_free. Nowe folio może dostać
   tę flagę jako gorące, a porównanie samych adresów przez xas_reload
   nie jest identyfikatorem pokolenia. Oryginalne modele LKMM G658–
   G659 były diagnostyczne i NIE dowodziły rzeczywistego UAF;
   bezpieczeństwo wszystkich ścieżek nadal nie zostało wykazane.
2. S2: szczególne ścieżki migrate/split, inode eviction, hwpoison,
   XArray node retire, pełna kolejność ARMv7.
3. S3: HighMem jest świadomie wyłączony. Cache alias VIPT/ARMv7,
   kmap_local_page, presja 1,5 GB RAM i włączanie writable mmap
   równolegle do kopiowania nie są w pełni udowodnione.
4. Zysk realny na 2 Cortex-A9 nieznany. S4/S5/S6 mają pozostać
   BLOCKED. Benchmark 8-wątkowy Linusa nie jest dowodem korzyści
   z naszej 2-rdzeniowej architektury.
5. Test userspace XArray C G665 czeka na prawdziwe nagłówki
   liburcu-dev; nie został uruchomiony, NIE oznaczać PASS.

Odtwarzalna kontrola
--------------------
Uruchom::

    python3 tools/testing/selftests/smt311/rcu_series_gate_g673.py

Wynik G673: 18/18 rzeczywistych obiektów Git w ancestrii wydania
G650, 13/13 źródłowych kontroli PASS oraz 4/4 usunięte zabezpieczenia
wykryte przez ponowne uruchomienie tych samych testów na zmienionych
źródłach. Surowe wyniki w
Documentation/mm/sm-t311-rcu-series-g673.json.

Taki PASS dotyczy **zawartości Git, warunków kodu i polityki default
OFF**, nie dowodu RCU lifetime/ABA, rzeczywistego arm32 kernel ON
ani realnego speedup. W G673 nie zbudowano nowego kernela i nie
flashowano tabletu, bo brak zmian kodu wykonywalnego.

Dalsza implementacja dopiero po dowodzie
----------------------------------------
Priorytet G674: rozwiązać lifetime niezaznaczonego dawcy i tę samą
tożsamość fizyczną w XArray: albo dowieść bezpieczeństwa rzeczywistych
writerów, albo opracować i zweryfikować wariant z pewną ochroną przed
recyklingiem, nie dokładając na ślepo blokady na każdy szybki odczyt.
Dopiero potem S3 ARMv7 HighMem/cache i nadzorowane testy S4.

S0 PASS, S1 ACTIVE, S2 BLOCKED, S3 BLOCKED, S4 BLOCKED,
S5 BLOCKED, S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
GitHub publikuje wyłącznie właściciel. Audio, połączenia, kamera
wyłącznie na koniec i nie w nocy; zero backupów i ryzykownego flasha.


Post-G673 staged source exception (G674/G676)
---------------------------------------------
The G673 statement that executable paths are identical to physical
G650 applied to the date of G673. Subsequent *host-only* changes
G674 mm/filemap.c and G676 mm/page_alloc.c are now tracked in
Documentation/mm/sm-t311-rcu-progress.json as STAGED NOT INSTALLED.
The G673 series gate has been updated to fail closed against the
exact recorded list; it must NOT require all current source to remain
identical to G650 or assert that these new sources are on the tablet.
The original 18/18 patch ancestry still belongs to G650.
