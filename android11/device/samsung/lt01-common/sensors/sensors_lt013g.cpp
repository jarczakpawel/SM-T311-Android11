/*
 * Copyright (C) 2026 Paweł Jarczak
 * SPDX-License-Identifier: Apache-2.0
 *
 * Small Android Sensors HAL 1.3 for the SM-T311 IIO sensor stack.
 * It intentionally delegates hardware acquisition to upstream-style IIO
 * drivers and contains no duplicated sensor register programming.
 */

#define LOG_TAG "sensors.lt013g"

#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <hardware/hardware.h>
#include <hardware/sensors.h>
#include <log/log.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

constexpr int kHandleLight = 1;
constexpr int kHandleProximity = 2;
constexpr int kHandleAccelerometer = 3;
constexpr int kHandleMagneticField = 4;
constexpr int kFirstHandle = kHandleLight;
constexpr int kLastHandle = kHandleMagneticField;
constexpr int64_t kNsPerSecond = 1000000000LL;
constexpr int64_t kDefaultDelayNs = 200000000LL;
constexpr float kStandardGravity = 9.80665f;

struct IioDevice {
    std::string base;
    std::string name;
};

// Row-major rotation from IIO chip axes to the Android device frame.
struct MountMatrix {
    std::array<float, 9> m;
};

// Defaults equal the Samsung vendor kernel for all Tab 3 8.0 variants and are
// used only when the kernel does not expose a mount_matrix attribute.
// K2DH accelerometer: position 5 (bottom/upper-right) -> diag(1, -1, -1).
constexpr MountMatrix kVendorAccelMatrix{{1, 0, 0, 0, -1, 0, 0, 0, -1}};
// YAS532 magnetometer: CONFIG_INPUT_YAS_MAGNETOMETER_POSITION=7 -> diag(-1, 1, -1).
constexpr MountMatrix kVendorMagMatrix{{-1, 0, 0, 0, 1, 0, 0, 0, -1}};

struct SensorState {
    bool enabled = false;
    bool flush_pending = false;
    int64_t delay_ns = kDefaultDelayNs;
    int64_t last_timestamp_ns = 0;
    uint64_t generation = 0;
    // ON_CHANGE sensors must report the first value after activation, and
    // thereafter only real changes. Keep this state per sensor handle.
    bool last_on_change_valid = false;
    float last_on_change_value = 0.0f;
};

struct Context {
    sensors_poll_device_1_t device{};
    std::mutex lock;
    std::array<SensorState, kLastHandle + 1> state{};
    IioDevice cm36653;
    IioDevice accelerometer;
    IioDevice magnetometer;
    MountMatrix accel_matrix = kVendorAccelMatrix;
    MountMatrix mag_matrix = kVendorMagMatrix;
    std::array<float, 3> accel_scale{};
    std::array<float, 3> mag_scale{};
    bool proximity_near = false;
};

int64_t boot_time_ns() {
    timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * kNsPerSecond + ts.tv_nsec;
}

std::string trim(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
        value.pop_back();
    }
    size_t first = 0;
    while (first < value.size() && value[first] == ' ') ++first;
    return value.substr(first);
}

bool read_text(const std::string& path, std::string* out) {
    std::string data;
    if (!android::base::ReadFileToString(path, &data)) return false;
    *out = trim(data);
    return true;
}

bool read_float(const std::string& path, float* value) {
    std::string text;
    if (!read_text(path, &text)) return false;
    char* end = nullptr;
    errno = 0;
    const float parsed = strtof(text.c_str(), &end);
    if (errno != 0 || end == text.c_str()) return false;
    *value = parsed;
    return true;
}

bool read_first_float(const std::string& base, const std::initializer_list<const char*>& names,
                      float* value) {
    for (const char* name : names) {
        if (read_float(base + "/" + name, value)) return true;
    }
    return false;
}

IioDevice find_iio_device(const std::initializer_list<const char*>& accepted_names) {
    constexpr const char* kRoot = "/sys/bus/iio/devices";
    DIR* dir = opendir(kRoot);
    if (!dir) return {};

    IioDevice result;
    while (dirent* entry = readdir(dir)) {
        if (strncmp(entry->d_name, "iio:device", 10) != 0) continue;
        const std::string base = std::string(kRoot) + "/" + entry->d_name;
        std::string name;
        if (!read_text(base + "/name", &name)) continue;
        for (const char* accepted : accepted_names) {
            if (name == accepted) {
                result = {base, name};
                closedir(dir);
                return result;
            }
        }
    }
    closedir(dir);
    return result;
}

