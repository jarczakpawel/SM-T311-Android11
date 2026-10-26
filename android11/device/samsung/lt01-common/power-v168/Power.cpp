#define LOG_TAG "SMT311PowerHAL"

#include "Power.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace android {
namespace hardware {
namespace power {
namespace V1_0 {
namespace implementation {
namespace {

using Clock = std::chrono::steady_clock;

struct BoostNode {
    const char* path;
    int64_t wake_value;
    int64_t interaction_value;
    int64_t base_value;
    bool available;
    int64_t applied_value = -1;
};

bool WriteValue(const char* path, int64_t value) {
    if (!android::base::WriteStringToFile(std::to_string(value), path)) {
        PLOG(ERROR) << "write failed: " << path << '=' << value;
        return false;
    }
    return true;
}

bool WriteText(const char* path, const char* value) {
    if (!android::base::WriteStringToFile(value, path)) {
        PLOG(ERROR) << "write failed: " << path << '=' << value;
        return false;
    }
    return true;
}

int64_t ReadValue(const char* path, int64_t fallback) {
    std::string text;
    if (!android::base::ReadFileToString(path, &text)) {
        return fallback;
    }
    text = android::base::Trim(text);
    if (text.empty()) {
        return fallback;
    }
    char* end = nullptr;
    long long value = std::strtoll(text.c_str(), &end, 10);
    return end == text.c_str() ? fallback : value;
}

int ClampDuration(int data, int fallback, int minimum, int maximum) {
    if (data <= 0) {
        data = fallback;
    }
    return std::max(minimum, std::min(maximum, data));
}

}  // namespace

class BoostController {
  public:
    BoostController()
        : nodes_({
              {"/sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq",
               1200000, 900000, 200000, false},
              {"/sys/class/devfreq/13000000.gpu/min_freq",
               350000000, 267000000, 160000000, false},
              {"/sys/class/devfreq/bus-dmc/min_freq",
               400000000, 267000000, 100000000, false},
              {"/sys/class/devfreq/bus-display/min_freq",
               200000000, 200000000, 160000000, false},
              {"/sys/class/devfreq/bus-leftbus/min_freq",
               200000000, 160000000, 100000000, false},
              {"/sys/class/devfreq/bus-rightbus/min_freq",
               200000000, 160000000, 100000000, false},
              {"/sys/class/devfreq/bus-c2c/min_freq",
               267000000, 267000000, 100000000, false},
          }),
          worker_(&BoostController::Worker, this) {
        (void)WriteText(
                "/sys/devices/system/cpu/cpufreq/policy0/scaling_governor",
                "schedutil");
        (void)WriteText("/sys/class/devfreq/13000000.gpu/governor",
                        "simple_ondemand");

        for (auto& node : nodes_) {
            std::string probe;
            node.available = android::base::ReadFileToString(node.path, &probe);
            if (node.available) {
                node.base_value = ReadValue(node.path, node.base_value);
            }
        }
        RestoreLocked();
        LOG(INFO) << "SM-T311 short-boost controller initialized";
    }

    ~BoostController() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            cv_.notify_all();
        }
        worker_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        RestoreLocked();
    }

    void SetInteractive(bool interactive) {
        if (interactive) {
            Request(true, 1800);
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            deadline_ = Clock::time_point();
            wake_deadline_ = Clock::time_point();
            RestoreLocked();
            cv_.notify_all();
        }
    }

    void Interaction(int data) {
        Request(false, ClampDuration(data, 650, 150, 1500));
    }

    void Launch(int data) {
        if (data != 0) {
            Request(true, 2000);
        }
    }

    void SetLowPower(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        low_power_ = enabled;
        if (enabled) {
            deadline_ = Clock::time_point();
            wake_deadline_ = Clock::time_point();
            RestoreLocked();
        }
        cv_.notify_all();
    }

    void SetSustained(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        sustained_ = enabled;
        if (deadline_ != Clock::time_point()) {
            ApplyLocked(wake_deadline_ > Clock::now());
        }
        cv_.notify_all();
    }

  private:
    void Request(bool wake, int duration_ms) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (low_power_) {
            return;
        }
        const auto new_deadline =
                Clock::now() + std::chrono::milliseconds(duration_ms);
        if (new_deadline > deadline_) {
            deadline_ = new_deadline;
        }
        if (wake && new_deadline > wake_deadline_) {
            wake_deadline_ = new_deadline;
        }
        ApplyLocked(wake_deadline_ > Clock::now());
        cv_.notify_all();
    }

    void ApplyLocked(bool wake) {
        for (auto& node : nodes_) {
            if (!node.available) {
                continue;
            }
            int64_t value = wake ? node.wake_value : node.interaction_value;
            if (sustained_) {
                value = std::min(value, node.interaction_value);
            }
            if (node.applied_value != value && WriteValue(node.path, value)) {
                node.applied_value = value;
            }
        }
    }

    void RestoreLocked() {
        for (auto& node : nodes_) {
            if (node.available && node.applied_value != node.base_value &&
                WriteValue(node.path, node.base_value)) {
                node.applied_value = node.base_value;
            }
        }
    }

    void Worker() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stop_) {
            if (deadline_ == Clock::time_point()) {
                cv_.wait(lock, [this] {
                    return stop_ || deadline_ != Clock::time_point();
                });
                continue;
            }

            const auto observed = deadline_;
            const auto observed_wake = wake_deadline_;
            const auto wake_active = wake_deadline_ > Clock::now();
            const auto next = wake_active ? std::min(deadline_, wake_deadline_)
                                          : deadline_;
            cv_.wait_until(lock, next, [this, observed, observed_wake] {
                return stop_ || deadline_ != observed ||
                       wake_deadline_ != observed_wake;
            });
            if (stop_) {
                break;
            }
            if (deadline_ == Clock::time_point()) {
                continue;
            }
            const auto now = Clock::now();
            if (now >= deadline_) {
                deadline_ = Clock::time_point();
                wake_deadline_ = Clock::time_point();
                RestoreLocked();
            } else if (wake_deadline_ != Clock::time_point() &&
                       now >= wake_deadline_) {
                wake_deadline_ = Clock::time_point();
                ApplyLocked(false);
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<BoostNode> nodes_;
    Clock::time_point deadline_{};
    Clock::time_point wake_deadline_{};
    bool stop_ = false;
    bool low_power_ = false;
    bool sustained_ = false;
    std::thread worker_;
};

Power::Power() : boost_(new BoostController()) {}
Power::~Power() = default;

Return<void> Power::setInteractive(bool interactive) {
    LOG(INFO) << "setInteractive=" << interactive;
    boost_->SetInteractive(interactive);
    return Void();
}

Return<void> Power::powerHint(PowerHint hint, int32_t data) {
    switch (hint) {
        case PowerHint::INTERACTION:
            boost_->Interaction(data);
            break;
        case PowerHint::LAUNCH:
            boost_->Launch(data);
            break;
        case PowerHint::LOW_POWER:
            boost_->SetLowPower(data != 0);
            break;
        case PowerHint::SUSTAINED_PERFORMANCE:
            boost_->SetSustained(data != 0);
            break;
        default:
            break;
    }
    return Void();
}

Return<void> Power::setFeature(Feature /*feature*/, bool /*activate*/) {
    return Void();
}

Return<void> Power::getPlatformLowPowerStats(
        getPlatformLowPowerStats_cb _hidl_cb) {
    hidl_vec<PowerStatePlatformSleepState> states;
    _hidl_cb(states, Status::SUCCESS);
    return Void();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace power
}  // namespace hardware
}  // namespace android
