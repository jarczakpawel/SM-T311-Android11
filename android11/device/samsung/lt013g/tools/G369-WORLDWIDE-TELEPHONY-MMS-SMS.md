# SM-T311 G369 — Worldwide carrier APN, stock Samsung voice and SMS/MMS acceptance

## Global release behavior
The ROM must support any network whose physical GSM/UMTS bands and service remain available, not hardcode Plus, PL 26001, country, MMSC or credentials.
Android AOSP DcTracker already applies SIM MCC/MNC, MVNO, APN type, data RAT, set and enabled filters. When several MVNOs share one MCC/MNC the default database order can prioritize an APN from a different brand. Actual new Plus SIM selected GaduAIR initially instead of plus.
New generic source frameworks/opt/telephony/src/java/com/android/internal/telephony/dataconnection/CarrierApnPriority.java stably reorders ALREADY-VALID APNs using EF-SPN from SIM and normalized entry name; Unicode NFKC, Locale.ROOT, boundary-safe matching. Never sets preferred APN or deletes fallbacks, ignores empty/ambiguous SPN. DcTracker.java invokes it only when mPreferredApn==null. User preferred APN always wins. No network/CP/SIM writes.
Host script device/samsung/lt013g/tools/test_g369_global_apn_priority.sh compiles ACTUAL helper using javac; 28 assertions PASS for Plus, Claro, Orange, Safaricom, Telkom, O2, absent SPN and MVNO boundary conditions.
Android11 m -j4 telephony-common job a33bb8751fb2 finished EXIT0 incl ART boot classpath compilation. NOT DEPLOYED: telephony-common.jar belongs to Android ART boot classpath. Blind single-JAR replacement without coherent boot*.art/oat/vdex and framework parity risks boot failure.

## Live Plus physical APN and separate PDP failure
Current Plus SIM operator SPN=Plus SIM MCC/MNC=26001, currently subId4. Existing stock database has Plus INTERNET APN plus id2728 (default,supl) and Plus MMS APN mms id2729 with MMSC http://mms.plusgsm.pl:8002 proxy 212.2.96.16 port8080. Preferred APN was missing, and Android tried GaduAIR internet.gadu-gadu.pl repeatedly. Through standard Android TelephonyProvider content URI, preferred subId4 was changed to Plus INTERNET id2728; verified normal preference. This is current SIM configuration, NOT ROM hardcode.
RIL log after setting now sends 7-argument SETUP_DATA_CALL APN=plus correctly; OEM Samsung original RIL_register version7 physically observed. Vendor responses DataCallResponse cause=14 (Android DataFailCause.NAS_SIGNALLING), no assigned data interface, mDataConnectionState=0. So APN correction confirmed but Internet DATA NOT FIXED. Investigate PDP signaling/provisioning, never fabricate success. Voice IN_SERVICE UMTS does not prove packet data.
MMS apps com.android.mms.service and com.android.messaging present, Android TelephonyProvider stock Plus MMS APN stored; MMS requires working packet connection and correct MMSC transaction. MMS delivery NOT VERIFIED. SMS separate: prior user-confirmed SMS reached recipient but Samsung OEM SEND_SMS response timed out 120seconds and Android marked FAILED. No fake ACK or resend.

## Original OEM Samsung 7.1.2 voice behavior actually verified
Stock libsec-ril.so G350 private binary (only 2-byte mutex patch, live SHA0a673993...) THUMB disassembly of CallManager::OnCallIncoming:
  movw r1, #0x3fa = 1018 RIL_UNSOL_CALL_RING; call SecRil::UnsolicitedResponse
  movw r1, #0x3e9 = 1001 RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED; call SecRil::UnsolicitedResponse.
Both are STANDARD Android RIL IDs. Host source disassembly test device/samsung/lt013g/tools/test_g369_stock_call_events.py PASS, verifies exact order and two OEM callback invocations. Never blindly remap undocumented vendor unsolicited IDs 11003 11010 11017 11022 to 1018 or 1001.
User from second phone heard ringback but tablet did NOT display INCOMING; outgoing RIL DIAL SUCCESS, DIALING and ALERTING decoded correctly, no ACTIVE/CONNECTED confirmed. Need real single user-initiated incoming call after G368 and correlate kernel read-only FMT counters + RIL 1001/1018 + GET_CURRENT_CALLS INCOMING + Telecom InCallService; no auto-dial/secret capture.

## Global hardware limits
Worldwide-compatible ROM does NOT add missing RF bands, LTE, VoLTE or service where 2G/3G has been shut down. Never promise universal physical availability.
Live G368 RIL PIN/PUK one-attempt fuse remains installed and Plus stays LOADED UMTS; NO PIN/PUK/SMS/test calls issued by assistant this stage. Full G367 image remains NOT FLASHABLE due kernel BOOT, SELinux permissive exceptions, OEM redistribution rights and older RIL than G368.

## Next
Build coherent telephony ART release with full bootclasspath consistency before any physical OTA. Research original OEM modem PDP NAS signaling error and SMS ACK and need owner-led one incoming call. Never overwrite existing user framework changes, never change modem firmware/EFS or issue SIM PIN/PUK. Update latest handoff, CURRENT/NEXT_TASK and two full DONE10/TODO10 lists.
