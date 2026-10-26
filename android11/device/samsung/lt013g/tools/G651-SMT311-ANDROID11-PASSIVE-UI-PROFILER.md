# SM-T311 Android 11: passive frame timing evidence for optimization

This nonmutating Python tool is kept IN the device source repository so it
will be included when the owner publishes Android sources on GitHub.
It parses captured dumpsys gfxinfo framestats for the current SM-T311.

Capture a read-only source snapshot when the user is not interacting with
the tablet:

    adb shell dumpsys gfxinfo com.android.systemui framestats > systemui-framestats.txt
    python3 device/samsung/lt013g/tools/g651_android11_framestats_readonly.py systemui-framestats.txt

For a causal pre-/post-optimization comparison, capture equal UI actions,
identical orientation, screen brightness, clocks/thermal, and sufficient
valid frames. SurfaceFlinger/HWC timestamps and app RenderThread should be
captured together. Do not equate cumulative gfxinfo "janky" percentage
with real-time FPS or a steady-state scrolling benchmark.

G651 actual passive diagnostic baseline: 113 valid SystemUI mixed-ViewRoot
records, end-to-end frame p50 22.617ms/p95 100.195ms,
SyncStart->SwapBuffers p95 35.267ms, DequeueBufferDuration p95 27.200ms.
No program/script here wakes screen, resets counters, launches apps,
disables SELinux, sets vendor HWC props, or sounds ringtone.

Keep G243 EGL damage 2N-to-N fix, Mesa26.2.4 and original fence ownership.
Old DMC bus ratio60 and sorted-z HWC experiments caused visible flicker;
do NOT auto-enable additional WIN4 overlay or change clocks based only on
cumulative graphics statistics. Production HAL has mixed 2 CLIENT + 1
DEVICE layers and zero Validate/Present errors in last read-only snapshot.

Full physical PMU/graphics research is in the Linux7.3 project ledger
G651_SM_T311_PMU_FOLIO_VS_ANDROID_UI_20261020.md, and its kernel
selftest source is in tools/testing/selftests/smt311.

GitHub pushes are reserved for the project owner.
