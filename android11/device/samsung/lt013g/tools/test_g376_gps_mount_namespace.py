#!/usr/bin/env python3
"""Compile and test extracted REAL GPS G376 mount-init C++ block (host only).

No actual mount() syscall, file writes to Android devices, SIM, USB or tablet.
Mock the existing recursive-bind helper and stat, exercise success and errors.
"""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/"gps-v195/legacy_launcher.cpp").read_text()
begin=source.index("    // G376: original Samsung/KitKat GPS expects /proc, /sys")
end=source.index("    if (!bindMount(systemGps, root + \"/data/system/gps\")",begin)
body=source[begin:end]
# Stub only stat() call, not the C++ 'struct stat' declaration.
mocked_body=body.replace('stat(selinuxStatus.c_str(), &selinuxStatusSt)',
                         'mockstat(selinuxStatus.c_str(), &selinuxStatusSt)')
assert mocked_body!=body
assert "mount(\"proc\"" not in body and "mount(\"sysfs\"" not in body
assert "mount(\"selinuxfs\"" not in body
assert 'bindMountRecursive("/proc", root + "/proc")' in body
assert 'bindMountRecursive("/sys", root + "/sys")' in body
assert 'root + "/sys/fs/selinux/status"' in body
assert 'G376_KERNEL_FS_BIND=PASS' in body
assert "namespace mnt" in (root/"gps-v195/init.lt013g.gps-legacy.rc").read_text()
assert 'setupPrivateLegacyBinder(root)' in source

header=r'''
#include <cassert>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/stat.h>
static bool procok=true,sysok=true,statusok=true;
static int detaches=0,bindings=0,statcalls=0;
static bool bindMountRecursive(const std::string &from,const std::string &to) {
    ++bindings;
    assert((from=="/proc" && to=="/test/root/proc") ||
           (from=="/sys" && to=="/test/root/sys"));
    return from=="/proc" ? procok: sysok;
}
static void detachRuntimeMounts(const std::string &root) {
    assert(root=="/test/root");
    ++detaches;
}
static int mockstat(const char *path,struct stat *out) {
    ++statcalls;
    assert(std::string(path)=="/test/root/sys/fs/selinux/status");
    if(!statusok){errno=ENOENT;return -1;}
    std::memset(out,0,sizeof(*out));
    out->st_mode=S_IFREG|0444;
    return 0;
}
#define ALOGE(...) ((void)0)
#define ALOGI(...) ((void)0)
bool actualSourceGpsKernelMountSetup(const std::string&root){
'''
tail=r'''
    return true;
}
static void reset() {
    procok=sysok=statusok=true;detaches=bindings=statcalls=0;
}
int main(){
    reset();
    assert(actualSourceGpsKernelMountSetup("/test/root"));
    assert(bindings==2 && detaches==0 && statcalls==1);
    reset();procok=false;
    assert(!actualSourceGpsKernelMountSetup("/test/root"));
    assert(bindings==1 && detaches==1 && statcalls==0);
    reset();sysok=false;
    assert(!actualSourceGpsKernelMountSetup("/test/root"));
    assert(bindings==2 && detaches==1 && statcalls==0);
    reset();statusok=false;
    assert(!actualSourceGpsKernelMountSetup("/test/root"));
    assert(bindings==2 && detaches==1 && statcalls==1);
    std::cout<<"G376_GPS_STOCK_NAMESPACE_BIND_TESTS_PASS cases=4 physical_mounts=0"<<std::endl;
}
'''
with tempfile.TemporaryDirectory(prefix="g376-gps-mount-") as td:
    test=Path(td)/"test.cpp"
    exe=Path(td)/"test"
    test.write_text(header+mocked_body+tail)
    built=subprocess.run(["c++","-std=c++17","-O1","-g","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",
                          "-fno-omit-frame-pointer",str(test),"-o",str(exe)],
                          capture_output=True,text=True)
    if built.returncode:raise RuntimeError("COMPILER\n"+built.stderr)
    result=subprocess.run([str(exe)],capture_output=True,text=True)
    if result.returncode:raise RuntimeError("EXECUTION\n"+result.stderr+"\n"+result.stdout)
    print(result.stdout.strip())
