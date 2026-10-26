# G598 - pasywny audyt peryferiów SM-T311

`readonly_g598.sh` jest skryptem dla Androida 11 wykonywanym przez zwykłą powłokę ADB. Nie zmienia konfiguracji, nie uruchamia kamer, audio, GPS fix, połączeń ani testów wydajności. Nie odczytuje numerów SIM, identyfikatorów sieci, lokalizacji ani plików użytkownika.

Uruchomienie po skopiowaniu skryptu na urządzenie:

```sh
adb push tools/g598-silent-peripherals/readonly_g598.sh /data/local/tmp/g598-readonly.sh
adb shell sh /data/local/tmp/g598-readonly.sh
```

Dane `LOG_KERNEL_...` dotyczą ostatnich maksymalnie 2000 wpisów aktualnie dostępnego bufora, a `LOG_CRASH_FATAL_SIGNAL` maksymalnie 500 wpisów. Nie są licznikami błędów od początku działania urządzenia. `VM_...` i `CPUFREQ_TRANSITIONS` są licznikami skumulowanymi w danym uruchomieniu. `MTP_HOST_FILE_COPY=UNVERIFIED` nie oznacza nieprawidłowego działania MTP. `UNAVAILABLE` sygnalizuje brak dostępu do sysfs; nie oznacza błędu GPU, PMIC, magistrali ani zRAM.

Próbka fizyczna Android G591/kernel G597, 20 października 2026:
`boot_completed=1`, `7.3.0-pjarczak`, `Enforcing`, fingerprinty zgodne, cpboot/rild/GNSS running, USB MTP current/applied/configured, zRAM SwapTotal=786428 KiB, `LOG_KERNEL_DWC2_STOP_TIMEOUT=2`, `LOG_KERNEL_OPP_REJECT=0`, `LOG_KERNEL_DEVFREQ_EVENT_FAIL=0`, `LOG_KERNEL_FATAL=0`, `LOG_CRASH_FATAL_SIGNAL=0`. GPU/devfreq i algorytm kompresji zRAM w sysfs były niedostępne dla zwykłego UID ADB; nie zwiększono uprawnień ani nie zmieniono SELinux.

Dla testu transferu MTP na komputerze wymagany jest bezpośredni dostęp do sesji GVFS/Dolphin i urządzenia USB hosta, którego izolowana wtyczka workspace nie udostępnia. Akceptacja szyfrowanego ADB Wi-Fi wymaga zgody użytkownika na obecną sieć w SystemUI; tego skrypt nie omija.
