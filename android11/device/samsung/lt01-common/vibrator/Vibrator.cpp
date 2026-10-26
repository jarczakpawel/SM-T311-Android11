// Copyright (C) 2026 Pawel Jarczak
// SPDX-License-Identifier: Apache-2.0
#define LOG_TAG "VibratorHalLt013g"

#include <android/hardware/vibrator/1.0/IVibrator.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>

using android::OK;
using android::sp;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::vibrator::V1_0::Effect;
using android::hardware::vibrator::V1_0::EffectStrength;
using android::hardware::vibrator::V1_0::IVibrator;
using android::hardware::vibrator::V1_0::Status;

namespace {

constexpr const char* kInputDir = "/dev/input";
constexpr const char* kDeviceName = "gpio-vibrator";

int openFfDevice() {
    DIR* dir = opendir(kInputDir);
    if (dir == nullptr) return -1;
    int found = -1;
    while (dirent* entry = readdir(dir)) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;
        const std::string path = std::string(kInputDir) + "/" + entry->d_name;
        const int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        char name[64] = {};
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 && strcmp(name, kDeviceName) == 0) {
            found = fd;
            break;
        }
        close(fd);
    }
    closedir(dir);
    return found;
}

class Vibrator : public IVibrator {
  public:
    Return<Status> on(uint32_t timeoutMs) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureDevice()) return Status::UNSUPPORTED_OPERATION;
        ff_effect effect{};
        effect.type = FF_RUMBLE;
        effect.id = static_cast<int16_t>(effectId_);
        effect.u.rumble.strong_magnitude = 0xffff;
        effect.replay.length = static_cast<uint16_t>(std::min<uint32_t>(timeoutMs, 0xffff));
        if (ioctl(fd_, EVIOCSFF, &effect) < 0) {
            ALOGE("EVIOCSFF failed: %s", strerror(errno));
            effectId_ = -1;
            return Status::UNKNOWN_ERROR;
        }
        effectId_ = effect.id;
        return play(1);
    }

    Return<Status> off() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ < 0 || effectId_ < 0) return Status::OK;
        return play(0);
    }

    Return<bool> supportsAmplitudeControl() override { return false; }

    Return<Status> setAmplitude(uint8_t) override { return Status::UNSUPPORTED_OPERATION; }

    Return<void> perform(Effect, EffectStrength, perform_cb hidlCb) override {
        hidlCb(Status::UNSUPPORTED_OPERATION, 0);
        return Void();
    }

  private:
    bool ensureDevice() {
        if (fd_ >= 0) return true;
        fd_ = openFfDevice();
        if (fd_ < 0) {
            ALOGE("input device %s not found", kDeviceName);
            return false;
        }
        effectId_ = -1;
        return true;
    }

    Status play(int value) {
        input_event event{};
        event.type = EV_FF;
        event.code = static_cast<uint16_t>(effectId_);
        event.value = value;
        if (write(fd_, &event, sizeof(event)) != static_cast<ssize_t>(sizeof(event))) {
            ALOGE("EV_FF write failed: %s", strerror(errno));
            close(fd_);
            fd_ = -1;
            effectId_ = -1;
            return Status::UNKNOWN_ERROR;
        }
        return Status::OK;
    }

    std::mutex mutex_;
    int fd_ = -1;
    int effectId_ = -1;
};

}  // namespace

int main() {
    configureRpcThreadpool(1, true);
    sp<IVibrator> service = new Vibrator();
    if (service->registerAsService() != OK) {
        ALOGE("failed to register IVibrator/default");
        return 1;
    }
    ALOGI("IVibrator/default registered (%s)", kDeviceName);
    joinRpcThreadpool();
    return 1;
}
