# SM-T311 G397 — reproduced high-FD AddressSanitizer stack overwrite in Broadcom SUPL probe

2026-10-17. Priority GPS first. Narrow modern GNSS proxy native memory-safety repair, independent from G396's genuine HIDL1.0 unknown-IP callback correction.

In original G396 vendor device/samsung/lt013g/gps-v195/gps_proxy.c, v398_probe_default_supl used fd_set wfds / FD_SET(fd, &wfds) / select(fd + 1,...) after socket(...). POSIX select fd_set is a FIXED SIZE stack bitset (normally FD_SETSIZE=1024), but a GNSS service can theoretically receive any available process FD. No guard rejected fd >= FD_SETSIZE. In a bounded host C harness compiling **exact G396 Git commit 89cfc09 function**, with synthetic socket() returning 2048 and connect() returning EINPROGRESS, clang AddressSanitizer detected **stack-buffer-overflow at FD_SET**. No physical kernel/device driver, actual SIM/network, real socket, DNS or GNSS operation used. This is code correctness and tested reproducer, NOT evidence such high FDs occurred during physical G379.

Minimal source change uses pollfd single slot POLLOUT, poll(..., 1, 700), preserving existing 700ms timeout; proper SO_ERROR/getsockopt, EINPROGRESS, failure/close handling preserved. C API poll is allowed for arbitrarily numbered FDs; no hard-coded fake IP, firmware kernel, OEM gpsd, Android framework or system route changes. No extra daemon threads.

Test tools/test_g397_supl_highfd_asan.py:
- Reads actual current gps_proxy.c real function from source without recreating algorithms; compiles with clang -fsanitize=address using fake socket/connect/poll and no actual network.
- --reproduce-actual-g396-unsafe reads exact historical G396 Git89cfc09:gps-v195/gps_proxy.c and MUST detect a real ASan stack-buffer-overflow at FD2048; failure to reproduce fails test.
- Safe current code passes real isolated C with ASan 9 functional branches: FD2048 successful POLLOUT; FD3000 asynchronous ECONNREFUSED; FD6550 timeout; FD2500 interrupted; FD2050 ENETUNREACH; direct connect success; no-IP port guard; zero-port guard; socket creation error. Per-case checks that fd closed exactly once and fake socket never sends real packets.
- Existing G396 unknown-IP JNI-null callback suite remains 10 functional+10 negative mutants PASS. G330-G333 CP transport races previously tested and no source changed there.

Status: source patch/host tests only until rebuilt and full signed cumulative G397 verified. Existing G396 testkey full signed ZIP SHA a6fcf6db0bb15f64df46e5cb3bd48f43e383fdd00bc80fd63a575dc0695a507d physically staged, untouched and NOT INSTALLED. Any newer G397 should be full guarded non-A/B image with G387/G388/G389/G396 retained, read-only real TWRP preflight and owner-controlled install; avoid partial binary pushes, blind recovery reboot, modem PIN/PUK or network operations. Public GitHub/licensing last, private history unchanged and no custom ADB plugin reinstall.
