#!/usr/bin/env python3
"""G378 host C++17 ASan/UBSan test of EXACT GPS binder setup implementation.

No physical ioctl, binderfs mount, chown, chmod or tablet file touched.
Builds extracted source with stat/chown/chmod/bind mocked; checks system Binder
never mutated, and dedicated device required by kernel config and neverallow.
"""
from pathlib import Path
import subprocess
import tempfile

device=Path(__file__).resolve().parents[1]
root=device.parents[2]
code=(device/"gps-v195/legacy_launcher.cpp").read_text()
begin=code.index("bool setupPrivateLegacyBinder(const std::string& root) {")
end=code.index("\npid_t startLegacyRuntimeService(",begin)
body=code[begin:end]
assert 'mount("binder"' not in body and 'ioctl(' not in body
assert '"/dev/binderfs/lt013g-gps"' in body
assert 'privateBefore.st_rdev == globalBefore.st_rdev' in body
assert 'insideAfter.st_rdev != privateAfter.st_rdev' in body
assert 'globalAfter.st_rdev != globalBefore.st_rdev' in body
assert 'privateAfter.st_gid != kGpsGid' in body
assert 'chmod(source.c_str(), 0660)' in body
assert 'detach(root + "/dev/binderfs/binder");' in code
assert 'CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder,lt013g-gps"' in (
    Path("/home/debian/Projekty/samsung/kernel/arch/arm/configs/sm_t311_android11_defconfig").read_text()
)
# Replace only FUNCTION call sites. Do not corrupt "struct stat" declarations.
body=body.replace("stat(","mockstat(").replace("chown(","mockchown(").replace("chmod(","mockchmod(")
body=body.replace("bindMount(","mockbindMount(").replace("detachRuntimeMounts(","mockdetachRuntimeMounts(")
h=r"""
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/sysmacros.h>
constexpr uid_t kSystemUid=1000;
constexpr gid_t kGpsGid=1021;
enum { SRC=1, HOST=2, JAIL=3 };
static bool havePrivate=true,independent=true,bindSuccess=true,chownSuccess=true,chmodSuccess=true;
static bool hostTamper=false,afterWrong=false,insideBad=false;
static bool jailBound=false,sysChanged=false;
static int binds=0,detaches=0,chowns=0,chmods=0;
static uid_t owner=0;static gid_t group=0;static mode_t permissions=0600;
static int which(const char*p) {
 std::string s(p);
 if(s=="/dev/binderfs/lt013g-gps")return SRC;
 if(s=="/dev/binderfs/binder")return HOST;
 if(s=="/test/jail/dev/binderfs/binder")return JAIL;
 assert(false && "unexpected binder path");return -1;
}
static int mockstat(const char*p,struct stat*st) {
 int n=which(p);if(n==SRC&&!havePrivate){errno=ENOENT;return -1;}
 memset(st,0,sizeof(*st));st->st_mode=S_IFCHR|0600;
 st->st_rdev=makedev(240,n==HOST||n==JAIL&&!jailBound?1:(independent?4:1));
 st->st_ino=(n==HOST&&hostTamper&&jailBound)?101:100;
 if(n==SRC){st->st_mode=S_IFCHR|permissions;st->st_uid=owner;st->st_gid=group;}
 if(n==JAIL&&jailBound){
    st->st_rdev=makedev(240,afterWrong?55:4);
 }
 if(n==JAIL&&insideBad)st->st_mode=S_IFREG;
 return 0;
}
static int mockchown(const char*p,uid_t uid,gid_t gid) {
 assert(which(p)==SRC);
 ++chowns;
 if(!chownSuccess){errno=EPERM;return -1;}
 owner=uid;group=gid;return 0;
}
static int mockchmod(const char*p,mode_t m) {
 assert(which(p)==SRC);++chmods;
 if(!chmodSuccess){errno=EPERM;return -1;}
 permissions=m;return 0;
}
static bool mockbindMount(const std::string &src,const std::string&dst){
 assert(which(src.c_str())==SRC && which(dst.c_str())==JAIL);
 ++binds;if(!bindSuccess){errno=EACCES;return false;}jailBound=true;return true;
}
static void mockdetachRuntimeMounts(const std::string&root){
 assert(root=="/test/jail");++detaches;
}
#define ALOGE(...) ((void)0)
#define ALOGI(...) ((void)0)
"""
tail=r"""
static void reset(){
 havePrivate=independent=bindSuccess=chownSuccess=chmodSuccess=true;
 hostTamper=afterWrong=insideBad=jailBound=sysChanged=false;
 binds=detaches=chowns=chmods=0;owner=group=0;permissions=0600;
}
int main(){
 reset();assert(setupPrivateLegacyBinder("/test/jail"));
 assert(jailBound && owner==1000 && group==1021 && permissions==0660);
 assert(chowns==1&&chmods==1&&binds==1&&detaches==0&&!sysChanged);
 reset();havePrivate=false;
 assert(!setupPrivateLegacyBinder("/test/jail"));
 assert(!jailBound && chowns==0);
 reset();independent=false;
 assert(!setupPrivateLegacyBinder("/test/jail"));
 assert(chowns==0&&!jailBound);
 reset();insideBad=true;
 assert(!setupPrivateLegacyBinder("/test/jail"));
 assert(chowns==0&&!jailBound);
 reset();chownSuccess=false;
 assert(!setupPrivateLegacyBinder("/test/jail"));
 assert(!jailBound);
 reset();chmodSuccess=false;
 assert(!setupPrivateLegacyBinder("/test/jail"));
 assert(!jailBound);
 reset();bindSuccess=false;
 assert(!setupPrivateLegacyBinder("/test/jail"));
 assert(binds==1&&!jailBound);
 reset();afterWrong=true;
 assert(!setupPrivateLegacyBinder("/test/jail"));
 assert(detaches==1);
 reset();hostTamper=true;
 assert(!setupPrivateLegacyBinder("/test/jail"));
 assert(detaches==1);
 std::cout<<"G378_BINDER_ISOLATION_CXX_HOST_PASS cases=9 global_mutations=0 kernel_device_required=1"<<std::endl;
}
"""
with tempfile.TemporaryDirectory(prefix="g378-binder-") as folder:
    src=Path(folder)/"test.cpp";exe=Path(folder)/"test"
    src.write_text(h+body+tail)
    run=subprocess.run(["c++","-std=c++17","-O1","-g","-Wall","-Wextra","-Werror",
       "-Wno-parentheses","-fsanitize=address,undefined","-fno-omit-frame-pointer",
       str(src),"-o",str(exe)],capture_output=True,text=True)
    if run.returncode:raise RuntimeError("BUILD\n"+run.stderr)
    output=subprocess.run([str(exe)],capture_output=True,text=True)
    if output.returncode:raise RuntimeError("TEST\n"+output.stdout+"\n"+output.stderr)
    print(output.stdout.strip())
