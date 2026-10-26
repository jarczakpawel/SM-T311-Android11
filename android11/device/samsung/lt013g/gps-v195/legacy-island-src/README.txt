Zrodla "wyspy sensorow" API25 (servicemanager + sensorservice w chroocie gpsd).
Skompilowane .so z tych zrodel SA JUZ w legacy-rootfs.tar
(system/lib/liblt013g-sensor-hw-loader-v415.so, liblt013g-servicemanager-selinux-off-v414.so,
system/lib/hw/sensors.{default,lt013g,smdk4x12}.so).
Ten katalog nie ma Android.bp celowo: nic tu sie nie buduje przy normalnym m/brunch.
Definicje modulow (gdyby trzeba bylo przebudowac rootfs) sa w sensor-island-modules.bp.txt.
