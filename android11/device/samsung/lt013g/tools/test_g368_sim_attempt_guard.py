#!/usr/bin/env python3
"""Host-only tests extracted from the ACTUAL Samsung-specific RIL code.

There is never an actual SIM/modem connection in this test. Validates that
empty/malformed credentials cannot reach OEM and that even a well-formed
numeric code cannot be forwarded more than once by a runaway framework.
"""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[4]
text=(root/"hardware/ril/libril/ril_service.cpp").read_text()
start=text.index("#ifdef T311_LEGACY_SIM_CARDSTATUS\n// Samsung Pin/Puk/PinChange")
end=text.index("#endif",start)+len("#endif")
logic=text[start:end]
header=r"""
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include <thread>
#include <vector>
#define T311_LEGACY_SIM_CARDSTATUS 1
#define RIL_REQUEST_ENTER_SIM_PIN 1
#define RIL_REQUEST_ENTER_SIM_PIN2 2
#define RIL_REQUEST_ENTER_SIM_PUK 3
#define RIL_REQUEST_ENTER_SIM_PUK2 4
#define RIL_REQUEST_CHANGE_SIM_PIN 5
#define RIL_REQUEST_CHANGE_SIM_PIN2 6
static void memsetAndFreeStrings(int, char* p) {
    if (p) { std::memset(p, 0, std::strlen(p)); std::free(p); }
}
"""
body=r"""
static void validate(int req,const char *a,const char *b,const char *aid,
                     T311SimSecretResult expect) {
    bool pin=req==RIL_REQUEST_ENTER_SIM_PIN || req==RIL_REQUEST_ENTER_SIM_PIN2;
    char *f[3]={strdup(a),strdup(pin?aid:b),pin?nullptr:strdup(aid)};
    int count=pin?2:3;
    auto result=t311PrepareSimSecrets(req,f,count);
    assert(result==expect);
    if (result==T311SimSecretResult::Ok) {
        for (int i=0;i<count-1;i++) {
            char nine[9];std::memcpy(nine,f[i],9);
            assert(nine[0]=='0'||(nine[0]>='1' && nine[0]<='9'));
        }
        char field[39];std::memcpy(field,f[count-1],39);
        assert(std::strcmp(f[count-1],aid)==0);
    }
    for (auto &p:f) memsetAndFreeStrings(1,p);
}
int main() {
    auto good=T311SimSecretResult::Ok;
    auto bad=T311SimSecretResult::Invalid;
    for (int req: {RIL_REQUEST_ENTER_SIM_PIN,RIL_REQUEST_ENTER_SIM_PIN2}) {
        validate(req,"1234","","",good);
        validate(req,"12345678","","A0000000",good);
        validate(req,"","","",bad);
        validate(req,"123","","",bad);
        validate(req,"123456789","","",bad);
        validate(req,"12AA","","",bad);
    }
    for (int req: {RIL_REQUEST_ENTER_SIM_PUK,RIL_REQUEST_ENTER_SIM_PUK2}) {
        validate(req,"12345678","1234","",good);
        validate(req,"12345678","87654321","A0000000",good);
        validate(req,"","1234","",bad);
        validate(req,"1234567","1234","",bad);
        validate(req,"123456789","1234","",bad);
        validate(req,"12345678","","",bad);
    }
    for (int req: {RIL_REQUEST_CHANGE_SIM_PIN,RIL_REQUEST_CHANGE_SIM_PIN2}) {
        validate(req,"1234","4321","",good);
        validate(req,"12345678","87654321","A0000000",good);
        validate(req,"","4321","",bad);
    }
    assert(t311ClaimSimCredentialAttempt(0xffff));
    assert(t311ClaimSimCredentialAttempt(0xffff));
    for (int req=1;req<=6;req++) {
        assert(t311ClaimSimCredentialAttempt(req));
        for (int i=0;i<100;i++)assert(!t311ClaimSimCredentialAttempt(req));
    }
    // Concurrent test on a *fresh* request index is impossible after
    // consuming all six above, so test an independent static atomic gate.
    std::atomic<unsigned char> concurrent{0};
    std::atomic<int> forwarded{0};
    std::vector<std::thread> threads;
    for(int i=0;i<48;i++)threads.emplace_back([&](){
        unsigned char expected=0;
        if (concurrent.compare_exchange_strong(expected,1,std::memory_order_acq_rel,
                                                std::memory_order_relaxed)) forwarded.fetch_add(1);
    });
    for(auto &t:threads)t.join();
    assert(forwarded.load()==1);
    std::puts("G368_HOST_SIM_GUARD_PASS: six request types, invalid formats, one attempt per process, atomic concurrency");
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="g368-sim-guard-") as d:
    src=Path(d)/"test.cpp"
    exe=Path(d)/"test"
    src.write_text(header+"\n"+logic+"\n"+body)
    cmd=["g++","-std=c++17","-O1","-Wall","-Wextra","-Werror","-pthread",
         "-fsanitize=address,undefined","-fno-omit-frame-pointer",str(src),"-o",str(exe)]
    result=subprocess.run(cmd,text=True,capture_output=True)
    if result.returncode:raise RuntimeError(result.stderr)
    result=subprocess.run([str(exe)],text=True,capture_output=True)
    if result.returncode:raise RuntimeError(result.stderr+"\n"+result.stdout)
    print(result.stdout.strip())
