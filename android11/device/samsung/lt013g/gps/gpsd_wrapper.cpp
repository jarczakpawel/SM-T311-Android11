// Copyright (C) 2026 Pawel Jarczak
#define LOG_TAG "Lt013gGpsdWrapper"

#include <log/log.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>

int main() {
    constexpr std::array<const char*, 4> binaries = {
        "/vendor/bin/gpsd",
        "/system/bin/gpsd",
        "/system/system/bin/gpsd",
        "/system/vendor/bin/gpsd",
    };
    constexpr std::array<const char*, 3> configs = {
        "/vendor/etc/gps.xml",
        "/system/etc/gps.xml",
        "/system/system/etc/gps.xml",
    };

    const char* binary = nullptr;
    const char* config = nullptr;
    for (const char* candidate : binaries) {
        if (access(candidate, X_OK) == 0) {
            binary = candidate;
            break;
        }
    }
    for (const char* candidate : configs) {
        if (access(candidate, R_OK) == 0) {
            config = candidate;
            break;
        }
    }
    if (!binary || !config) {
        ALOGE("GPS stack incomplete: gpsd=%s gps.xml=%s", binary ? binary : "missing",
              config ? config : "missing");
        return 127;
    }

    ALOGI("starting %s -c %s", binary, config);
    execl(binary, binary, "-c", config, static_cast<char*>(nullptr));
    ALOGE("exec(%s) failed: %s", binary, strerror(errno));
    return 126;
}