float read_scale(const std::string& base, const std::initializer_list<const char*>& names,
                 float fallback) {
    float value = fallback;
    read_first_float(base, names, &value);
    return value;
}

// The kernel device tree is the single source of truth for sensor placement.
// IIO prints the matrix as "a, b, c; d, e, f; g, h, i" under one of these names
// (st_accel: shared by all -> mount_matrix, yamaha-yas530: in_mount_matrix).
bool parse_mount_matrix(const std::string& text, MountMatrix* out) {
    MountMatrix parsed{};
    const char* cursor = text.c_str();
    for (size_t i = 0; i < parsed.m.size(); ++i) {
        while (*cursor == ' ' || *cursor == ',' || *cursor == ';' || *cursor == '\t' ||
               *cursor == '\n') {
            ++cursor;
        }
        char* end = nullptr;
        errno = 0;
        const float value = strtof(cursor, &end);
        if (errno != 0 || end == cursor || !std::isfinite(value) || std::fabs(value) > 1.001f) {
            return false;
        }
        parsed.m[i] = value;
        cursor = end;
    }
    *out = parsed;
    return true;
}

MountMatrix read_mount_matrix(const IioDevice& dev, const MountMatrix& fallback, const char* what) {
    if (dev.base.empty()) return fallback;
    for (const char* name : {"mount_matrix", "in_mount_matrix", "in_accel_mount_matrix",
                             "in_magn_mount_matrix"}) {
        std::string text;
        MountMatrix matrix{};
        if (read_text(dev.base + "/" + name, &text) && parse_mount_matrix(text, &matrix)) {
            ALOGI("%s mount matrix from kernel %s: [%g %g %g; %g %g %g; %g %g %g]", what, name,
                  matrix.m[0], matrix.m[1], matrix.m[2], matrix.m[3], matrix.m[4], matrix.m[5],
                  matrix.m[6], matrix.m[7], matrix.m[8]);
            return matrix;
        }
    }
    ALOGW("%s: kernel exposes no mount matrix, using Samsung vendor matrix", what);
    return fallback;
}

void apply_matrix(const MountMatrix& matrix, float x, float y, float z, float* ox, float* oy,
                  float* oz) {
    *ox = matrix.m[0] * x + matrix.m[1] * y + matrix.m[2] * z;
    *oy = matrix.m[3] * x + matrix.m[4] * y + matrix.m[5] * z;
    *oz = matrix.m[6] * x + matrix.m[7] * y + matrix.m[8] * z;
}

bool read_xyz(const IioDevice& dev, const char* prefix, float* x, float* y, float* z) {
    if (dev.base.empty()) return false;
    return read_float(dev.base + "/in_" + prefix + "_x_raw", x) &&
           read_float(dev.base + "/in_" + prefix + "_y_raw", y) &&
           read_float(dev.base + "/in_" + prefix + "_z_raw", z);
}

sensor_t make_sensor(const char* name, const char* vendor, int handle, int type,
                     float max_range, float resolution, float power, int32_t min_delay_us,
                     const char* string_type, int32_t max_delay_us, uint32_t flags) {
    sensor_t sensor{};
    sensor.name = name;
    sensor.vendor = vendor;
    sensor.version = 1;
    sensor.handle = handle;
    sensor.type = type;
    sensor.maxRange = max_range;
    sensor.resolution = resolution;
    sensor.power = power;
    sensor.minDelay = min_delay_us;
    sensor.fifoReservedEventCount = 0;
    sensor.fifoMaxEventCount = 0;
    sensor.stringType = string_type;
    sensor.requiredPermission = "";
    sensor.maxDelay = max_delay_us;
    sensor.flags = flags;
    return sensor;
}

