# G703 - ARM32 FUSE ABI/diagnostics split, SM-T311

- G686 EXP i G700 OFF+DEBUG_FS mają `CONFIG_FUSE_FS=y` oraz ten sam kod źródłowy `fs/fuse`. Jednak wskutek RCU Kconfig zmienia się wynikowa binarka: 15/15 nowych kontroli G703 PASS.
- `CONFIG_SM_T311_RCU_HOT4K_EXPERIMENT=y` warunkowo dodaje `atomic64_t passthrough_successful_opens` do `struct fuse_conn`, inkrementuje licznik w udanym passthrough open i dodaje plik `passthrough_opens` w fuse control. G700 wyłącza te pomiary (mimo `DEBUG_FS=y`). Maszynowe `fuse_conn_init` zeruje odpowiednio 240/224 bajty, a offset odczytu w `fuse_direct_io` zmienia się 0x80 -> 0x70. `CONFIG_PAGE_RCU_FREE` wpływa na `folio_test_rcu_free` w FUSE i numerację page flags.
- Sprawdzone tylko istniejące ARM32 ELF, bez fizycznego uruchomienia G700, którego zgodność FUSE/HAL i ABI pozostaje NIEPOTWIERDZONA. Nie zmieniać Androida, SELinux, GUI ani init pod ten eksperyment bez dowodu.
- Fizycznie nadal G650, G700 HOST ONLY DO NOT FLASH. G704: osobno rozważyć utratę opcjonalnych statystyk FUSE; S6 czeka na prawdziwe p9/p10 i TWRP. S0 PASS S1 ACTIVE S2-S6 BLOCKED S7 PARTIAL S8 ACTIVE. W każdym kolejnym raporcie cztery sekcje plus S0-S8. GitHub właściciel.
