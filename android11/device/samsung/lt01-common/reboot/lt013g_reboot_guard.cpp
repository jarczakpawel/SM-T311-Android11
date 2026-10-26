/*
 * Copyright (C) 2026 Paweł Jarczak
 * SPDX-License-Identifier: Apache-2.0
 *
 * SM-T311 reboot finalizer. Android init remains the primary shutdown
 * coordinator. If old userspace cannot complete a reboot request, this daemon
 * performs the Linux reboot syscall directly instead of setting sys.powerctl
 * for a second time.
 *
 * Recovery/download selection is preserved by the kernel syscon-reboot-mode
 * notifier. This daemon intentionally does not access the OTA/misc partition.
 */

#define LOG_TAG "lt013g-reboot-guard"

#include <android-base/logging.h>
#include <android-base/properties.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <linux/reboot.h>
#include <string>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

namespace {

constexpr auto kPollPeriod = std::chrono::milliseconds(100);
constexpr auto kGracePeriod = std::chrono::seconds(6);

std::string reboot_target(const std::string& powerctl) {
    if (powerctl == "reboot") return "normal";
    constexpr char prefix[] = "reboot,";
    if (powerctl.rfind(prefix, 0) == 0) {
        std::string target = powerctl.substr(sizeof(prefix) - 1);
        if (target.empty() || target == "adb") return "normal";
        return target;
    }
    return {};
}

void direct_reboot(const std::string& target) {
    sync();

    errno = 0;
    long rc;
    if (target == "normal") {
        LOG(ERROR) << "forcing direct LINUX_REBOOT_CMD_RESTART";
        rc = syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
                     LINUX_REBOOT_CMD_RESTART, nullptr);
    } else {
        LOG(ERROR) << "forcing direct LINUX_REBOOT_CMD_RESTART2 target=" << target;
        rc = syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
                     LINUX_REBOOT_CMD_RESTART2, target.c_str());
    }

    const int saved_errno = errno;
    LOG(ERROR) << "direct reboot syscall returned rc=" << rc
               << " errno=" << saved_errno << " (" << strerror(saved_errno) << ")";

    /* Last-resort normal restart. It cannot preserve a special boot target. */
    errno = 0;
    rc = syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
                 LINUX_REBOOT_CMD_RESTART, nullptr);
    LOG(ERROR) << "fallback restart returned rc=" << rc << " errno=" << errno;
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    android::base::InitLogging(argv);
    LOG(INFO) << "Paweł Jarczak SM-T311 direct reboot guard V198 started";

    std::string handled;
    for (;;) {
        const std::string powerctl = android::base::GetProperty("sys.powerctl", "");
        const std::string target = reboot_target(powerctl);
        if (!target.empty() && powerctl != handled) {
            handled = powerctl;
            LOG(WARNING) << "observed " << powerctl << ", allowing Android "
                         << kGracePeriod.count() << " seconds for orderly shutdown";
            LOG(INFO) << "kernel reboot-mode will preserve target=" << target;

            std::this_thread::sleep_for(kGracePeriod);
            direct_reboot(target);
        }
        std::this_thread::sleep_for(kPollPeriod);
    }
}