const std::array<sensor_t, 4>& sensor_list() {
    static const std::array<sensor_t, 4> sensors = {
        make_sensor("CM36653 Ambient Light", "Capella Microsystems", kHandleLight,
                    SENSOR_TYPE_LIGHT, 65535.0f, 1.0f, 0.20f, 200000,
                    SENSOR_STRING_TYPE_LIGHT, 1000000, SENSOR_FLAG_ON_CHANGE_MODE),
        make_sensor("CM36653 Proximity", "Capella Microsystems", kHandleProximity,
                    SENSOR_TYPE_PROXIMITY, 5.0f, 5.0f, 0.25f, 0,
                    SENSOR_STRING_TYPE_PROXIMITY, 0, SENSOR_FLAG_ON_CHANGE_MODE | SENSOR_FLAG_WAKE_UP),
        make_sensor("LIS2DH12 Accelerometer", "STMicroelectronics", kHandleAccelerometer,
                    SENSOR_TYPE_ACCELEROMETER, 39.2266f, 0.00980665f, 0.20f, 20000,
                    SENSOR_STRING_TYPE_ACCELEROMETER, 1000000, SENSOR_FLAG_CONTINUOUS_MODE),
        make_sensor("YAS532 Magnetic Field", "Yamaha", kHandleMagneticField,
                    SENSOR_TYPE_MAGNETIC_FIELD, 2000.0f, 0.1f, 0.50f, 50000,
                    SENSOR_STRING_TYPE_MAGNETIC_FIELD, 1000000, SENSOR_FLAG_CONTINUOUS_MODE),
    };
    return sensors;
}

int get_sensors_list(struct sensors_module_t*, struct sensor_t const** list) {
    *list = sensor_list().data();
    return static_cast<int>(sensor_list().size());
}

Context* from_device(sensors_poll_device_t* dev) {
    return reinterpret_cast<Context*>(dev);
}

int close_device(hw_device_t* dev) {
    delete reinterpret_cast<Context*>(dev);
    return 0;
}

int activate(sensors_poll_device_t* dev, int handle, int enabled) {
    if (handle < kFirstHandle || handle > kLastHandle) return -EINVAL;
    Context* ctx = from_device(dev);
    std::lock_guard<std::mutex> guard(ctx->lock);
    ctx->state[handle].enabled = enabled != 0;
    ctx->state[handle].last_timestamp_ns = 0;
    ctx->state[handle].last_on_change_valid = false;
    ++ctx->state[handle].generation;
    return 0;
}

int set_delay(sensors_poll_device_t* dev, int handle, int64_t ns) {
    if (handle < kFirstHandle || handle > kLastHandle || ns <= 0) return -EINVAL;
    Context* ctx = from_device(dev);
    std::lock_guard<std::mutex> guard(ctx->lock);
    ctx->state[handle].delay_ns = std::clamp<int64_t>(ns, 20000000LL, kNsPerSecond);
    return 0;
}

int batch(sensors_poll_device_1_t* dev, int handle, int, int64_t period_ns, int64_t) {
    return set_delay(&dev->v0, handle, period_ns);
}

int flush(sensors_poll_device_1_t* dev, int handle) {
    if (handle < kFirstHandle || handle > kLastHandle) return -EINVAL;
    Context* ctx = reinterpret_cast<Context*>(dev);
    std::lock_guard<std::mutex> guard(ctx->lock);
    if (!ctx->state[handle].enabled) return -EINVAL;
    ctx->state[handle].flush_pending = true;
    return 0;
}

bool fill_light(Context* ctx, sensors_event_t* event, int64_t timestamp) {
    float raw = 0.0f;
    if (!read_first_float(ctx->cm36653.base,
                          {"in_illuminance_raw", "in_intensity_clear_raw"}, &raw)) return false;
    event->version = sizeof(*event);
    event->sensor = kHandleLight;
    event->type = SENSOR_TYPE_LIGHT;
    event->timestamp = timestamp;
    event->light = std::max(0.0f, raw);
    return true;
}

bool fill_proximity(Context* ctx, sensors_event_t* event, int64_t timestamp) {
    float raw = 0.0f;
    if (!read_float(ctx->cm36653.base + "/in_proximity_raw", &raw)) return false;
    if (raw >= 10.0f) ctx->proximity_near = true;
    else if (raw <= 8.0f) ctx->proximity_near = false;
    event->version = sizeof(*event);
    event->sensor = kHandleProximity;
    event->type = SENSOR_TYPE_PROXIMITY;
    event->timestamp = timestamp;
    event->distance = ctx->proximity_near ? 0.0f : 5.0f;
    return true;
}

