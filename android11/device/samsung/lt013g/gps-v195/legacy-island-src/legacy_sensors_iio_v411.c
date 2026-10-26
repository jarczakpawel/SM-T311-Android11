#include <hardware/hardware.h>
#include <hardware/sensors.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define H_LIGHT 1
#define H_PROX 2
#define H_ACCEL 3
#define H_MAG 4
#define H_FIRST H_LIGHT
#define H_LAST H_MAG
#define NS_PER_SEC 1000000000LL
#define LOG_PATH "/data/gps/v411-legacy-sensors.log"
#ifndef SENSORS_DEVICE_API_VERSION_1_0
#define SENSORS_DEVICE_API_VERSION_1_0 0x100
#endif

struct sensor_state {
    int enabled;
    int64_t delay_ns;
    int64_t last_ns;
};

struct legacy_ctx {
    struct sensors_poll_device_t dev;
    pthread_mutex_t lock;
    struct sensor_state state[H_LAST + 1];
    char cm36653[256];
    char accel[256];
    char mag[256];
    int prox_near;
};

static void trace_line(const char* text) {
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0660);
    if (fd < 0) return;
    (void)write(fd, text, strlen(text));
    (void)write(fd, "\n", 1);
    close(fd);
}

static void trace_paths(const char* cm, const char* accel, const char* mag) {
    char buf[896];
    int n = snprintf(buf, sizeof(buf), "V411_API25_SENSOR_OPEN cm36653=%s accel=%s mag=%s",
                     cm[0] ? cm : "missing", accel[0] ? accel : "missing", mag[0] ? mag : "missing");
    if (n > 0) trace_line(buf);
}

__attribute__((constructor)) static void v411_loaded(void) {
    trace_line("V411_API25_SENSOR_LOAD=PASS");
}

static int64_t boot_ns(void) {
    struct timespec ts;
    memset(&ts, 0, sizeof(ts));
    if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * NS_PER_SEC + ts.tv_nsec;
}

static int read_text(const char* path, char* out, size_t out_size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    ssize_t n = read(fd, out, out_size - 1);
    int saved = errno;
    close(fd);
    if (n <= 0) return n == 0 ? -EIO : -saved;
    out[n] = '\0';
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ' || out[n - 1] == '\t')) out[--n] = '\0';
    return 0;
}

static int read_float_file(const char* path, float* value) {
    char buf[96];
    if (read_text(path, buf, sizeof(buf)) != 0) return -1;
    char* end = NULL;
    errno = 0;
    float v = strtof(buf, &end);
    if (errno != 0 || end == buf) return -1;
    *value = v;
    return 0;
}

static int read_named_float(const char* base, const char* a, const char* b, float* value) {
    char path[384];
    if (a != NULL) {
        snprintf(path, sizeof(path), "%s/%s", base, a);
        if (read_float_file(path, value) == 0) return 0;
    }
    if (b != NULL) {
        snprintf(path, sizeof(path), "%s/%s", base, b);
        if (read_float_file(path, value) == 0) return 0;
    }
    return -1;
}

static void find_iio(const char* n1, const char* n2, char* out, size_t out_size) {
    DIR* d = opendir("/sys/bus/iio/devices");
    if (d == NULL) return;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "iio:device", 10) != 0) continue;
        char base[384], name_path[448], name[128];
        snprintf(base, sizeof(base), "/sys/bus/iio/devices/%s", e->d_name);
        snprintf(name_path, sizeof(name_path), "%s/name", base);
        if (read_text(name_path, name, sizeof(name)) != 0) continue;
        if (strcmp(name, n1) == 0 || (n2 != NULL && strcmp(name, n2) == 0)) {
            snprintf(out, out_size, "%s", base);
            break;
        }
    }
    closedir(d);
}

static float scale_or(const char* base, const char* a, const char* b, float fallback) {
    float v = fallback;
    if (base[0] != '\0') (void)read_named_float(base, a, b, &v);
    return v;
}

static int read_xyz(const char* base, const char* prefix, float* x, float* y, float* z) {
    if (base[0] == '\0') return -1;
    char px[384], py[384], pz[384];
    snprintf(px, sizeof(px), "%s/in_%s_x_raw", base, prefix);
    snprintf(py, sizeof(py), "%s/in_%s_y_raw", base, prefix);
    snprintf(pz, sizeof(pz), "%s/in_%s_z_raw", base, prefix);
    return read_float_file(px, x) == 0 && read_float_file(py, y) == 0 && read_float_file(pz, z) == 0 ? 0 : -1;
}

