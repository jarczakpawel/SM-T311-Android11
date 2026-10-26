// SPDX-License-Identifier: Apache-2.0
// SM-T311 USB OTG Android -> gadget NONE -> charger HOST VBUS handshake.
// NOT INSTALLED / NOT PROVEN PHYSICALLY. Requires external rescue & hardware.
// NO active operations without --supervised AND explicit offline opt-in.
#include <android/hardware/usb/gadget/1.0/IUsbGadget.h>
#include <android/hardware/usb/gadget/1.0/IUsbGadgetCallback.h>
#include <hidl/HidlTransportSupport.h>
#include <unistd.h>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace gadget = android::hardware::usb::gadget::V1_0;
using android::hardware::Return;
using android::sp;
using Clock = std::chrono::steady_clock;

static constexpr const char *kExtcon = "/sys/class/extcon/extcon0/state";
static constexpr const char *kInterlock =
    "/sys/devices/platform/max77693-charger/t311_otg_interlock";
static constexpr const char *kRefresh =
    "/sys/devices/platform/max77693-charger/t311_otg_host_refresh";
static constexpr int kPollMs = 250;

static bool readFile(const char *path, std::string *s) {
    std::ifstream f(path);
    if (!f.good()) return false;
    *s = std::string((std::istreambuf_iterator<char>(f)), {});
    return true;
}

static bool writeFile(const char *path, const char *value) {
    std::ofstream f(path);
    if (!f.good()) return false;
    f << value;
    f.flush();
    return f.good();
}

static bool hasField(const std::string &s, const std::string &name, int value) {
    std::string pattern = name + "=" + std::to_string(value);
    size_t pos = s.find(pattern);
    while (pos != std::string::npos) {
        const size_t end = pos + pattern.size();
        if ((pos == 0 || s[pos - 1] == '\n' || s[pos - 1] == ' ') &&
            (end == s.size() || s[end] == '\n' || s[end] == ' '))
            return true;
        pos = s.find(pattern, pos + 1);
    }
    return false;
}

class Callback final : public gadget::IUsbGadgetCallback {
public:
    Return<void> setCurrentUsbFunctionsCb(uint64_t functions,
                                           gadget::Status status) override {
        std::lock_guard<std::mutex> lock(mu_);
        lastFunctions_ = functions;
        status_ = status;
        done_ = true;
        cv_.notify_all();
        return android::hardware::Void();
    }
    Return<void> getCurrentUsbFunctionsCb(uint64_t,
                                           gadget::Status) override {
        return android::hardware::Void();
    }
    void prepare() {
        std::lock_guard<std::mutex> lock(mu_);
        done_ = false;
    }
    bool await(uint64_t expected) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, std::chrono::seconds(10),
                            [this] { return done_; }) &&
               lastFunctions_ == expected && status_ == gadget::Status::SUCCESS;
    }
private:
    std::mutex mu_;
    std::condition_variable cv_;
    bool done_ = false;
    uint64_t lastFunctions_ = 0;
    gadget::Status status_ = gadget::Status::ERROR;
};

static bool setFunctions(const sp<gadget::IUsbGadget> &hal,
                         const sp<Callback> &cb, uint64_t fn) {
    cb->prepare();
    auto result = hal->setCurrentUsbFunctions(fn, cb, 10000);
    return result.isOk() && cb->await(fn);
}

static bool chargerSafeOff() {
    std::string value;
    return readFile(kInterlock, &value) && hasField(value, "safe_off", 1) &&
           hasField(value, "pmic_known", 1) && hasField(value, "lease", 0);
}

static bool host5vReady() {
    std::string value;
    return readFile(kInterlock, &value) && hasField(value, "pmic_known", 1) &&
           hasField(value, "pmic_boost", 1) && hasField(value, "lease", 1) &&
           hasField(value, "boost", 1);
}

static bool extconHost() {
    std::string state;
    return readFile(kExtcon, &state) && hasField(state, "USB-HOST", 1) &&
           hasField(state, "USB", 0);
}

static bool waitFor(bool (*predicate)(), int seconds) {
    const auto limit = Clock::now() + std::chrono::seconds(seconds);
    do {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    } while (Clock::now() < limit);
    return false;
}

int main(int argc, char **argv) {
    std::string extcon, power;
    const bool extconOk = readFile(kExtcon, &extcon);
    const bool powerOk = readFile(kInterlock, &power);
    // Default operation is non-mutating; no USB/HAL I2C writes on inspect.
    if (argc == 2 && std::strcmp(argv[1], "--inspect") == 0) {
        std::printf("extcon: %s\ncharger interlock: %s\n",
                    extconOk ? extcon.c_str() : "UNAVAILABLE",
                    powerOk ? power.c_str() : "NOT ENABLED (EXPECTED)");
        return extconOk ? 0 : 2;
    }
    // Separate independent recovery is mandatory. This opt-in is intentionally
    // NOT a proof of Wi-Fi ADB/serial, cable wiring or real 5V safety.
    if (argc != 2 || std::strcmp(argv[1], "--supervised") || getuid() != 0 ||
        !std::getenv("T311_INDEPENDENT_RECOVERY_READY") ||
        std::strcmp(std::getenv("T311_INDEPENDENT_RECOVERY_READY"), "YES") ||
        !extconOk || !powerOk || !chargerSafeOff()) {
        std::fprintf(stderr, "OTG REFUSED: supervised root, independent rescue, "
                             "known PMIC OFF and opt-in kernel required\n");
        return 3;
    }
    android::hardware::configureRpcThreadpool(1, false);
    auto hal = gadget::IUsbGadget::getService();
    if (!hal) return 4;
    sp<Callback> cb = new Callback();
    // Physical HOST insertion must be detected BEFORE disconnecting ADB.
    if (!waitFor(extconHost, 90)) return 5;
    if (!chargerSafeOff()) return 6;
    constexpr uint64_t none = static_cast<uint64_t>(gadget::GadgetFunction::NONE);
    constexpr uint64_t adb = static_cast<uint64_t>(gadget::GadgetFunction::ADB);
    if (!setFunctions(hal, cb, none)) return 7;

    // HAL NONE should release DWC2 device PHY0. The kernel's independent
    // PHY1 lease and actual MODE_SWITCH HOST gate BOOST in this refresh.
    bool refreshed = writeFile(kRefresh, "1\n");
    bool hostOn = refreshed && waitFor(host5vReady, 5);
    if (hostOn) {
        std::fprintf(stderr, "T311 OTG BOOST register reports ON; physical "
                             "5V, overcurrent and USB enumeration UNPROVEN\n");
        // Wait for physical detach; kernel extcon worker owns PMIC OFF.
        const auto until = Clock::now() + std::chrono::seconds(90);
        while (extconHost() && Clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    // NEVER reconnect Gadget while physical HOST BOOST may still source VBUS.
    // If I2C failed and power remains uncertain, fail closed; an independent
    // recovery/operator is required rather than risking VBUS backfeed.
    if (!waitFor(chargerSafeOff, 8)) {
        std::fprintf(stderr, "OTG INCOMPLETE: physical charger OFF not "
                             "confirmed; gadget remains disconnected\n");
        return 8;
    }
    if (!setFunctions(hal, cb, adb)) {
        std::fprintf(stderr, "OTG restore failed; independent rescue needed\n");
        return 9;
    }
    return hostOn ? 0 : 10;
}
