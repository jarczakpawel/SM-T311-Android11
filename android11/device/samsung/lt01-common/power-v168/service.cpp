#define LOG_TAG "SMT311PowerHALService"

#include "Power.h"

#include <android-base/logging.h>
#include <hidl/HidlTransportSupport.h>

using ::android::OK;
using ::android::sp;
using ::android::status_t;
using ::android::hardware::configureRpcThreadpool;
using ::android::hardware::joinRpcThreadpool;
using ::android::hardware::power::V1_0::IPower;
using ::android::hardware::power::V1_0::implementation::Power;

int main() {
    configureRpcThreadpool(1, true);
    sp<IPower> service = new Power();
    const status_t status = service->registerAsService();
    if (status != OK) {
        LOG(ERROR) << "registerAsService failed: " << status;
        return 1;
    }
    LOG(INFO) << "SM-T311 Power HAL 1.0 ready";
    joinRpcThreadpool();
    return 1;
}