static struct sensor_t g_sensors[] = {
    { .name="CM36653 Light Sensor", .vendor="Capella Microsystems", .version=1, .handle=H_LIGHT,
      .type=SENSOR_TYPE_LIGHT, .maxRange=10240.0f, .resolution=1.0f, .power=0.75f, .minDelay=0,
      .fifoReservedEventCount=0, .fifoMaxEventCount=0, .stringType=SENSOR_STRING_TYPE_LIGHT,
      .requiredPermission="", .maxDelay=0, .flags=SENSOR_FLAG_CONTINUOUS_MODE },
    { .name="CM36653 Proximity Sensor", .vendor="Capella Microsystems", .version=1, .handle=H_PROX,
      .type=SENSOR_TYPE_PROXIMITY, .maxRange=5.0f, .resolution=5.0f, .power=0.75f, .minDelay=0,
      .fifoReservedEventCount=0, .fifoMaxEventCount=0, .stringType=SENSOR_STRING_TYPE_PROXIMITY,
      .requiredPermission="", .maxDelay=0, .flags=SENSOR_FLAG_WAKE_UP },
    { .name="LIS3DH Acceleration Sensor", .vendor="STMicroelectronics", .version=1, .handle=H_ACCEL,
      .type=SENSOR_TYPE_ACCELEROMETER, .maxRange=39.2266f, .resolution=0.00980665f, .power=0.20f, .minDelay=10000,
      .fifoReservedEventCount=0, .fifoMaxEventCount=0, .stringType=SENSOR_STRING_TYPE_ACCELEROMETER,
      .requiredPermission="", .maxDelay=0, .flags=SENSOR_FLAG_CONTINUOUS_MODE },
    { .name="MS-3R (YAS532) Magnetic Sensor", .vendor="Yamaha Corporation", .version=1, .handle=H_MAG,
      .type=SENSOR_TYPE_MAGNETIC_FIELD, .maxRange=2000.0f, .resolution=0.06f, .power=6.8f, .minDelay=10000,
      .fifoReservedEventCount=0, .fifoMaxEventCount=0, .stringType=SENSOR_STRING_TYPE_MAGNETIC_FIELD,
      .requiredPermission="", .maxDelay=0, .flags=SENSOR_FLAG_CONTINUOUS_MODE },
};

static int get_list(struct sensors_module_t* module, struct sensor_t const** list) {
    (void)module;
    *list = g_sensors;
    trace_line("V411_API25_SENSOR_GET_LIST count=4");
    return 4;
}

static struct legacy_ctx* ctx_from_dev(struct sensors_poll_device_t* dev) { return (struct legacy_ctx*)dev; }

static int close_dev(struct hw_device_t* hw) {
    struct legacy_ctx* ctx = (struct legacy_ctx*)hw;
    trace_line("V411_API25_SENSOR_CLOSE");
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    return 0;
}

static int activate_dev(struct sensors_poll_device_t* dev, int handle, int enabled) {
    if (handle < H_FIRST || handle > H_LAST) return -EINVAL;
    struct legacy_ctx* ctx = ctx_from_dev(dev);
    pthread_mutex_lock(&ctx->lock);
    ctx->state[handle].enabled = enabled != 0;
    ctx->state[handle].last_ns = 0;
    pthread_mutex_unlock(&ctx->lock);
    char buf[128];
    snprintf(buf, sizeof(buf), "V411_API25_SENSOR_ACTIVATE handle=%d enabled=%d", handle, enabled != 0);
    trace_line(buf);
    return 0;
}

static int set_delay_dev(struct sensors_poll_device_t* dev, int handle, int64_t ns) {
    if (handle < H_FIRST || handle > H_LAST || ns <= 0) return -EINVAL;
    if (ns < 10000000LL) ns = 10000000LL;
    if (ns > NS_PER_SEC) ns = NS_PER_SEC;
    struct legacy_ctx* ctx = ctx_from_dev(dev);
    pthread_mutex_lock(&ctx->lock);
    ctx->state[handle].delay_ns = ns;
    pthread_mutex_unlock(&ctx->lock);
    return 0;
}

