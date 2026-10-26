#!/usr/bin/env bash
# Standalone host regression for the actual Android11 Java APN-priority helper.
# No device, ICCID, account settings, network calls or SIM interaction.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
src="$root/frameworks/opt/telephony/src/java/com/android/internal/telephony/dataconnection/CarrierApnPriority.java"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/com/android/internal/telephony/dataconnection"
cat > "$work/com/android/internal/telephony/dataconnection/CarrierApnPriorityTest.java" <<'EOF'
package com.android.internal.telephony.dataconnection;
import java.util.*;
public final class CarrierApnPriorityTest {
    private static int assertions=0;
    private static void eq(Object a,Object b) {
        ++assertions;
        if(!Objects.equals(a,b))throw new AssertionError("want="+b+" got="+a);
    }
    private static void check(String spn,List<String> before,List<String> after,boolean changed) {
        ArrayList<String> a=new ArrayList<>(before);
        boolean result=CarrierApnPriority.moveBrandMatchesFirst(a,spn,x->x);
        eq(result,changed);
        eq(a,after);
    }
    public static void main(String[] argv) {
        eq(CarrierApnPriority.matchesBrand("Plus INTERNET","Plus"),true);
        eq(CarrierApnPriority.matchesBrand("PLUS MMS","plus"),true);
        eq(CarrierApnPriority.matchesBrand("ClaroNet","Claro"),false);
        eq(CarrierApnPriority.matchesBrand("O2 (data)","O2"),true);
        eq(CarrierApnPriority.matchesBrand("MÓVIL Internet","móvil"),true);
        eq(CarrierApnPriority.matchesBrand("Orange France","orange"),true);
        eq(CarrierApnPriority.matchesBrand("MobileNet","Mobile"),false);
        eq(CarrierApnPriority.matchesBrand("X internet","X"),false);
        eq(CarrierApnPriority.matchesBrand(null,"Telkom"),false);
        eq(CarrierApnPriority.matchesBrand("Etisalat",null),false);
        check("Plus",Arrays.asList("GaduAIR","Plus INTERNET","Carrefour Mova","Plus Backup"),
                Arrays.asList("Plus INTERNET","Plus Backup","GaduAIR","Carrefour Mova"),true);
        check("Claro",Arrays.asList("Vivo Internet","Claro Internet","Claro-MMS","Vivo Backup"),
                Arrays.asList("Claro Internet","Claro-MMS","Vivo Internet","Vivo Backup"),true);
        check("Orange",Arrays.asList("SFR Internet","Orange France","Orange MMS"),
                Arrays.asList("Orange France","Orange MMS","SFR Internet"),true);
        check("Safaricom",Arrays.asList("Airtel Internet","Safaricom Internet","MTN internet"),
                Arrays.asList("Safaricom Internet","Airtel Internet","MTN internet"),true);
        check("Telkom",Arrays.asList("Vodacom internet","MTN Internet"),
                Arrays.asList("Vodacom internet","MTN Internet"),false);
        check(null,Arrays.asList("Vivo","Claro"),Arrays.asList("Vivo","Claro"),false);
        check("o",Arrays.asList("Orange","O2"),Arrays.asList("Orange","O2"),false);
        check("Claro",Arrays.asList("Claro Internet","Claro MMS"),
                Arrays.asList("Claro Internet","Claro MMS"),false);
        check("Plus",Arrays.asList("PLUS","Plus Internet","PlusMMS"),
                Arrays.asList("PLUS","Plus Internet","PlusMMS"),true);
        System.out.println("G369_APN_GLOBAL_HOST_PASS assertions="+assertions+" brands=Plus,Claro,Orange,Safaricom,Telkom,O2");
    }
}
EOF
javac -d "$work" "$src" "$work/com/android/internal/telephony/dataconnection/CarrierApnPriorityTest.java"
java -cp "$work" com.android.internal.telephony.dataconnection.CarrierApnPriorityTest
