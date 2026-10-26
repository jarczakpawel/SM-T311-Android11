/*
 * G807: conservative Android11 SM-T311 USB bootstrap prototype.
 * SPDX-License-Identifier: MIT
 *
 * Never mounts FunctionFS, toggles UDC, writes ConfigFS, or starts adbd.
 * Init owns mounts; Android Gadget HAL owns physical USB configuration.
 * This prototype is not claimed binary/behavior-equivalent to OEM V115.
 * G807_HOST_TEST redirects all probes and property writes to fake fixtures.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#if defined(__ANDROID__) && !defined(G807_HOST_TEST) && !defined(G808_READONLY_DIAGNOSTIC)
#include <sys/system_properties.h>
#endif

#define ADB_MOUNT "/dev/usb-ffs/adb"
#define UDC_CLASS "/sys/class/udc/12480000.usb"
#define ADB_FUNCTION "/config/usb_gadget/g1/functions/ffs.adb"
#define GADGET_CONFIG "/config/usb_gadget/g1/configs/b.1"
#ifndef G808_READONLY_DIAGNOSTIC
#define BOOTSTRAP_PROP "vendor.smt311.usb.bootstrap"
#endif
#ifdef G807_HOST_TEST
static const char *test_root(void) {
    const char *s = getenv("G807_TEST_ROOT");
    return s && s[0] == '/' && !strstr(s, "..") ? s : NULL;
}
#endif
static int rooted(const char *path, char *dst, size_t length) {
#ifdef G807_HOST_TEST
    const char *root = test_root();
    if (!root) return -1;
    int n = snprintf(dst, length, "%s%s", root, path);
#else
    int n = snprintf(dst, length, "%s", path);
#endif
    return n > 0 && (size_t)n < length ? 0 : -1;
}
static bool isdir(const char *name) {
    char path[PATH_MAX];
    struct stat st;
    return rooted(name, path, sizeof(path)) == 0 &&
           stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
static bool ffs_mounted(void) {
    char path[PATH_MAX];
    if (rooted("/proc/self/mountinfo", path, sizeof(path))) return false;
    FILE *fp = fopen(path, "r");
    if (!fp) return false;
    char *line = NULL;
    size_t cap = 0;
    bool found = false;
    while (getline(&line, &cap, fp) != -1) {
        char *sep = strstr(line, " - ");
        if (!sep) continue;
        /* mountinfo after " - ": fstype, source, mount options.
         * Reject FunctionFS instances mounted for MTP/PTP, even if a
         * broken setup accidentally places them at the ADB mountpoint.
         */
        char fstype[32] = {0}, device[32] = {0};
        if (sscanf(sep + 3, "%31s %31s", fstype, device) != 2 ||
            strcmp(fstype, "functionfs") || strcmp(device, "adb"))
            continue;
        char *prefix = strndup(line, (size_t)(sep - line));
        if (!prefix) break;
        unsigned idx = 0;
        char *save = NULL;
        for (char *field = strtok_r(prefix, " ", &save);
             field; field = strtok_r(NULL, " ", &save)) {
            if (++idx == 5 && strcmp(field, ADB_MOUNT) == 0) {
                found = true;
                break;
            }
        }
        free(prefix);
        if (found) break;
    }
    free(line);
    fclose(fp);
    return found;
}
struct status {
    bool mount, mountdir, udc, function, configuration;
};
static struct status probe(void) {
    return (struct status){
        ffs_mounted(), isdir(ADB_MOUNT), isdir(UDC_CLASS),
        isdir(ADB_FUNCTION), isdir(GADGET_CONFIG)
    };
}
static bool ready(struct status s) {
    return s.mount && s.mountdir && s.udc && s.function && s.configuration;
}
#ifndef G808_READONLY_DIAGNOSTIC
static int signal_ready(void) {
#ifdef G807_HOST_TEST
    char path[PATH_MAX];
    if (rooted("/test-result-property", path, sizeof(path))) return -1;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    const char value[] = BOOTSTRAP_PROP "=ready\n";
    ssize_t w = write(fd, value, sizeof(value) - 1);
    int c = close(fd);
    return w == (ssize_t)(sizeof(value)-1) && !c ? 0 : -1;
#else
    if (geteuid() != 0) return -1;
#ifdef __ANDROID__
    return __system_property_set(BOOTSTRAP_PROP, "ready");
#else
    return -1;
#endif
#endif
}
#endif /* !G808_READONLY_DIAGNOSTIC */
int main(int argc, char **argv) {
#ifdef G808_READONLY_DIAGNOSTIC
    /* Compile-time reduction: no bootstrap/property write code even links. */
    if (argc != 2 || strcmp(argv[1], "timeline")) {
        fprintf(stderr, "G808 diagnostic build only supports timeline\n");
        return 64;
    }
#else
    if (argc != 2 || (strcmp(argv[1], "bootstrap") && strcmp(argv[1], "timeline"))) {
        fprintf(stderr, "usage: %s bootstrap|timeline\n", argv[0]);
        return 64;
    }
#endif /* diagnostic-only argument switch */
#ifdef G807_HOST_TEST
    if (!test_root()) return 65;
#endif
    bool diagnostic = strcmp(argv[1], "timeline") == 0;
    for (unsigned i = 0; i < (diagnostic ? 1u : 30u); ++i) {
        struct status s = probe();
        if (diagnostic) {
            printf("ffs_mount=%d ffs_dir=%d udc=%d function=%d config=%d ready=%d\n",
                   s.mount, s.mountdir, s.udc, s.function, s.configuration, ready(s));
            return 0;
        }
#ifndef G808_READONLY_DIAGNOSTIC
        if (ready(s)) {
            if (!signal_ready()) return 0;
            fprintf(stderr, "G807 property service rejected readiness; no bootstrap success\n");
            return 70;
        }
#endif
        if (i != 29u) {
            struct timespec wait = {0,100000000};
            while (nanosleep(&wait, &wait) < 0 && errno == EINTR) {}
        }
    }
    fprintf(stderr, "G807 readiness incomplete; no USB changes\n");
    return 75;
}
