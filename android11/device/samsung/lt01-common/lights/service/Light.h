// Copyright (C) 2026 Pawel Jarczak
#pragma once

#include <android/hardware/light/2.0/ILight.h>
#include <mutex>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

class Light final : public ILight {
public:
    Light();

    Return<Status> setLight(Type type, const LightState& state) override;
    Return<void> getSupportedTypes(getSupportedTypes_cb callback) override;

private:
    static int readInt(const char* path, int fallback);
    static bool pathExists(const char* path);
    static int rgbToBrightness(uint32_t color);
    static bool writeInt(const char* path, int value);

    Status setBacklight(const LightState& state);
    Status setButtons(const LightState& state);

    std::mutex lock_;
    int backlightMax_;
    int buttonsMax_;
    bool haveBacklight_;
    bool haveButtons_;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
