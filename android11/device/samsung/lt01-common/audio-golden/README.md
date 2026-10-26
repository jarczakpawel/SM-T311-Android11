# SM-T311 pinned audio HAL (G507)

This directory's Makefile deliberately selects the **physically verified** Android 11 WM1811 V191 HAL instead of a newer, unapproved experimental HAL. Before building the private image, provision `audio.primary.lt013g.so` from the owner's authorized device or private archive. The installed known-good Android 30 ARM32 ELF SHA-256 is:

`636743a098acb4ab31bad39adb3c46875c461d4b313f5fddb6c2106318224cfa`

Always compare the provisioned ELF's hash first. Preserve licensing/provenance and do not publish this binary merely by cloning or committing the source repository. Do not replace it by recompiling older/newer HAL branches without a full speaker, mic, headset, calls, ringtone, codec and AudioFlinger regression.

The G507 private source file on its Debian host is `artifacts/SM-T311-ALSA-WM1811-PERIOD-LATENCY-20261012/audio.primary.lt013g.LIVE-ORIGINAL.so`, outside this source repo.