static int fill_event(struct legacy_ctx* ctx, int handle, sensors_event_t* ev, int64_t ts) {
    memset(ev, 0, sizeof(*ev));
    ev->version = sizeof(*ev);
    ev->sensor = handle;
    ev->timestamp = ts;
    if (handle == H_LIGHT) {
        float raw = 0.0f;
        if (read_named_float(ctx->cm36653, "in_illuminance_raw", "in_intensity_clear_raw", &raw) != 0) return -1;
        ev->type = SENSOR_TYPE_LIGHT; ev->light = raw < 0.0f ? 0.0f : raw; return 0;
    }
    if (handle == H_PROX) {
        float raw = 0.0f;
        if (read_named_float(ctx->cm36653, "in_proximity_raw", NULL, &raw) != 0) return -1;
        if (raw >= 10.0f) ctx->prox_near = 1; else if (raw <= 8.0f) ctx->prox_near = 0;
        ev->type = SENSOR_TYPE_PROXIMITY; ev->distance = ctx->prox_near ? 0.0f : 5.0f; return 0;
    }
    if (handle == H_ACCEL) {
        float x=0.0f,y=0.0f,z=0.0f;
        if (read_xyz(ctx->accel, "accel", &x, &y, &z) != 0) return -1;
        float scale = scale_or(ctx->accel, "in_accel_scale", NULL, 0.00980665f);
        ev->type = SENSOR_TYPE_ACCELEROMETER;
        ev->acceleration.x = -x * scale; ev->acceleration.y = y * scale; ev->acceleration.z = -z * scale;
        ev->acceleration.status = SENSOR_STATUS_ACCURACY_HIGH; return 0;
    }
    if (handle == H_MAG) {
        float x=0.0f,y=0.0f,z=0.0f;
        if (read_xyz(ctx->mag, "magn", &x, &y, &z) != 0 && read_xyz(ctx->mag, "mag", &x, &y, &z) != 0) return -1;
        float sx=scale_or(ctx->mag,"in_magn_x_scale","in_mag_x_scale",0.001f);
        float sy=scale_or(ctx->mag,"in_magn_y_scale","in_mag_y_scale",0.001f);
        float sz=scale_or(ctx->mag,"in_magn_z_scale","in_mag_z_scale",0.001f);
        ev->type = SENSOR_TYPE_MAGNETIC_FIELD;
        ev->magnetic.x = -x*sx*100.0f; ev->magnetic.y = y*sy*100.0f; ev->magnetic.z = -z*sz*100.0f;
        ev->magnetic.status = SENSOR_STATUS_ACCURACY_MEDIUM; return 0;
    }
    return -1;
}

static int poll_dev(struct sensors_poll_device_t* dev, sensors_event_t* data, int count) {
    if (data == NULL || count <= 0) return -EINVAL;
    struct legacy_ctx* ctx = ctx_from_dev(dev);
    for (;;) {
        int produced = 0, enabled_any = 0;
        int64_t now = boot_ns();
        for (int h = H_FIRST; h <= H_LAST && produced < count; ++h) {
            int enabled; int64_t delay,last;
            pthread_mutex_lock(&ctx->lock);
            enabled=ctx->state[h].enabled; delay=ctx->state[h].delay_ns; last=ctx->state[h].last_ns;
            pthread_mutex_unlock(&ctx->lock);
            if (!enabled) continue;
            enabled_any=1;
            if (last != 0 && now-last < delay) continue;
            sensors_event_t ev;
            if (fill_event(ctx,h,&ev,now)==0) {
                data[produced++]=ev;
                pthread_mutex_lock(&ctx->lock); if (ctx->state[h].enabled) ctx->state[h].last_ns=now; pthread_mutex_unlock(&ctx->lock);
            }
        }
        if (produced > 0) return produced;
        usleep(enabled_any ? 10000 : 100000);
    }
}

static int open_sensors(const struct hw_module_t* module, const char* id, struct hw_device_t** device) {
    (void)id;
    struct legacy_ctx* ctx = (struct legacy_ctx*)calloc(1, sizeof(*ctx));
    if (ctx == NULL) return -ENOMEM;
    pthread_mutex_init(&ctx->lock, NULL);
    for (int h=H_FIRST; h<=H_LAST; ++h) ctx->state[h].delay_ns=200000000LL;
    find_iio("cm36653",NULL,ctx->cm36653,sizeof(ctx->cm36653));
    find_iio("lis2dh12_accel","lis2dh12",ctx->accel,sizeof(ctx->accel));
    find_iio("yas532",NULL,ctx->mag,sizeof(ctx->mag));
    trace_paths(ctx->cm36653,ctx->accel,ctx->mag);
    ctx->dev.common.tag=HARDWARE_DEVICE_TAG;
    ctx->dev.common.version=SENSORS_DEVICE_API_VERSION_1_0;
    ctx->dev.common.module=(struct hw_module_t*)module;
    ctx->dev.common.close=close_dev;
    ctx->dev.activate=activate_dev;
    ctx->dev.setDelay=set_delay_dev;
    ctx->dev.poll=poll_dev;
    *device=&ctx->dev.common;
    return 0;
}

static struct hw_module_methods_t g_methods = { .open = open_sensors };

struct sensors_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag=HARDWARE_MODULE_TAG,
        .module_api_version=SENSORS_MODULE_API_VERSION_0_1,
        .hal_api_version=HARDWARE_HAL_API_VERSION,
        .id=SENSORS_HARDWARE_MODULE_ID,
        .name="SM-T311 API25 IIO Sensors Compatibility HAL",
        .author="SM-T311 compatibility layer",
        .methods=&g_methods,
    },
    .get_sensors_list=get_list,
};
