# SM-T311: PRODUCT_COPY_FILES defaults to 0644 even for ELF executables.
# The legacy GNSS rootfs binds these immutable vendor files as executable code.

[vendor/etc/lt013g-gps-legacy/immutable/system/bin/gpsd]
mode: 0755
user: AID_ROOT
group: AID_ROOT
caps: 0

[system/vendor/etc/lt013g-gps-legacy/immutable/system/bin/gpsd]
mode: 0755
user: AID_ROOT
group: AID_ROOT
caps: 0

[vendor/etc/lt013g-gps-legacy/immutable/system/bin/linker]
mode: 0755
user: AID_ROOT
group: AID_ROOT
caps: 0

[system/vendor/etc/lt013g-gps-legacy/immutable/system/bin/linker]
mode: 0755
user: AID_ROOT
group: AID_ROOT
caps: 0

[vendor/etc/lt013g-gps-legacy/immutable/system/bin/mksh]
mode: 0755
user: AID_ROOT
group: AID_ROOT
caps: 0

[system/vendor/etc/lt013g-gps-legacy/immutable/system/bin/mksh]
mode: 0755
user: AID_ROOT
group: AID_ROOT
caps: 0

[vendor/etc/lt013g-gps-legacy/immutable/system/bin/sensorservice]
mode: 0755
user: AID_ROOT
group: AID_ROOT
caps: 0

[system/vendor/etc/lt013g-gps-legacy/immutable/system/bin/sensorservice]
mode: 0755
user: AID_ROOT
group: AID_ROOT
caps: 0

[vendor/etc/lt013g-gps-legacy/immutable/system/bin/servicemanager]
mode: 0755
user: AID_ROOT
group: AID_ROOT
caps: 0

[system/vendor/etc/lt013g-gps-legacy/immutable/system/bin/servicemanager]
mode: 0755
user: AID_ROOT
group: AID_ROOT
caps: 0
