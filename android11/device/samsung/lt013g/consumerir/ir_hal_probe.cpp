#include <android/hardware/ir/1.0/IConsumerIr.h>
#include <cstdio>
#include <cstring>

using android::hardware::hidl_vec;
using android::hardware::ir::V1_0::ConsumerIrFreqRange;
using android::hardware::ir::V1_0::IConsumerIr;

int main(int argc, char **argv) {
    bool transmit = argc == 2 && std::strcmp(argv[1], "--transmit") == 0;
    if (argc > 2 || (argc == 2 && !transmit)) {
        std::puts("Usage: t311-ir-hal-probe [--transmit]");
        return 2;
    }

    auto ir = IConsumerIr::getService();
    if (!ir) {
        std::puts("IR_HAL_UNAVAILABLE");
        return 1;
    }

    bool callbackCalled = false;
    bool frequenciesValid = false;
    auto response = ir->getCarrierFreqs(
        [&](bool success, const hidl_vec<ConsumerIrFreqRange>& ranges) {
            callbackCalled = true;
            for (const auto& range : ranges) {
                if (success && range.min > 0 && range.min <= range.max &&
                    range.min <= 38000 && range.max >= 38000) {
                    frequenciesValid = true;
                    std::printf("IR_HAL_CARRIER_RANGE min=%d max=%d\n",
                                range.min, range.max);
                }
            }
        });
    if (!response.isOk() || !callbackCalled || !frequenciesValid) {
        std::puts("IR_HAL_CARRIER_QUERY_FAILED");
        return 1;
    }

    if (!transmit) {
        std::puts("IR_HAL_READ_ONLY_PASS: no infrared signal transmitted");
        return 0;
    }

    hidl_vec<int32_t> pattern;
    pattern.resize(3);
    pattern[0] = 9000;
    pattern[1] = 4500;
    pattern[2] = 560;
    auto result = ir->transmit(38000, pattern);
    if (!result.isOk() || !static_cast<bool>(result)) {
        std::puts("IR_HAL_TRANSMIT_FAILED");
        return 1;
    }
    std::puts("IR_HAL_TRANSMIT_OK");
    return 0;
}
