#!/usr/bin/env python3
"""Test EXTRACTED, exact Android11 DataConnection /32 gateway helper in javac.

No real modem/network writes; model/iface/address checks are negative controls
against accidentally changing other Android users or APNs.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

def check(cond, reason):
    if not cond:
        raise AssertionError(reason)

def test(src, connectivity):
    start=src.index('    private static void addLt013gPdpGatewayHostRoute(')
    end=src.index('    private SetupResult setLinkProperties(',start)
    helper=src[start:end]
    check('addLt013gPdpGatewayHostRoute(response, linkProperties, gateway);' in src[end:],
          "helper is not invoked from live PDP LinkProperties")
    check(src.index('addLt013gPdpGatewayHostRoute(response, linkProperties, gateway);') <
          src.index('linkProperties.addRoute(new RouteInfo(null, gateway, null,',end),
          "gateway host route must precede default route")
    a=connectivity.index('private boolean updateRoutes(')
    b=connectivity.index('for (RouteInfo route : routeDiff.updated)',a)
    update=connectivity[a:b]
    check('if (route.hasGateway()) continue' in update and
          'if (!route.hasGateway()) continue' in update,
          "Android11 must install next-hop-independent routes first")

    java=r'''
import java.net.*;
import java.util.*;
public class G387HostRouteTest {
    public static class LinkAddress {
        InetAddress addr; int prefix;
        LinkAddress(String host,int p) throws Exception {
            addr=InetAddress.getByName(host); prefix=p;
        }
        InetAddress getAddress(){return addr;}
        int getPrefixLength(){return prefix;}
    }
    public static class DataCallResponse {
        String iface; List<LinkAddress> addrs;
        DataCallResponse(String i,LinkAddress... a){
            iface=i; addrs=Arrays.asList(a);
        }
        String getInterfaceName(){return iface;}
        List<LinkAddress> getAddresses(){return addrs;}
    }
    public static class RouteInfo {
        InetAddress host; String iface; boolean hasGateway;
        static RouteInfo makeHostRoute(InetAddress host,String iface){
            RouteInfo r=new RouteInfo();r.host=host;r.iface=iface;
            r.hasGateway=false;return r;
        }
    }
    public static class LinkProperties {
        List<RouteInfo> routes=new ArrayList<>();
        void addRoute(RouteInfo route){routes.add(route);}
    }
    __HELPER__
    static int cases;
    static void expect(String title, boolean expected,String model,String iface,
            String gateway,LinkAddress... ips) throws Exception {
        android.os.Build.DEVICE=model;
        LinkProperties props=new LinkProperties();
        InetAddress gw=InetAddress.getByName(gateway);
        addLt013gPdpGatewayHostRoute(new DataCallResponse(iface,ips),props,gw);
        if (expected) {
            if (props.routes.size()!=1) throw new AssertionError(title+" expected1");
            RouteInfo route=props.routes.get(0);
            if (!route.host.equals(gw) || !route.iface.equals(iface) || route.hasGateway)
                throw new AssertionError(title+" wrong directly-connected host route");
        } else if(!props.routes.isEmpty()) throw new AssertionError(title+" unexpected route");
        cases++;
        System.out.println("PASS "+title);
    }
    public static void main(String[] ignored) throws Exception {
        LinkAddress ip32=new LinkAddress("10.33.0.2",32);
        expect("real SM-T311 /32 and separate IPv4 gateway",true,"lt013g","rmnet1","10.33.0.1",ip32);
        expect("other Samsung device untouched",false,"lt01wifi","rmnet1","10.33.0.1",ip32);
        expect("other cellular interface untouched",false,"lt013g","rmnet0","10.33.0.1",ip32);
        expect("conventional /24 route untouched",false,"lt013g","rmnet1","10.33.0.1",new LinkAddress("10.33.0.2",24));
        expect("same gateway as own address untouched",false,"lt013g","rmnet1","10.33.0.2",ip32);
        expect("zero gateway point-to-point untouched",false,"lt013g","rmnet1","0.0.0.0",ip32);
        expect("IPv6 gateway untouched",false,"lt013g","rmnet1","fe80::1",ip32);
        expect("dual address interface untouched",false,"lt013g","rmnet1","10.33.0.1",ip32,new LinkAddress("10.44.0.2",32));
        expect("IPv6 PDP address untouched",false,"lt013g","rmnet1","10.33.0.1",new LinkAddress("2001:db8::1",128));
        expect("missing PDP address untouched",false,"lt013g","rmnet1","10.33.0.1");
        if(cases!=10)throw new AssertionError("expected 10 tests");
        System.out.println("G387_REAL_EXTRACTED_JAVA_ROUTE_TESTS="+cases);
    }
}'''
    java=java.replace('__HELPER__',helper)
    with tempfile.TemporaryDirectory(prefix='g387-routes-') as temp:
        root=Path(temp)
        d=root/"android/os";d.mkdir(parents=True)
        (d/"Build.java").write_text('package android.os; public class Build { public static String DEVICE="lt013g"; }\n')
        file=root/"G387HostRouteTest.java";file.write_text(java)
        def build_and_run():
            cp=subprocess.run(['javac','-d',str(root),str(d/'Build.java'),str(file)],
                              capture_output=True,text=True)
            check(cp.returncode==0,'javac failed: '+cp.stderr)
            rp=subprocess.run(['java','-cp',str(root),'G387HostRouteTest'],
                              capture_output=True,text=True)
            return rp
        good=build_and_run()
        check(good.returncode==0,'base Java tests failed: '+good.stdout+good.stderr)
        check('G387_REAL_EXTRACTED_JAVA_ROUTE_TESTS=10' in good.stdout,
              "not all Java safety cases covered")
        print(good.stdout.strip())
        mutants=[
            ('missing_device_scope','!"lt013g".equals(android.os.Build.DEVICE)','false'),
            ('missing_iface_scope','!"rmnet1".equals(response.getInterfaceName())','false'),
            ('missing_pdp_32','pdp.getPrefixLength() != 32','false'),
            ('missing_zero_gateway','gateway.isAnyLocalAddress()','false'),
        ]
        for label,needle,replacement in mutants:
            check(needle in helper, 'missing guard '+label)
            file.write_text(java.replace(needle,replacement,1))
            mutated=build_and_run()
            check(mutated.returncode!=0,'mutant escaped tests: '+label)
            print("PASS negative mutation",label)
    print('G387_GATE PASS 10 real-Java cases, 4 fail-closed mutations, AOSP order verified')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--android',type=Path,required=True)
    opts=parser.parse_args()
    src=(opts.android/'frameworks/opt/telephony/src/java/com/android/internal/telephony/dataconnection/DataConnection.java').read_text()
    connectivity=(opts.android/'frameworks/base/services/core/java/com/android/server/ConnectivityService.java').read_text()
    test(src,connectivity)

if __name__=='__main__':main()
