# SM-T311 proprietary blob publishing boundary

This device tree contains redistributable project **source code** and local-only
copies of original Samsung/Broadcom firmware. Build artifacts and stock binaries
are **not** automatically licensed for public redistribution merely because they
are used to build a community Android ROM.

## Local-only inputs (excluded from this device Git repository)

- `radio/prebuilt/`: original Samsung modem binaries, including
  `libsec-ril.so`, `t311-cbd`, and binary compatibility libraries.
- `prebuilt/`: local recovery image and other generated/stock boot artifacts.
- `gps-v195/legacy-rootfs.tar` and `gps-v195/legacy-rootfs-mutable.tar`:
  archived legacy API25 GPS runtime, including proprietary code.
- `gps-v195/immutable/`: extracted private GNSS libraries and firmware.

The `.gitignore` excludes these paths. The standalone
`vendor/samsung/lt013g/proprietary/` directory is outside the device tree and
was **not** itself a Git repository at the time of this audit. Do not blindly
include that directory in public releases either.

## Scope and unresolved work

The GPL-2.0 Linux kernel source tree is a separate work product; verify
kernel source attribution, any patches, and required notices before publishing.
The phone/audio and GNSS bridge sources have their own histories: check
authorship, imported code and applicable license obligations separately.

For every closed-source binary: identify exact origin, license/redistribution
permission and required notices, or give users a local extraction/build
workflow instead of bundling it in a public Git repository. The proprietary
blob rights have **not** been verified; P0-5 remains OPEN. Excluding files
from Git does not by itself authorize redistributing a prebuilt ROM or OTA.

No binary was removed from the working build inputs by this audit.
