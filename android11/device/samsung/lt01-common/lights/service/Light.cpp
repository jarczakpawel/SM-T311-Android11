// Copyright (C) 2026 Pawel Jarczak
#define LOG_TAG "LightHalLt013g"

#include "Light.h"

#include <fcntl.h>
#include <log/log.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {
namespace {
constexpr const char* kBacklightBrightness = "/sys/class/backlight/lcd-bl/brightness";
constexpr const char* kBacklightMax = "/sys/class/backlight/lcd-bl/max_brightness";
constexpr const char* kButtonsBrightness = "/sys/class/leds/white:kbd_backlight/brightness";
constexpr const char* kButtonsMax = "/sys/class/leds/white:kbd_backlight/max_brightness";
}

Light::Light()
    : backlightMax_(readInt(kBacklightMax, 255)),
      buttonsMax_(readInt(kButtonsMax, 1)),
      haveBacklight_(pathExists(kBacklightBrightness)),
      haveButtons_(pathExists(kButtonsBrightness)) {
    ALOGI("V161 light HAL: backlight=%d max=%d buttons=%d max=%d",
          haveBacklight_, backlightMax_, haveButtons_, buttonsMax_);
}

bool Light::pathExists(const char* path) {
    return access(path, F_OK) == 0;
}

int Light::readInt(const char* path, int fallback) {
    char buffer[32] = {};
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return fallback;
    const ssize_t count = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (count <= 0) return fallback;
    char* end = nullptr;
    const long value = strtol(buffer, &end, 10);
    if (end == buffer || value <= 0 || value > 65535) return fallback;
    return static_cast<int>(value);
}

bool Light::writeInt(const char* path, int value) {
    char buffer[32];
    const int length = snprintf(buffer, sizeof(buffer), "%d\n", value);
    if (length <= 0 || length >= static_cast<int>(sizeof(buffer))) return false;

    const int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        ALOGE("open(%s) failed: %d", path, errno);
        return false;
    }
    const ssize_t written = write(fd, buffer, static_cast<size_t>(length));
    const int savedErrno = errno;
    close(fd);
    if (written != length) {
        ALOGE("write(%s) failed: %d", path, savedErrno);
        return false;
    }
    return true;
}

int Light::rgbToBrightness(uint32_t color) {
    const int red = (color >> 16) & 0xff;
    const int green = (color >> 8) & 0xff;
    const int blue = color & 0xff;
    return (77 * red + 150 * green + 29 * blue) >> 8;
}

Return<Status> Light::setLight(Type type, const LightState& state) {
    std::lock_guard<std::mutex> guard(lock_);
    if (state.brightnessMode == Brightness::LOW_PERSISTENCE) {
        return Status::BRIGHTNESS_NOT_SUPPORTED;
    }
    switch (type) {
        case Type::BACKLIGHT:
            return setBacklight(state);
        case Type::BUTTONS:
            return setButtons(state);
        default:
            return Status::LIGHT_NOT_SUPPORTED;
    }
}

Status Light::setBacklight(const LightState& state) {
    if (!haveBacklight_) return Status::LIGHT_NOT_SUPPORTED;
    const int brightness = rgbToBrightness(state.color);
    const int scaled = std::min(std::max((brightness * backlightMax_ + 127) / 255, 0), backlightMax_);
    return writeInt(kBacklightBrightness, scaled) ? Status::SUCCESS : Status::UNKNOWN;
}

Status Light::setButtons(const LightState& state) {
    // SM-T311 V176: regulator-led probes after the Light HAL on this board.
    // Re-detect the node instead of caching the early boot result forever.
    if (!haveButtons_) {
        haveButtons_ = pathExists(kButtonsBrightness);
        if (haveButtons_) buttonsMax_ = readInt(kButtonsMax, 1);
    }
    if (!haveButtons_) return Status::LIGHT_NOT_SUPPORTED;
    const int value = (state.color & 0x00ffffffU) ? buttonsMax_ : 0;
    return writeInt(kButtonsBrightness, value) ? Status::SUCCESS : Status::UNKNOWN;
}

Return<void> Light::getSupportedTypes(getSupportedTypes_cb callback) {
    hidl_vec<Type> types;
    size_t count = 1;  // SM-T311 V176: MENU/BACK LEDs are fixed board hardware.
    if (haveBacklight_) ++count;
    types.resize(count);
    size_t index = 0;
    if (haveBacklight_) types[index++] = Type::BACKLIGHT;
    types[index++] = Type::BUTTONS;
    callback(types);
    return Void();
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
