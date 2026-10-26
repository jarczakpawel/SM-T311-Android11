# SM-T311 Android 11 minimal parser foundation

This tree is deliberately minimal.

It proves that LineageOS 18.1 can discover and parse a clean 32-bit SM-T311
product without importing incompatible Android 7 build rules.

It does not build or flash boot.img. The audited Linux 6.12 kernel, source DTB,
modules, firmware, init, SELinux and HALs are introduced in later controlled
stages.
