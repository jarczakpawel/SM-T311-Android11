#include <hardware/hardware.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TRACE_PATH "/data/gps/v415-sensor-loader.log"
#define SENSOR_ID "sensors"
#define SENSOR_PATH "/system/lib/hw/sensors.default.so"

static void trace_line(const char* text) {
    int fd = open(TRACE_PATH, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0660);
    if (fd < 0) return;
    (void)write(fd, text, strlen(text));
    (void)write(fd, "\n", 1);
    close(fd);
}

typedef int (*hw_get_module_fn)(const char*, const struct hw_module_t**);

int hw_get_module(const char* id, const struct hw_module_t** module) {
    if (id == NULL || module == NULL) return -EINVAL;

    if (strcmp(id, SENSOR_ID) != 0) {
        hw_get_module_fn real_fn = (hw_get_module_fn)dlsym(RTLD_NEXT, "hw_get_module");
        if (real_fn == NULL) {
            trace_line("V415_SENSOR_HW_GET_MODULE=FAIL reason=REAL_SYMBOL_MISSING");
            return -ENOENT;
        }
        return real_fn(id, module);
    }

    void* handle = dlopen(SENSOR_PATH, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        char buf[384];
        const char* err = dlerror();
        snprintf(buf, sizeof(buf), "V415_SENSOR_HW_GET_MODULE=FAIL reason=DLOPEN path=%s error=%s",
                 SENSOR_PATH, err != NULL ? err : "unknown");
        trace_line(buf);
        return -EINVAL;
    }

    const struct hw_module_t* hmi =
        (const struct hw_module_t*)dlsym(handle, "HMI");
    if (hmi == NULL || hmi->id == NULL || strcmp(hmi->id, SENSOR_ID) != 0) {
        trace_line("V415_SENSOR_HW_GET_MODULE=FAIL reason=BAD_HMI");
        return -EINVAL;
    }

    *module = hmi;
    trace_line("V415_SENSOR_HW_GET_MODULE=PASS path=/system/lib/hw/sensors.default.so");
    return 0;
}
