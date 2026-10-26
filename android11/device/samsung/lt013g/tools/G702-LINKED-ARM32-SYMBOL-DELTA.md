# G702 - porównanie zlinkowanych symboli G686/G700 bez instalacji

- 2026-10-21: skrypt i JSON kernela G702 wykazały identyczne 26742 nazwy i typy symboli globalnych dwóch hostowych ARM32 ELF. Z 26547 symboli posiadających rozmiary 18 ma różne rozmiary, z czego pięć to funkcje FUSE.
- Kod `fs/fuse` nie zmienił się od źródła fizycznego G650, jednak różne Kconfig wariantów G686/G700 wpływają na zlinkowane instrukcje. Samo dopasowanie nazw ELF nie dowodzi zgodności FUSE dla Androida 11. Wymaga oddzielnego przeglądu i później fizycznej kwalifikacji.
- Fizyczny SM-T311 BOOT pozostaje G650. G700 RCU OFF + DEBUG_FS jest host-only, SHA256 `aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f`, bez zezwolenia na flash. Partycje p9/p10 nie mają aktualnie potwierdzonego odczytu SHA z TWRP.
- Bez nowych zmian wykonywalnych Androida. Nie włączać RCU ON, nie zmieniać SELinux/modemu/GNSS/HWC/FUSE bez dowodu; nie reinstalować funkcji już zatwierdzonych i obecnych na urządzeniu.
- S0 PASS, S1 ACTIVE, S2-S6 BLOCKED, S7 PARTIAL, S8 ACTIVE. Kolejny etap G703 FUSE binary deltas i S6 TWRP read-only identity. GitHub wyłącznie właściciel.
