#!/usr/bin/env python3
"""Compile and run the ACTUAL private Broadcom GPS XML transformation code.

Mocks only Android property/logging, uses a TEMPORARY copy of original CM14
gps.xml. Never edits stock GPS tar, physical tablet, Android preferences, or SIM.
"""
from pathlib import Path
import subprocess
import tempfile

base=Path(__file__).resolve().parents[1]
src=(base/"gps-v195/legacy_launcher.cpp").read_text()
start=src.index("void applyGpsLogMode(")
end=src.index("bool setupRootfs(",start)
original=src[start:end]
assert 'IgnoreJniTime=\\"true\\"' in original
reference=Path("/home/debian/Projekty/samsung/reference/android_device_samsung_lt013g-cm14.1/configs/gps.xml")
stock=reference.read_text()
assert 'IgnoreJniTime="true"' in stock and 'Log="JAVA"' in stock
head=r'''
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <fstream>
#include <filesystem>
#define PROP_VALUE_MAX 92
static bool g_enable_logging=false;
static int __system_property_get(const char *key,char *value){
 assert(std::string(key)=="persist.vendor.lt013g.gps.filelog");
 const char *v=g_enable_logging?"1":"0";
 std::strcpy(value,v);return 1;
}
#define ALOGI(...) do {} while(0)
#define ALOGW(...) do {} while(0)
'''
tail=r'''
static void put(const std::string &path,const std::string &str){
 std::ofstream out(path,std::ios::binary|std::ios::trunc);
 assert(out);out<<str;assert(out);
}
static std::string get(const std::string &path){
 std::ifstream in(path,std::ios::binary);assert(in);
 return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
int main(int argc,char **argv){
 assert(argc==3);
 const std::string root=argv[1];
 const std::string original_path=argv[2];
 const std::string xml_path=root+"/system/etc/gps.xml";
 std::filesystem::create_directories(root+"/system/etc");
 const std::string stock=get(original_path);
 assert(stock.find("IgnoreJniTime=\"true\"")!=std::string::npos);
 put(xml_path,stock);
 g_enable_logging=false;
 applyGpsLogMode(root);
 std::string converted=get(xml_path);
 assert(converted.find("IgnoreJniTime=\"false\"")!=std::string::npos);
 assert(converted.find("IgnoreJniTime=\"true\"")==std::string::npos);
 assert(converted.find("Log=\"JAVA\"")!=std::string::npos);
 assert(converted.find("Log=\"FILE\"")==std::string::npos);
 assert(get(original_path)==stock);
 puts("PASS original Broadcom GPS XML: UTC accepted in PRIVATE copy, stock untouched");

 // Re-extract original tar config into private directory and enable filelog.
 put(xml_path,stock);
 g_enable_logging=true;
 applyGpsLogMode(root);
 converted=get(xml_path);
 assert(converted.find("IgnoreJniTime=\"false\"")!=std::string::npos);
 assert(converted.find("LogEnabled=\"true\" Log=\"FILE\"")!=std::string::npos);
 assert(get(original_path)==stock);
 puts("PASS original launcher filelog opt-in preserves UTC assistance and immutable stock");

 applyGpsLogMode(root);
 assert(get(xml_path)==converted);
 puts("PASS repeated transformation leaves correct existing config intact");

 put(xml_path,"<gll><configuration IgnoreJniTime=\"absent\"/></gll>\n");
 g_enable_logging=false;
 applyGpsLogMode(root);
 assert(get(xml_path)=="<gll><configuration IgnoreJniTime=\"absent\"/></gll>\n");
 puts("PASS unexpected vendor XML is left unchanged instead of clobbered");
}
'''
with tempfile.TemporaryDirectory(prefix="g328-bcm4752-config-") as td:
    source=Path(td)/"gps_test.cpp";exe=Path(td)/"gps_test"
    source.write_text(head+original+tail)
    subprocess.run(["c++","-std=c++17","-O2","-Wall","-Wextra",
                    "-Werror",str(source),"-o",str(exe)],check=True)
    subprocess.run([str(exe),str(Path(td)/"private-jail"),str(reference)],check=True)