bool fill_accelerometer(Context* ctx, sensors_event_t* event, int64_t timestamp) {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!read_xyz(ctx->accelerometer, "accel", &x, &y, &z)) return false;
    event->version = sizeof(*event);
    event->sensor = kHandleAccelerometer;
    event->type = SENSOR_TYPE_ACCELEROMETER;
    event->timestamp = timestamp;
    apply_matrix(ctx->accel_matrix, x * ctx->accel_scale[0], y * ctx->accel_scale[1],
                 z * ctx->accel_scale[2], &event->acceleration.x, &event->acceleration.y,
                 &event->acceleration.z);
    event->acceleration.status = SENSOR_STATUS_ACCURACY_HIGH;
    return true;
}

bool fill_magnetic(Context* ctx, sensors_event_t* event, int64_t timestamp) {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!read_xyz(ctx->magnetometer, "magn", &x, &y, &z) &&
        !read_xyz(ctx->magnetometer, "mag", &x, &y, &z)) return false;
    const float scale_x = ctx->mag_scale[0];
    const float scale_y = ctx->mag_scale[1];
    const float scale_z = ctx->mag_scale[2];
    event->version = sizeof(*event);
    event->sensor = kHandleMagneticField;
    event->type = SENSOR_TYPE_MAGNETIC_FIELD;
    event->timestamp = timestamp;
    // IIO magnetometer scale is expressed in gauss/LSB; Android expects uT.
    constexpr float kGaussToMicrotesla = 100.0f;
    apply_matrix(ctx->mag_matrix, x * scale_x * kGaussToMicrotesla,
                 y * scale_y * kGaussToMicrotesla, z * scale_z * kGaussToMicrotesla,
                 &event->magnetic.x, &event->magnetic.y, &event->magnetic.z);
    event->magnetic.status = SENSOR_STATUS_ACCURACY_MEDIUM;
    return true;
}

bool fill_event(Context* ctx, int handle, sensors_event_t* event, int64_t timestamp) {
    memset(event, 0, sizeof(*event));
    switch (handle) {
        case kHandleLight: return fill_light(ctx, event, timestamp);
        case kHandleProximity: return fill_proximity(ctx, event, timestamp);
        case kHandleAccelerometer: return fill_accelerometer(ctx, event, timestamp);
        case kHandleMagneticField: return fill_magnetic(ctx, event, timestamp);
        default: return false;
    }
}

int poll_events(sensors_poll_device_t* dev, sensors_event_t* data, int count) {
    if (!data || count <= 0) return -EINVAL;
    Context* ctx = from_device(dev);

    struct DueSensor {
        int handle;
        uint64_t generation;
    };

    for (;;) {
        int produced = 0;
        int due_count = 0;
        int64_t shortest_wait_ns = kDefaultDelayNs;
        std::array<DueSensor, kLastHandle - kFirstHandle + 1> due_sensors{};
        const int64_t now = boot_time_ns();

        // Never keep the state mutex across sysfs I/O. A direct IIO raw read may
        // sleep while the kernel powers and settles a sensor. Holding this lock
        // used to block activate()/setDelay() on the second HIDL binder thread
        // and stalled screen-on for roughly six seconds per accelerometer read.
        {
            std::lock_guard<std::mutex> guard(ctx->lock);
            for (int handle = kFirstHandle; handle <= kLastHandle && produced < count; ++handle) {
                SensorState& state = ctx->state[handle];
                if (state.flush_pending) {
                    sensors_event_t& event = data[produced++];
                    memset(&event, 0, sizeof(event));
                    event.version = META_DATA_VERSION;
                    event.type = SENSOR_TYPE_META_DATA;
                    event.meta_data.what = META_DATA_FLUSH_COMPLETE;
                    event.meta_data.sensor = handle;
                    state.flush_pending = false;
                    continue;
                }
                if (!state.enabled) continue;

                const int64_t due = state.last_timestamp_ns + state.delay_ns;
                if (state.last_timestamp_ns == 0 || now >= due) {
                    if (due_count < static_cast<int>(due_sensors.size())) {
                        due_sensors[due_count++] = {handle, state.generation};
                    }
                } else {
                    shortest_wait_ns = std::min(shortest_wait_ns, due - now);
                }
            }
        }

        for (int i = 0; i < due_count && produced < count; ++i) {
            sensors_event_t event{};
            const int handle = due_sensors[i].handle;
            const int64_t timestamp = boot_time_ns();
            const bool read_ok = fill_event(ctx, handle, &event, timestamp);

            std::lock_guard<std::mutex> guard(ctx->lock);
            SensorState& state = ctx->state[handle];
            if (!state.enabled || state.generation != due_sensors[i].generation) continue;
            // Account for an attempted sample even when the IIO read failed or
            // ON_CHANGE value was unchanged. Otherwise the due check can
            // retry at 5ms intervals (CPU churn after transient sysfs errors).
            state.last_timestamp_ns = timestamp;
            if (!read_ok) continue;
            if (handle == kHandleProximity || handle == kHandleLight) {
                const float value = handle == kHandleProximity ? event.distance : event.light;
                if (state.last_on_change_valid && value == state.last_on_change_value)
                    continue;
                state.last_on_change_valid = true;
                state.last_on_change_value = value;
            }
            data[produced++] = event;
        }

        if (produced > 0) return produced;
        const int64_t sleep_ns = std::clamp<int64_t>(shortest_wait_ns, 5000000LL, 100000000LL);
        std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
    }
}

