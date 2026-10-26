#include <android/hardware/radio/1.0/IRadio.h>

int main() {
    return android::hardware::radio::V1_0::IRadio::tryGetService("slot1") ? 0 : 1;
}
