#!/usr/bin/env bash
# Host-only regression of ACTUAL global AOSP DcTracker carrier readiness policy.
# No SIM, RIL, phone numbers, modem or private manufacturer code is used.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
helper="$root/frameworks/opt/telephony/src/java/com/android/internal/telephony/dataconnection/CarrierConfigReadinessRetry.java"
tracker="$root/frameworks/opt/telephony/src/java/com/android/internal/telephony/dataconnection/DcTracker.java"
test -f "$helper" && test -f "$tracker"
grep -q 'scheduleCarrierConfigReadinessRetry();' "$tracker"
grep -q 'mSimState == TelephonyManager.SIM_STATE_LOADED' "$tracker"
grep -q 'removeMessages(DctConstants.EVENT_CARRIER_CONFIG_CHANGED);' "$tracker"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
mkdir -p "$work/com/android/internal/telephony/dataconnection"
cat > "$work/com/android/internal/telephony/dataconnection/CarrierConfigReadinessRetryTest.java" <<'EOF'
package com.android.internal.telephony.dataconnection;
public final class CarrierConfigReadinessRetryTest {
    private static int assertions;
    private static void eq(boolean actual,boolean want) {
        assertions++;
        if(actual!=want)throw new AssertionError("expected="+want+" actual="+actual);
    }
    public static void main(String[] args) {
        eq(CarrierConfigReadinessRetry.shouldSchedule(true,false,0),true);
        eq(CarrierConfigReadinessRetry.shouldSchedule(true,false,14),true);
        eq(CarrierConfigReadinessRetry.shouldSchedule(true,false,15),false);
        eq(CarrierConfigReadinessRetry.shouldSchedule(true,false,16),false);
        eq(CarrierConfigReadinessRetry.shouldSchedule(false,false,0),false);
        eq(CarrierConfigReadinessRetry.shouldSchedule(false,true,0),false);
        eq(CarrierConfigReadinessRetry.shouldSchedule(true,true,0),false);
        eq(CarrierConfigReadinessRetry.shouldSchedule(true,true,14),false);
        eq(CarrierConfigReadinessRetry.shouldSchedule(true,false,-1),false);
        eq(CarrierConfigReadinessRetry.RETRY_DELAY_MS==2000,true);
        eq(CarrierConfigReadinessRetry.MAX_ATTEMPTS==15,true);
        for (int i=0;i<CarrierConfigReadinessRetry.MAX_ATTEMPTS;i++)
            eq(CarrierConfigReadinessRetry.shouldSchedule(true,false,i),true);
        eq(CarrierConfigReadinessRetry.shouldSchedule(true,false,
                CarrierConfigReadinessRetry.MAX_ATTEMPTS),false);
        System.out.println("G374_CARRIER_CONFIG_READY_HOST_PASS assertions="+assertions
                +" max_retries=15 delay_ms=2000 carrier_neutral=true");
    }
}
EOF
javac -d "$work" "$helper"  "$work/com/android/internal/telephony/dataconnection/CarrierConfigReadinessRetryTest.java"
java -cp "$work" com.android.internal.telephony.dataconnection.CarrierConfigReadinessRetryTest
