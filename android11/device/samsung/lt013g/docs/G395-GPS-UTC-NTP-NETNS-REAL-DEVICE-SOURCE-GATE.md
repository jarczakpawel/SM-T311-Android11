# SM-T311 G395: genuine GNSS shared network namespace and UTC assistance path

2026-10-17. GPS-FIRST source+read-only physical audit. LIVE G379 Android11 Enforcing Plus SIM LOADED UMTS, GNSS daemon running but GPS power0 idle. G389 cumulative signed guarded ROM unchanged and NOT INSTALLED.

## Physical netns finding (passive /proc/PID/ns/net readlink)
- Android init PID1: net:[4026531840]
- Android system_server PID371: net:[4026531840]
- vendor GPS launcher PID159: net:[4026531840]
- old Samsung/Broadcom gpsd PID902: net:[4026531840]
These actual simultaneously running processes share one Linux network namespace, NOT distinct jail networking. GPS mount/Binder isolation remains; no evidence of functional internet DNS/NTP/SUPL or outdoor 3D fix. PIDs and namespace numbers are ephemeral, not fixed settings.

## True selected OEM Broadcom private XML and time injection chain
Original immutable gps-v195/legacy-rootfs.tar contains stock system/etc/gps.xml with IgnoreJniTime=true, SuplEnable=true and SuplUseApn=false. Real GPS launcher extracts this immutable stock profile and executes applyGpsLogMode(root), rewriting the actual jailed system/etc/gps.xml to IgnoreJniTime=false before old GPS starts. This was already PHYSICALLY accepted in G386, not a new fix. A separate source gps-prod-v1.xml still says true but isn't the actual launched private file.

Android11 NtpTimeHelper refreshes through framework active Network and only feeds fresh NTP results; GnssLocationProvider.injectTime -> JNI gnssHal->injectTime -> HIDL1.0 mGnssIface->inject_time -> proxy LT013G_CMD_INJECT_TIME with 64-bit time_ms/reference_ms and int32 uncertainty_ms -> legacy API25 gpsd g_iface->inject_time. Reverse receiver time request LT013G_CB_REQUEST_UTC -> callback request_utc_time_cb -> HIDL gnssRequestTimeCb -> NtpTimeHelper. Proxy exposes GPS_CAPABILITY_ON_DEMAND_TIME. GPS launcher binds existing host dnsproxyd/fwmarkd/netd sockets into private mount namespace, no CLONE_NEWNET or setns path. This is source chain consistency; no physical delivered NTP injection accepted.

G386 real framework NTP cache null and 7 requestTime errors captured; G387 real cellular rmnet1 /32 and offlink gateway gave netd ENETUNREACH101. Narrow exact G387 mobile host-route source correction is included in signed G389, still UNINSTALLED. The live GPS receiver may not get network UTC or SUPL until networking works. Do NOT add an invented separate GNSS default route, broad SELinux exception, cold reset, APN or carrier intervention.

## G395 fail-closed regression suite
tools/test_g395_gps_utc_netns_bridge.py reads actual host Android11 Java/JNI/HIDL, private Broadcom proxy/server/launcher/IPC header and original immutable tar XML, plus bounded real passive 4-process namespace JSON with no IP, coordinates/NMEA, IMSI, ICCID, APN values or user info. Verifies all UTC links and G379 state. Rejects 18 deliberate source failures plus a synthetic different GPS network namespace = 19/19 PASS. No kernel/Android/stock blob source changes, no reboot/flash/build required.

GPS native V359 injection rc0, validated internet, NTP/SUPL and outdoor stable 3D lock remain untested. Next owner manually enters actual TWRP using keys, executes G390 non-writing preflight and separately supervises complete G389 full install, then real route and GPS acceptance. All 20 original IDs GPS-FIRST, GitHub/legal LAST. Latest full G390 private history transfer connector safety blocked; keep host copy and old G389 full tablet private, don't circumvent; no plugin reinstall.
