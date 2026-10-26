#pragma once

#include <android/hardware/power/1.0/IPower.h>
#include <hidl/Status.h>

#include <memory>

namespace android {
namespace hardware {
namespace power {
namespace V1_0 {
namespace implementation {

using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_vec;
using ::android::hardware::power::V1_0::Feature;
using ::android::hardware::power::V1_0::IPower;
using ::android::hardware::power::V1_0::PowerHint;
using ::android::hardware::power::V1_0::PowerStatePlatformSleepState;
using ::android::hardware::power::V1_0::Status;

class BoostController;

struct Power final : public IPower {
    Power();
    ~Power() override;

    Return<void> setInteractive(bool interactive) override;
    Return<void> powerHint(PowerHint hint, int32_t data) override;
    Return<void> setFeature(Feature feature, bool activate) override;
    Return<void> getPlatformLowPowerStats(
            getPlatformLowPowerStats_cb _hidl_cb) override;

  private:
    std::unique_ptr<BoostController> boost_;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace power
}  // namespace hardware
}  // namespace android
