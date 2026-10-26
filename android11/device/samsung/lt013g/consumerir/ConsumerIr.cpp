#include "ConsumerIr.h"

#include <fcntl.h>
#include <linux/lirc.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace android {
namespace hardware {
namespace ir {
namespace V1_0 {
namespace implementation {

Return<bool> ConsumerIr::transmit(int32_t carrierFreq, const hidl_vec<int32_t>& pattern) {
    if (carrierFreq < 30000 || carrierFreq > 60000 ||
        pattern.size() == 0 || pattern.size() > 1020 || !(pattern.size() & 1)) {
        return false;
    }

    uint64_t total = 0;
    for (int32_t duration : pattern) {
        if (duration <= 0 || duration > 500000 || (total += duration) > 2000000) {
            return false;
        }
    }

    int fd = open("/dev/lirc0", O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    unsigned int carrier = static_cast<unsigned int>(carrierFreq);
    bool result = ioctl(fd, LIRC_SET_SEND_CARRIER, &carrier) == 0 &&
                  write(fd, pattern.data(), pattern.size() * sizeof(int32_t)) ==
                      static_cast<ssize_t>(pattern.size() * sizeof(int32_t));
    close(fd);
    return result;
}

Return<void> ConsumerIr::getCarrierFreqs(getCarrierFreqs_cb callback) {
    hidl_vec<ConsumerIrFreqRange> ranges;
    ranges.resize(1);
    ranges[0].min = 30000;
    ranges[0].max = 60000;
    callback(true, ranges);
    return Void();
}

}
}
}
}
}
