#include <ui/Region.h>
#include <ui/Rect.h>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static unsigned long long nanos() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<unsigned long long>(t.tv_sec) * 1000000000ULL + t.tv_nsec;
}

int main(int argc, char** argv) {
    const int count = argc > 1 ? atoi(argv[1]) : 4000000;
    if (count < 1 || count > 30000000) return 1;
    android::Region region;
    volatile int checksum = 0;
    for (int i=0; i<100000; ++i) region.clear();
    const auto begin = nanos();
    for (int i=0; i<count; ++i) region.clear();
    const auto end = nanos();
    checksum += region.isEmpty() ? 1 : 0;
    std::printf("REGION_CLEAR iterations=%d ns_per_call=%.3f sum=%d total_ms=%.3f\n",
                count, static_cast<double>(end-begin)/count, checksum,
                static_cast<double>(end-begin)/1000000.0);
    const android::Rect r(3,4,100,200);
    const auto begin2 = nanos();
    for (int i=0; i<count; ++i) {
        region.set(r);
        region.clear();
    }
    const auto end2 = nanos();
    checksum += region.isEmpty() ? 1 : 0;
    std::printf("REGION_SET_CLEAR iterations=%d ns_per_pair=%.3f sum=%d total_ms=%.3f\n",
                count, static_cast<double>(end2-begin2)/count, checksum,
                static_cast<double>(end2-begin2)/1000000.0);
    return 0;
}