int open_sensors(const hw_module_t* module, const char*, hw_device_t** device) {
    auto* ctx = new Context();
    ctx->cm36653 = find_iio_device({"cm36653"});
    ctx->accelerometer = find_iio_device({"lis2dh12_accel", "lis2dh12"});
    ctx->magnetometer = find_iio_device({"yas532"});

    ALOGI("IIO paths: cm36653=%s accel=%s mag=%s",
          ctx->cm36653.base.c_str(), ctx->accelerometer.base.c_str(),
          ctx->magnetometer.base.c_str());
    ctx->accel_matrix = read_mount_matrix(ctx->accelerometer, kVendorAccelMatrix, "accelerometer");
    ctx->mag_matrix = read_mount_matrix(ctx->magnetometer, kVendorMagMatrix, "magnetometer");
    const float accel_common =
            read_scale(ctx->accelerometer.base, {"in_accel_scale"}, 0.001f * kStandardGravity);
    ctx->accel_scale = {read_scale(ctx->accelerometer.base, {"in_accel_x_scale"}, accel_common),
                        read_scale(ctx->accelerometer.base, {"in_accel_y_scale"}, accel_common),
                        read_scale(ctx->accelerometer.base, {"in_accel_z_scale"}, accel_common)};
    ctx->mag_scale = {read_scale(ctx->magnetometer.base, {"in_magn_x_scale", "in_mag_x_scale"}, 0.001f),
                      read_scale(ctx->magnetometer.base, {"in_magn_y_scale", "in_mag_y_scale"}, 0.001f),
                      read_scale(ctx->magnetometer.base, {"in_magn_z_scale", "in_mag_z_scale"}, 0.001f)};
    ALOGI("scales: accel=[%g %g %g] magn=[%g %g %g]", ctx->accel_scale[0], ctx->accel_scale[1],
          ctx->accel_scale[2], ctx->mag_scale[0], ctx->mag_scale[1], ctx->mag_scale[2]);
    ALOGI("V173 wake fix: sensor state mutex is never held across IIO reads");

    ctx->device.v0.common.tag = HARDWARE_DEVICE_TAG;
    ctx->device.v0.common.version = SENSORS_DEVICE_API_VERSION_1_3;
    ctx->device.v0.common.module = const_cast<hw_module_t*>(module);
    ctx->device.v0.common.close = close_device;
    ctx->device.v0.activate = activate;
    ctx->device.v0.setDelay = set_delay;
    ctx->device.v0.poll = poll_events;
    ctx->device.batch = batch;
    ctx->device.flush = flush;
    *device = &ctx->device.v0.common;
    return 0;
}

hw_module_methods_t module_methods = {
    .open = open_sensors,
};

}  // namespace

extern "C" {

sensors_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = SENSORS_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = SENSORS_HARDWARE_MODULE_ID,
        .name = "SM-T311 IIO Sensors HAL",
        .author = "Paweł Jarczak",
        .methods = &module_methods,
    },
    .get_sensors_list = get_sensors_list,
};

}  // extern "C"
