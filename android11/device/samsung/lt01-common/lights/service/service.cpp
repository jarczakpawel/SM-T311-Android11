// Copyright (C) 2026 Pawel Jarczak
#define LOG_TAG "LightHalLt013gService"

#include "Light.h"

#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

using android::OK;
using android::sp;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::light::V2_0::implementation::Light;

int main() {
    configureRpcThreadpool(1, true);
    sp<Light> service = new Light();
    if (service->registerAsService() != OK) {
        ALOGE("failed to register ILight/default");
        return 1;
    }
    ALOGI("SM-T311 V161 ILight/default registered");
    joinRpcThreadpool();
    return 1;
}
