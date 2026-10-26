# G717: cancel obsolete SUPL work BEFORE opening a TCP probe (2026-10-21)

## Actual runtime C source improvement

`gps-v195/gps_proxy.c:v398_agps_request_worker()` previously called
`v398_probe_default_supl()` (a nonblocking TCP connect, maximum 700ms
poll) before inspecting `generation == g_v398_agps_generation`.
A framework RELEASE or newer SUPL request could already have made the
worker obsolete before that thread was scheduled, causing unnecessary
socket traffic and waiting even when the old request should be canceled.

G717 checks current request generation and all three active data states
under `g_state_lock` **before** IP conversion and the TCP probe. If
obsolete, it frees the work and returns immediately, with no TCP call.
The **existing second check** inside `g_v398_link_lock`, after poll,
is intentionally preserved: a request can be canceled while TCP is
pending. There is no new lock-order inversion (early probe uses only
`g_state_lock`, releases it before taking `g_v398_link_lock`).
There is no change to healthy requests, GNSS power, active GPS location
sessions, XTRA injection, SUPL fallback, or data-open/close sequencing.

## Verified

- `python3 tools/test_g717_supl_stale_preprobe.py` extracts the REAL
  guard from `gps_proxy.c`, compiles a minimal pthread C harness,
  and exercises 5/5 valid/stale/request-state cases PASS. The test also
  asserts the post-probe generation guard still exists.
- Full Android 11 module rebuild:
  `source build/envsetup.sh; lunch lineage_lt013g-userdebug; m -j4 gps.default.lt013g.v195`.
  **PASS**, output ELF32 ARM, 36,032 bytes.
- New built GPS SHA256:
  `c85b7b9e28f6bc8ac35d94d9b56011efde66b7d555dab1e7f7e3a5d90e7215a8`.
- Deployed ONLY INACTIVE to `/data/local/tmp/g717-gps-candidate.so` by
  ADB `shell`; tablet SHA matches build. Running
  `/vendor/lib/hw/gps.default.so` remains G621-era SHA256
  `102b7ea8f6536baa6a7db7fc8a946f5d29f49e387519bc7d8580489a2f671df2`.
  GNSS Android and legacy bridge services remain running.
- No `/vendor` writes, GNSS service restart, recovery, BOOT flash,
  partition access, backup, PIN, phone, audio, camera, forced GPS fix
  or new location request.

## Scope

This removes a provably obsolete SUPL connection attempt; it does
**not** prove better TTFF/fix or fix the modem/GNSS assistance path.
Current `dumpsys location` had GPS provider enabled but no active GPS
request and no new fix or SV status. TTFF must be measured outdoors
on an owner-attended device when this library is deliberately deployed.

## Quick Settings correction

The earlier claim that Android was missing the Location tile is
misleading: physical `settings get secure sysui_qs_tiles` already
contains `location`, the device overlay sets it in the default list,
and AOSP/Lineage `QSFactoryImpl` constructs `LocationTile`.
This does **not** establish whether it is visibly rendered correctly.
Do not add duplicate tile or silently rewrite the user's tile order.

## Next

Prepare a safe GNSS-only test package / owner-attended install and
validate SUPL signaling and actual GNSS TTFF with real network and
outdoor visibility. Do not mistake an inactive file under
`/data/local/tmp` for a deployed /vendor HAL.
