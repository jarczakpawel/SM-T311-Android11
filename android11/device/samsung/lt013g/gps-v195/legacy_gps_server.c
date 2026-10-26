#define _GNU_SOURCE 1
#include "bridge_protocol.h"
#include "legacy_lto_v2_guard.h"

#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>


#ifdef LT013G_GPSD_CRASHTRACE
#include <ucontext.h>

static void v352_write_all(int fd, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n <= 0) return;
        buf += n;
        len -= (size_t)n;
    }
}

static size_t v352_append_text(char *buf, size_t off, size_t cap, const char *text) {
    while (*text && off < cap) buf[off++] = *text++;
    return off;
}

static size_t v352_append_hex(char *buf, size_t off, size_t cap, uintptr_t value) {
    static const char hex[] = "0123456789abcdef";
    int shift;
    off = v352_append_text(buf, off, cap, "0x");
    for (shift = (int)(sizeof(uintptr_t) * 8) - 4; shift >= 0 && off < cap; shift -= 4)
        buf[off++] = hex[(value >> shift) & 0xfu];
    return off;
}

static size_t v352_append_dec(char *buf, size_t off, size_t cap, unsigned value) {
    char tmp[16];
    size_t n = 0;
    if (value == 0) {
        if (off < cap) buf[off++] = '0';
        return off;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n && off < cap) buf[off++] = tmp[--n];
    return off;
}

static void v352_dump_maps(void) {
    char buf[1024];
    ssize_t n;
    int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    static const char begin[] = "V352_GPSD_MAPS_BEGIN\n";
    static const char end[] = "V352_GPSD_MAPS_END\n";
    v352_write_all(STDERR_FILENO, begin, sizeof(begin) - 1);
    if (fd >= 0) {
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            v352_write_all(STDERR_FILENO, buf, (size_t)n);
        close(fd);
    }
    v352_write_all(STDERR_FILENO, end, sizeof(end) - 1);
}

static void v352_crash_handler(int sig, siginfo_t *info, void *opaque) {
    (void)opaque;
    char line[512];
    size_t off = 0;
    uintptr_t pc = 0, lr = 0, sp = 0, fp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0;
#if defined(__arm__)
    ucontext_t *uc = (ucontext_t *)opaque;
    if (uc) {
        pc = (uintptr_t)uc->uc_mcontext.arm_pc;
        lr = (uintptr_t)uc->uc_mcontext.arm_lr;
        sp = (uintptr_t)uc->uc_mcontext.arm_sp;
        fp = (uintptr_t)uc->uc_mcontext.arm_fp;
        r0 = (uintptr_t)uc->uc_mcontext.arm_r0;
        r1 = (uintptr_t)uc->uc_mcontext.arm_r1;
        r2 = (uintptr_t)uc->uc_mcontext.arm_r2;
        r3 = (uintptr_t)uc->uc_mcontext.arm_r3;
    }
#endif
    off = v352_append_text(line, off, sizeof(line), "V352_GPSD_CRASH sig=");
    off = v352_append_dec(line, off, sizeof(line), (unsigned)sig);
    off = v352_append_text(line, off, sizeof(line), " fault=");
    off = v352_append_hex(line, off, sizeof(line), (uintptr_t)(info ? info->si_addr : 0));
    off = v352_append_text(line, off, sizeof(line), " pc=");
    off = v352_append_hex(line, off, sizeof(line), pc);
    off = v352_append_text(line, off, sizeof(line), " lr=");
    off = v352_append_hex(line, off, sizeof(line), lr);
    off = v352_append_text(line, off, sizeof(line), " sp=");
    off = v352_append_hex(line, off, sizeof(line), sp);
    off = v352_append_text(line, off, sizeof(line), " fp=");
    off = v352_append_hex(line, off, sizeof(line), fp);
    off = v352_append_text(line, off, sizeof(line), " r0=");
    off = v352_append_hex(line, off, sizeof(line), r0);
    off = v352_append_text(line, off, sizeof(line), " r1=");
    off = v352_append_hex(line, off, sizeof(line), r1);
    off = v352_append_text(line, off, sizeof(line), " r2=");
    off = v352_append_hex(line, off, sizeof(line), r2);
    off = v352_append_text(line, off, sizeof(line), " r3=");
    off = v352_append_hex(line, off, sizeof(line), r3);
    if (off < sizeof(line)) line[off++] = '\n';
    v352_write_all(STDERR_FILENO, line, off);
    _exit(128 + sig);
}

__attribute__((constructor))
static void v352_crashtrace_init(void) {
    struct sigaction action;
    static const char ready[] = "V352_GPSD_CRASH_TRACER_READY\n";
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = v352_crash_handler;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    (void)sigaction(SIGSEGV, &action, NULL);
    (void)sigaction(SIGBUS, &action, NULL);
    v352_write_all(STDERR_FILENO, ready, sizeof(ready) - 1);
    v352_dump_maps();
}

#else
#define GPS_MAX_SVS 32
#define GNSS_MAX_SVS 64

typedef int64_t GpsUtcTime;
typedef uint16_t GpsLocationFlags;
typedef uint16_t GpsStatusValue;
typedef uint16_t GpsAidingData;
typedef uint32_t GpsPositionMode;
typedef uint32_t GpsPositionRecurrence;
typedef uint8_t GnssConstellationType;
typedef uint8_t GnssSvFlags;

/* Android legacy gps.h capability bits.  This server intentionally mirrors the
 * API25 ABI without including Android 11 headers. */
enum {
    GPS_CAPABILITY_MSB = 0x00000002u,
    GPS_CAPABILITY_MSA = 0x00000004u,
};

typedef struct {
    size_t size;
    GpsLocationFlags flags;
    double latitude;
    double longitude;
    double altitude;
    float speed;
    float bearing;
    float accuracy;
    GpsUtcTime timestamp;
} GpsLocation;

typedef struct {
    size_t size;
    GpsStatusValue status;
} GpsStatus;

/* Samsung lt01 API25 ABI: one extra 32-bit field follows azimuth. */
typedef struct {
    size_t size;
    int prn;
    float snr;
    float elevation;
    float azimuth;
    int unknown_samsung_field;
} GpsSvInfo;

typedef struct {
    size_t size;
    int num_svs;
    GpsSvInfo sv_list[GPS_MAX_SVS];
    uint32_t ephemeris_mask;
    uint32_t almanac_mask;
    uint32_t used_in_fix_mask;
} GpsSvStatus;

typedef struct {
    size_t size;
    int16_t svid;
    GnssConstellationType constellation;
    float c_n0_dbhz;
    float elevation;
    float azimuth;
    GnssSvFlags flags;
} GnssSvInfo;

typedef struct {
    size_t size;
    int num_svs;
    GnssSvInfo gnss_sv_list[GNSS_MAX_SVS];
} GnssSvStatus;

typedef struct {
    size_t size;
    uint16_t year_of_hw;
} GnssSystemInfo;

#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(GpsSvInfo) == 24, "Samsung API25 GpsSvInfo ABI drift");
_Static_assert(offsetof(GpsSvStatus, sv_list) == 8, "API25 GpsSvStatus sv_list offset drift");
_Static_assert(offsetof(GpsSvStatus, ephemeris_mask) == 776, "Samsung API25 GpsSvStatus mask offset drift");
_Static_assert(sizeof(GpsSvStatus) == 788, "Samsung API25 GpsSvStatus ABI drift");
_Static_assert(sizeof(GnssSvInfo) == 24, "API25 GnssSvInfo ABI drift");
_Static_assert(sizeof(GnssSvStatus) == 1544, "API25 GnssSvStatus ABI drift");
#endif

typedef void (*gps_location_callback)(GpsLocation *location);
typedef void (*gps_status_callback)(GpsStatus *status);
typedef void (*gps_sv_status_callback)(GpsSvStatus *sv_info);
typedef void (*gnss_sv_status_callback)(GnssSvStatus *sv_info);
typedef void (*gps_nmea_callback)(GpsUtcTime timestamp, const char *nmea, int length);
typedef void (*gps_set_capabilities)(uint32_t capabilities);
typedef void (*gps_acquire_wakelock)(void);
typedef void (*gps_release_wakelock)(void);
typedef pthread_t (*gps_create_thread)(const char *name, void (*start)(void *), void *arg);
typedef void (*gps_request_utc_time)(void);
typedef void (*gnss_set_system_info)(const GnssSystemInfo *info);

typedef struct {
    size_t size;
    gps_location_callback location_cb;
    gps_status_callback status_cb;
    gps_sv_status_callback sv_status_cb;
    gps_nmea_callback nmea_cb;
    gps_set_capabilities set_capabilities_cb;
    gps_acquire_wakelock acquire_wakelock_cb;
    gps_release_wakelock release_wakelock_cb;
    gps_create_thread create_thread_cb;
    gps_request_utc_time request_utc_time_cb;
    gnss_set_system_info set_system_info_cb;
    gnss_sv_status_callback gnss_sv_status_cb;
} GpsCallbacks;

#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(GpsCallbacks) == 48, "API25 GpsCallbacks ABI drift");
#endif

typedef struct {
    size_t size;
    int (*init)(GpsCallbacks *callbacks);
    int (*start)(void);
    int (*stop)(void);
    void (*cleanup)(void);
    int (*inject_time)(GpsUtcTime time, int64_t time_reference, int uncertainty);
    int (*inject_location)(double latitude, double longitude, float accuracy);
    void (*delete_aiding_data)(GpsAidingData flags);
    int (*set_position_mode)(GpsPositionMode mode,
                             GpsPositionRecurrence recurrence,
                             uint32_t min_interval,
                             uint32_t preferred_accuracy,
                             uint32_t preferred_time);
    const void *(*get_extension)(const char *name);
} GpsInterface;

typedef const GpsInterface *(*get_gps_interface_fn)(void);

typedef void (*api25_xtra_download_request)(void);
typedef struct {
    api25_xtra_download_request download_request_cb;
    gps_create_thread create_thread_cb;
} Api25GpsXtraCallbacks;
typedef struct {
    size_t size;
    int (*init)(Api25GpsXtraCallbacks *callbacks);
    int (*inject_xtra_data)(char *data, int length);
} Api25GpsXtraInterface;

typedef uint16_t Api25AGpsType;
typedef uint16_t Api25AGpsStatusValue;
typedef uint16_t Api25ApnIpType;
typedef uint16_t Api25AGpsSetIDType;
typedef uint16_t Api25AGpsRefLocationType;
typedef struct {
    size_t size;
    Api25AGpsType type;
    Api25AGpsStatusValue status;
    uint32_t ipaddr;
    struct sockaddr_storage addr;
} Api25AGpsStatus;
typedef void (*api25_agps_status_callback)(Api25AGpsStatus *status);
typedef struct {
    api25_agps_status_callback status_cb;
    gps_create_thread create_thread_cb;
} Api25AGpsCallbacks;
typedef struct {
    size_t size;
    void (*init)(Api25AGpsCallbacks *callbacks);
    int (*data_conn_open)(const char *apn);
    int (*data_conn_closed)(void);
    int (*data_conn_failed)(void);
    int (*set_server)(Api25AGpsType type, const char *hostname, int port);
    int (*data_conn_open_with_apn_ip_type)(const char *apn, Api25ApnIpType apn_ip_type);
} Api25AGpsInterface;

/* Samsung/Broadcom AGPS RIL ABI used by legacy Samsung GPS blobs.
 * Historical Samsung hardware/gps.h inserts one 16-bit "magic" field
 * between LAC and CID. Natural ARM alignment keeps the outer union at 4
 * and places CID at byte 16 of AGpsRefLocation. The legacy gpsd still
 * expects the set_ref_location size argument to remain 16. */
typedef struct {
    Api25AGpsRefLocationType type;
    uint16_t mcc;
    uint16_t mnc;
    uint16_t lac;
    uint16_t foo;
    uint32_t cid;
} Api25AGpsRefLocationCellID;
typedef struct { uint8_t mac[6]; } Api25AGpsRefLocationMac;
typedef struct {
    Api25AGpsRefLocationType type;
    union {
        Api25AGpsRefLocationCellID cellID;
        Api25AGpsRefLocationMac mac;
    } u;
} Api25AGpsRefLocation;

typedef void (*api25_ni_notify_callback)(void *notification);
typedef struct {
    api25_ni_notify_callback notify_cb;
    gps_create_thread create_thread_cb;
} Api25GpsNiCallbacks;
typedef struct {
    size_t size;
    void (*init)(Api25GpsNiCallbacks *callbacks);
} Api25GpsNiInitInterface;

typedef void (*api25_agps_ril_request_set_id)(uint32_t flags);
typedef void (*api25_agps_ril_request_ref_loc)(uint32_t flags);
typedef struct {
    api25_agps_ril_request_set_id request_setid;
    api25_agps_ril_request_ref_loc request_refloc;
    gps_create_thread create_thread_cb;
} Api25AGpsRilCallbacks;
typedef struct {
    size_t size;
    void (*init)(Api25AGpsRilCallbacks *callbacks);
    void (*set_ref_location)(const Api25AGpsRefLocation *location, size_t size);
    void (*set_set_id)(Api25AGpsSetIDType type, const char *setid);
    void (*ni_message)(uint8_t *msg, size_t len);
    void (*update_network_state)(int connected, int type, int roaming, const char *extra_info);
    void (*update_network_availability)(int available, const char *apn);
} Api25AGpsRilInterface;

#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(Api25GpsXtraCallbacks) == 8, "API25 GpsXtraCallbacks ABI drift");
_Static_assert(sizeof(Api25AGpsCallbacks) == 8, "API25 AGpsCallbacks ABI drift");
_Static_assert(sizeof(Api25GpsNiCallbacks) == 8, "API25 GpsNiCallbacks ABI drift");
_Static_assert(sizeof(Api25AGpsRilCallbacks) == 12, "API25 AGpsRilCallbacks ABI drift");
_Static_assert(sizeof(Api25AGpsRefLocationCellID) == 16, "Samsung magic AGpsRefLocationCellID ABI drift");
_Static_assert(offsetof(Api25AGpsRefLocation, u) == 4, "Samsung magic AGpsRefLocation union offset drift");
_Static_assert(offsetof(Api25AGpsRefLocation, u.cellID.type) == 4, "Samsung magic AGpsRefLocation cell type offset drift");
_Static_assert(offsetof(Api25AGpsRefLocation, u.cellID.mcc) == 6, "Samsung magic AGpsRefLocation mcc offset drift");
_Static_assert(offsetof(Api25AGpsRefLocation, u.cellID.mnc) == 8, "Samsung magic AGpsRefLocation mnc offset drift");
_Static_assert(offsetof(Api25AGpsRefLocation, u.cellID.lac) == 10, "Samsung magic AGpsRefLocation lac offset drift");
_Static_assert(offsetof(Api25AGpsRefLocation, u.cellID.foo) == 12, "Samsung magic AGpsRefLocation foo offset drift");
_Static_assert(offsetof(Api25AGpsRefLocation, u.cellID.cid) == 16, "Samsung magic AGpsRefLocation cid offset drift");
_Static_assert(sizeof(Api25AGpsRefLocation) == 20, "Samsung magic AGpsRefLocation ABI drift");
#endif

static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_send_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_client_fd = -1;
static pid_t g_gpsd_pid = -1;
static int g_want_gpsd;
static int g_tracking;
static int g_idle_stop_expected;
static volatile sig_atomic_t g_stop_server;
static volatile sig_atomic_t g_retire_after_response;
static uint32_t g_gpsd_generation;
static void *g_hal_handle;
static const GpsInterface *g_iface;
static GpsCallbacks g_legacy_callbacks;
static Api25GpsXtraCallbacks g_v331_xtra_callbacks;
static const Api25GpsXtraInterface *g_xtra_iface;
static uint8_t *g_xtra_buffer;
static uint32_t g_xtra_total;
static uint32_t g_xtra_received;
static int g_xtra_pending;
static Api25AGpsCallbacks g_v331_agps_callbacks;
static const Api25AGpsInterface *g_agps_iface;
static Api25GpsNiCallbacks g_v331_ni_callbacks;
static Api25AGpsRilCallbacks g_v331_agps_ril_callbacks;
static const Api25AGpsRilInterface *g_agps_ril_iface;
static pthread_mutex_t g_hal_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_mode_valid;
static GpsPositionMode g_mode;
static GpsPositionRecurrence g_recurrence;
static uint32_t g_min_interval;
static uint32_t g_preferred_accuracy;
static uint32_t g_preferred_time;
static int g_close_fd_limit = 1024;
/*
 * Event source for gpsd death and server shutdown.  The signal handler only
 * performs an async-signal-safe write(); all waitpid/recovery work remains in
 * the monitor thread.
 */
static int g_signal_pipe[2] = { -1, -1 };

static void log_line(const char *fmt, ...) {
    char buffer[768];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof(buffer)) n = (int)sizeof(buffer) - 1;
    if (n == 0 || buffer[n - 1] != '\n') {
        if ((size_t)n + 1u < sizeof(buffer)) buffer[n++] = '\n';
    }
    (void)write(STDERR_FILENO, buffer, (size_t)n);
}

static void stage_line(const char *text) {
    int fd = open("/data/gps/v290-preload-stage.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0660);
    if (fd < 0) return;
    (void)write(fd, text, strlen(text));
    (void)write(fd, "\n", 1);
    close(fd);
}


static const char *v355_operation_mode_text(GpsPositionMode mode) {
    switch (mode) {
        case 0: return "STANDALONE";
        case 1: return "MS BASED";
        case 2: return "MS ASSISTED";
        default: return NULL;
    }
}

static int v355_write_full(int fd, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        if (n == 0) return -EIO;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

static int v355_sync_secgps_mode(GpsPositionMode mode) {
    static const char path[] = "/data/data/com.android.angryGps/secgps.conf";
    static const char tmp[] = "/data/data/com.android.angryGps/secgps.conf.v355.tmp";
    char input[8192];
    char output[9216];
    const char *mode_text = v355_operation_mode_text(mode);
    ssize_t nr = 0;
    size_t out = 0;
    int fd;
    int rc = 0;
    int have_test_mode = 0;
    const char *p;
    const char *end;

    if (!mode_text) {
        log_line("V355_SECGPS_MODE_SYNC=FAIL reason=UNSUPPORTED_MODE mode=%u", (unsigned)mode);
        return -EINVAL;
    }

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        nr = read(fd, input, sizeof(input) - 1u);
        if (nr < 0) rc = -errno;
        close(fd);
        if (rc != 0) return rc;
    } else if (errno != ENOENT) {
        return -errno;
    }
    if (nr < 0) return (int)nr;
    input[nr] = '\0';

    p = input;
    end = input + nr;
    while (p < end) {
        const char *line_end = memchr(p, '\n', (size_t)(end - p));
        size_t len = line_end ? (size_t)(line_end - p + 1) : (size_t)(end - p);
        size_t content_len = len;
        while (content_len > 0 && (p[content_len - 1] == '\n' || p[content_len - 1] == '\r'))
            content_len--;

        if (content_len >= sizeof("OPERATION_MODE=") - 1u &&
            memcmp(p, "OPERATION_MODE=", sizeof("OPERATION_MODE=") - 1u) == 0) {
            p += len;
            continue;
        }
        if (content_len >= sizeof("OPERATION_TEST_MODE=") - 1u &&
            memcmp(p, "OPERATION_TEST_MODE=", sizeof("OPERATION_TEST_MODE=") - 1u) == 0)
            have_test_mode = 1;

        if (out + len >= sizeof(output)) return -E2BIG;
        memcpy(output + out, p, len);
        out += len;
        p += len;
    }

    if (out > 0 && output[out - 1] != '\n') {
        if (out + 1u >= sizeof(output)) return -E2BIG;
        output[out++] = '\n';
    }
    if (!have_test_mode) {
        static const char test_line[] = "OPERATION_TEST_MODE=0\n";
        if (out + sizeof(test_line) - 1u >= sizeof(output)) return -E2BIG;
        memcpy(output + out, test_line, sizeof(test_line) - 1u);
        out += sizeof(test_line) - 1u;
    }
    {
        int n = snprintf(output + out, sizeof(output) - out,
                         "OPERATION_MODE=%s\n", mode_text);
        if (n < 0 || (size_t)n >= sizeof(output) - out) return -E2BIG;
        out += (size_t)n;
    }

    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0660);
    if (fd < 0) return -errno;
    if (fchmod(fd, 0660) != 0) rc = -errno;
    if (rc == 0) rc = v355_write_full(fd, output, out);
    if (rc == 0 && fsync(fd) != 0) rc = -errno;
    if (close(fd) != 0 && rc == 0) rc = -errno;
    if (rc != 0) {
        (void)unlink(tmp);
        log_line("V355_SECGPS_MODE_SYNC=FAIL mode=%u text=%s rc=%d", (unsigned)mode, mode_text, rc);
        return rc;
    }
    if (rename(tmp, path) != 0) {
        rc = -errno;
        (void)unlink(tmp);
        log_line("V355_SECGPS_MODE_SYNC=FAIL mode=%u text=%s rc=%d", (unsigned)mode, mode_text, rc);
        return rc;
    }

    log_line("V355_SECGPS_MODE_SYNC=PASS mode=%u text=%s bytes=%u",
             (unsigned)mode, mode_text, (unsigned)out);
    return 0;
}


static void packet_init(struct lt013g_bridge_packet *packet, uint32_t type, uint32_t sequence) {
    memset(packet, 0, sizeof(*packet));
    packet->magic = LT013G_BRIDGE_MAGIC;
    packet->version = LT013G_BRIDGE_VERSION;
    packet->type = type;
    packet->sequence = sequence;
}

static int send_packet(const struct lt013g_bridge_packet *packet) {
    int fd;
    ssize_t written;
    pthread_mutex_lock(&g_send_lock);
    fd = g_client_fd;
    if (fd < 0) {
        pthread_mutex_unlock(&g_send_lock);
        return -ENOTCONN;
    }
    do {
        written = send(fd, packet, sizeof(*packet), MSG_NOSIGNAL);
    } while (written < 0 && errno == EINTR);
    {
        int saved_errno = errno;
        if (written != (ssize_t)sizeof(*packet)) {
            log_line("V307_SERVER_SEND_FAIL type=%u seq=%u fd=%d written=%zd errno=%d (%s)",
                     packet->type, packet->sequence, fd, written, saved_errno,
                     written < 0 ? strerror(saved_errno) : "short-send");
            shutdown(fd, SHUT_RDWR);
        }
        pthread_mutex_unlock(&g_send_lock);
        if (written != (ssize_t)sizeof(*packet)) return written < 0 ? -saved_errno : -EIO;
    }
    return 0;
}

static void send_response(uint32_t sequence, int result) {
    struct lt013g_bridge_packet packet;
    packet_init(&packet, LT013G_MSG_RESPONSE, sequence);
    packet.result = result;
    (void)send_packet(&packet);
}

static void send_gpsd_event(uint32_t event, uint32_t generation, int status, uint32_t expected) {
    struct lt013g_bridge_packet packet;
    struct lt013g_cb_gpsd_event payload;
    memset(&payload, 0, sizeof(payload));
    payload.event = event;
    payload.generation = generation;
    payload.status = status;
    payload.expected = expected;
    packet_init(&packet, LT013G_CB_GPSD_EVENT, 0);
    packet.payload_size = sizeof(payload);
    memcpy(packet.payload, &payload, sizeof(payload));
    (void)send_packet(&packet);
}

static void location_cb(GpsLocation *location) {
    struct lt013g_bridge_packet packet;
    struct lt013g_cb_location payload;
    const int64_t gps_rollover_ms = 619315200000LL;
    const int64_t rollover_window_ms = 1209600000LL; /* +/- 14 days */
    int64_t timestamp_ms;
    struct timespec rt;
    if (!location) return;
    memset(&payload, 0, sizeof(payload));
    payload.flags = location->flags;
    payload.latitude = location->latitude;
    payload.longitude = location->longitude;
    payload.altitude = location->altitude;
    payload.speed = location->speed;
    payload.bearing = location->bearing;
    payload.accuracy = location->accuracy;
    timestamp_ms = location->timestamp;
    if (timestamp_ms > 0 && clock_gettime(CLOCK_REALTIME, &rt) == 0) {
        const int64_t now_ms = (int64_t)rt.tv_sec * 1000LL + rt.tv_nsec / 1000000LL;
        const int64_t age_ms = now_ms - timestamp_ms;
        if (age_ms > gps_rollover_ms - rollover_window_ms &&
            age_ms < gps_rollover_ms + rollover_window_ms) {
            const int64_t old_ms = timestamp_ms;
            timestamp_ms += gps_rollover_ms;
            static int rollover_logged;
            if (!rollover_logged) {
                rollover_logged = 1;
                log_line("T311_GPS_WEEK_ROLLOVER_FIX old=%lld new=%lld now=%lld age=%lld (logged once)",
                         (long long)old_ms, (long long)timestamp_ms,
                         (long long)now_ms, (long long)age_ms);
            }
        }
    }
    payload.timestamp_ms = timestamp_ms;
    packet_init(&packet, LT013G_CB_LOCATION, 0);
    packet.payload_size = sizeof(payload);
    memcpy(packet.payload, &payload, sizeof(payload));
    (void)send_packet(&packet);
}

static void status_cb(GpsStatus *status) {
    struct lt013g_bridge_packet packet;
    struct lt013g_cb_status payload;
    if (!status) return;
    payload.status = status->status;
    packet_init(&packet, LT013G_CB_STATUS, 0);
    packet.payload_size = sizeof(payload);
    memcpy(packet.payload, &payload, sizeof(payload));
    (void)send_packet(&packet);
}

static void sv_status_cb(GpsSvStatus *status) {
    struct lt013g_bridge_packet packet;
    struct lt013g_cb_sv payload;
    int count;
    int i;
    if (!status) return;
    memset(&payload, 0, sizeof(payload));
    count = status->num_svs;
    if (count < 0) count = 0;
    if (count > (int)LT013G_BRIDGE_MAX_SVS) count = (int)LT013G_BRIDGE_MAX_SVS;
    payload.num_svs = count;
    payload.ephemeris_mask = status->ephemeris_mask;
    payload.almanac_mask = status->almanac_mask;
    payload.used_in_fix_mask = status->used_in_fix_mask;
    for (i = 0; i < count; ++i) {
        payload.sv[i].prn = status->sv_list[i].prn;
        payload.sv[i].snr = status->sv_list[i].snr;
        payload.sv[i].elevation = status->sv_list[i].elevation;
        payload.sv[i].azimuth = status->sv_list[i].azimuth;
    }
    packet_init(&packet, LT013G_CB_SV, 0);
    packet.payload_size = sizeof(payload);
    memcpy(packet.payload, &payload, sizeof(payload));
    (void)send_packet(&packet);
}


static void gnss_sv_status_cb(GnssSvStatus *status) {
    struct lt013g_bridge_packet packet;
    struct lt013g_cb_gnss_sv payload;
    int count;
    int i;
    if (!status) return;
    memset(&payload, 0, sizeof(payload));
    count = status->num_svs;
    if (count < 0) count = 0;
    if (count > (int)LT013G_BRIDGE_MAX_GNSS_SVS) count = (int)LT013G_BRIDGE_MAX_GNSS_SVS;
    payload.num_svs = count;
    for (i = 0; i < count; ++i) {
        payload.sv[i].svid = status->gnss_sv_list[i].svid;
        payload.sv[i].constellation = status->gnss_sv_list[i].constellation;
        payload.sv[i].flags = status->gnss_sv_list[i].flags;
        payload.sv[i].c_n0_dbhz = status->gnss_sv_list[i].c_n0_dbhz;
        payload.sv[i].elevation = status->gnss_sv_list[i].elevation;
        payload.sv[i].azimuth = status->gnss_sv_list[i].azimuth;
    }
    packet_init(&packet, LT013G_CB_GNSS_SV, 0);
    packet.payload_size = sizeof(payload);
    memcpy(packet.payload, &payload, sizeof(payload));
    (void)send_packet(&packet);
}

static void system_info_cb(const GnssSystemInfo *info) {
    struct lt013g_bridge_packet packet;
    struct lt013g_cb_system_info payload;
    if (!info) return;
    memset(&payload, 0, sizeof(payload));
    payload.year_of_hw = info->year_of_hw;
    packet_init(&packet, LT013G_CB_SYSTEM_INFO, 0);
    packet.payload_size = sizeof(payload);
    memcpy(packet.payload, &payload, sizeof(payload));
    (void)send_packet(&packet);
    log_line("V290_API25_SYSTEM_INFO year=%u", (unsigned)info->year_of_hw);
}

static void nmea_cb(GpsUtcTime timestamp, const char *nmea, int length) {
    struct lt013g_bridge_packet packet;
    struct lt013g_cb_nmea payload;
    uint32_t count = 0;
    memset(&payload, 0, sizeof(payload));
    if (nmea && length > 0) {
        count = (uint32_t)length;
        if (count > LT013G_BRIDGE_NMEA_MAX) count = LT013G_BRIDGE_NMEA_MAX;
        memcpy(payload.data, nmea, count);
    }
    payload.timestamp_ms = timestamp;
    payload.length = count;
    packet_init(&packet, LT013G_CB_NMEA, 0);
    packet.payload_size = sizeof(payload);
    memcpy(packet.payload, &payload, sizeof(payload));
    (void)send_packet(&packet);
}

static uint32_t g_v356_extension_mask;
static uint32_t g_v357_last_raw_capabilities;
static int g_v357_have_raw_capabilities;

static uint32_t v357_filter_capabilities(uint32_t capabilities) {
    uint32_t exposed = capabilities;

    /* Assisted position modes are only valid when the corresponding legacy
     * assistance surface is actually exposed to Android.  The vendor blob may
     * report capabilities before extension discovery, so filtering must use
     * the bridge's effective extension contract, not callback timing. */
    if ((g_v356_extension_mask & ((1u << 1) | (1u << 2))) == 0)
        exposed &= ~(GPS_CAPABILITY_MSB | GPS_CAPABILITY_MSA);
    return exposed;
}

static void v357_publish_capabilities(uint32_t raw, const char *reason) {
    struct lt013g_bridge_packet packet;
    struct lt013g_cb_capabilities payload;

    payload.capabilities = v357_filter_capabilities(raw);
    log_line("V357_CAPABILITIES_PUBLISH reason=%s raw_seen=%d raw=0x%08x ext_mask=0x%08x exposed=0x%08x msb=%u msa=%u",
             reason ? reason : "unknown", g_v357_have_raw_capabilities, raw,
             g_v356_extension_mask, payload.capabilities,
             !!(payload.capabilities & GPS_CAPABILITY_MSB),
             !!(payload.capabilities & GPS_CAPABILITY_MSA));
    packet_init(&packet, LT013G_CB_CAPABILITIES, 0);
    packet.payload_size = sizeof(payload);
    memcpy(packet.payload, &payload, sizeof(payload));
    (void)send_packet(&packet);
}

static void capabilities_cb(uint32_t capabilities) {
    g_v357_last_raw_capabilities = capabilities;
    g_v357_have_raw_capabilities = 1;
    log_line("V356_CAPABILITIES_CONTRACT raw=0x%08x ext_mask=0x%08x exposed=0x%08x msb=%u msa=%u",
             capabilities, g_v356_extension_mask, v357_filter_capabilities(capabilities),
             !!(v357_filter_capabilities(capabilities) & GPS_CAPABILITY_MSB),
             !!(v357_filter_capabilities(capabilities) & GPS_CAPABILITY_MSA));
    v357_publish_capabilities(capabilities, "vendor-callback");
}

static void acquire_wakelock_cb(void) {
    struct lt013g_bridge_packet packet;
    packet_init(&packet, LT013G_CB_WAKELOCK_ACQUIRE, 0);
    (void)send_packet(&packet);
}

static void release_wakelock_cb(void) {
    struct lt013g_bridge_packet packet;
    packet_init(&packet, LT013G_CB_WAKELOCK_RELEASE, 0);
    (void)send_packet(&packet);
}

struct thread_args {
    void (*start)(void *);
    void *arg;
};

static void *thread_entry(void *opaque) {
    struct thread_args *args = (struct thread_args *)opaque;
    void (*start)(void *) = args->start;
    void *arg = args->arg;
    free(args);
    start(arg);
    return NULL;
}

static pthread_t create_thread_cb(const char *name, void (*start)(void *), void *arg) {
    struct thread_args *args;
    pthread_attr_t attr;
    pthread_t thread = (pthread_t)0;
    int rc;
    (void)name;
    args = (struct thread_args *)calloc(1, sizeof(*args));
    if (!args) return (pthread_t)0;
    args->start = start;
    args->arg = arg;
    rc = pthread_attr_init(&attr);
    if (rc != 0) {
        free(args);
        return (pthread_t)0;
    }
    rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (rc == 0) rc = pthread_create(&thread, &attr, thread_entry, args);
    (void)pthread_attr_destroy(&attr);
    if (rc != 0) {
        free(args);
        return (pthread_t)0;
    }
    return thread;
}

static void request_utc_time_cb(void) {
    struct lt013g_bridge_packet packet;
    packet_init(&packet, LT013G_CB_REQUEST_UTC, 0);
    (void)send_packet(&packet);
}

static void v331_xtra_download_request_cb(void) {
    struct lt013g_bridge_packet packet;
    packet_init(&packet, LT013G_CB_XTRA_DOWNLOAD_REQUEST, 0);
    log_line("G357_XTRA_DOWNLOAD_REQUEST_TO_ANDROID");
    (void)send_packet(&packet);
}

static void v331_agps_status_cb(Api25AGpsStatus *status) {
    struct lt013g_bridge_packet packet;
    struct lt013g_cb_agps_status payload;
    memset(&payload, 0, sizeof(payload));
    if (!status) return;
    payload.type = status->type;
    payload.status = status->status;
    payload.ipaddr = status->ipaddr;
    if (status->size >= sizeof(Api25AGpsStatus)) {
        payload.family = (uint16_t)status->addr.ss_family;
        if (status->addr.ss_family == AF_INET) {
            const struct sockaddr_in *in = (const struct sockaddr_in *)&status->addr;
            memcpy(payload.address, &in->sin_addr, sizeof(in->sin_addr));
        } else if (status->addr.ss_family == AF_INET6) {
            const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)&status->addr;
            memcpy(payload.address, &in6->sin6_addr, sizeof(in6->sin6_addr));
        }
    }
    log_line("V341_API25_EXT_CB name=agps event=status type=%u status=%u family=%u",
             payload.type, payload.status, payload.family);
    packet_init(&packet, LT013G_CB_AGPS_STATUS, 0);
    packet.payload_size = sizeof(payload);
    memcpy(packet.payload, &payload, sizeof(payload));
    (void)send_packet(&packet);
}

static void v331_ni_notify_cb(void *notification) {
    log_line("V331_API25_EXT_CB name=gps-ni event=notify ptr=%p", notification);
}

static void v331_agps_ril_request_setid_cb(uint32_t flags) {
    struct lt013g_bridge_packet packet;
    struct lt013g_cb_agps_ril_request payload;
    payload.flags = flags;
    log_line("V341_API25_EXT_CB name=agps_ril event=request-setid flags=0x%08" PRIx32, flags);
    packet_init(&packet, LT013G_CB_AGPS_RIL_REQUEST_SETID, 0);
    packet.payload_size = sizeof(payload);
    memcpy(packet.payload, &payload, sizeof(payload));
    (void)send_packet(&packet);
}

static void v331_agps_ril_request_refloc_cb(uint32_t flags) {
    struct lt013g_bridge_packet packet;
    struct lt013g_cb_agps_ril_request payload;
    payload.flags = flags;
    log_line("V341_API25_EXT_CB name=agps_ril event=request-refloc flags=0x%08" PRIx32, flags);
    packet_init(&packet, LT013G_CB_AGPS_RIL_REQUEST_REFLOC, 0);
    packet.payload_size = sizeof(payload);
    memcpy(packet.payload, &payload, sizeof(payload));
    (void)send_packet(&packet);
}

static void __attribute__((unused)) v341_discover_api25_extensions(void) {
    const void *ext;
    uint32_t mask = 0;
    if (!g_iface || !g_iface->get_extension) {
        g_v356_extension_mask = 0;
        log_line("V341_API25_EXT_DISCOVER get_extension=0");
        return;
    }
    ext = g_iface->get_extension("gps-xtra");
    g_xtra_iface = (const Api25GpsXtraInterface *)ext;
    if (g_xtra_iface) mask |= (1u << 0);
    log_line("V341_API25_EXT_DISCOVER name=gps-xtra present=%d size=%u inject=%d",
             g_xtra_iface != NULL,
             g_xtra_iface ? (unsigned)g_xtra_iface->size : 0u,
             g_xtra_iface && g_xtra_iface->inject_xtra_data);

    ext = g_iface->get_extension("agps");
    g_agps_iface = (const Api25AGpsInterface *)ext;
    if (g_agps_iface) mask |= (1u << 1);
    log_line("V341_API25_EXT_DISCOVER name=agps present=%d size=%u",
             g_agps_iface != NULL, g_agps_iface ? (unsigned)g_agps_iface->size : 0u);

    ext = g_iface->get_extension("gps-ni");
    if (ext) {
        const Api25GpsNiInitInterface *iface = (const Api25GpsNiInitInterface *)ext;
        mask |= (1u << 3);
        memset(&g_v331_ni_callbacks, 0, sizeof(g_v331_ni_callbacks));
        g_v331_ni_callbacks.notify_cb = v331_ni_notify_cb;
        g_v331_ni_callbacks.create_thread_cb = create_thread_cb;
        if (iface->init) iface->init(&g_v331_ni_callbacks);
        log_line("V341_API25_EXT_INIT name=gps-ni present=1 init=%s size=%u", iface->init ? "called" : "missing", (unsigned)iface->size);
    } else log_line("V341_API25_EXT_INIT name=gps-ni present=0");

    ext = g_iface->get_extension("agps_ril");
    g_agps_ril_iface = (const Api25AGpsRilInterface *)ext;
    if (g_agps_ril_iface) mask |= (1u << 2);
    g_v356_extension_mask = mask;
    log_line("V341_API25_EXT_DISCOVER name=agps_ril present=%d size=%u",
             g_agps_ril_iface != NULL, g_agps_ril_iface ? (unsigned)g_agps_ril_iface->size : 0u);
    log_line("V356_EXTENSION_MASK_DISCOVERED mask=0x%08x", g_v356_extension_mask);
}

static int load_hal(void);

/*
 * V302 keeps the v301 launch contract unchanged and fixes observability around
 * the API25 gpsd exit(255).  The server stderr now survives chroot/setuid via
 * the launcher's pre-opened fd, so every probe below is evidence from the same
 * uid/mount namespace that immediately execs the legacy gpsd.
 */
static void log_gpsd_runtime_preflight(void) {
    gid_t groups[32];
    int group_count;
    struct stat st;
    int fd;
    char group_buf[256];
    size_t off = 0;

    group_count = getgroups((int)(sizeof(groups) / sizeof(groups[0])), groups);
    group_buf[0] = '\0';
    if (group_count >= 0) {
        int i;
        for (i = 0; i < group_count; ++i) {
            int n = snprintf(group_buf + off, sizeof(group_buf) - off,
                             "%s%u", i ? "," : "", (unsigned)groups[i]);
            if (n < 0 || (size_t)n >= sizeof(group_buf) - off) break;
            off += (size_t)n;
        }
    }
    log_line("V302_GPSD_PREFLIGHT_ID uid=%u gid=%u groups=%s",
             (unsigned)getuid(), (unsigned)getgid(),
             group_count < 0 ? "ERROR" : group_buf);

    if (stat("/system/bin/gpsd", &st) == 0)
        log_line("V302_GPSD_PREFLIGHT_BIN=PASS mode=%#o uid=%u gid=%u size=%lld",
                 (unsigned)st.st_mode, (unsigned)st.st_uid, (unsigned)st.st_gid,
                 (long long)st.st_size);
    else
        log_line("V302_GPSD_PREFLIGHT_BIN=FAIL errno=%d (%s)", errno, strerror(errno));

    if (stat("/dev/ttySAC1", &st) == 0) {
        log_line("V302_GPSD_PREFLIGHT_TTY_STAT=PASS mode=%#o uid=%u gid=%u rdev=%u:%u",
                 (unsigned)st.st_mode, (unsigned)st.st_uid, (unsigned)st.st_gid,
                 (unsigned)major(st.st_rdev), (unsigned)minor(st.st_rdev));
    } else {
        log_line("V302_GPSD_PREFLIGHT_TTY_STAT=FAIL errno=%d (%s)", errno, strerror(errno));
    }
    fd = open("/dev/ttySAC1", O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd >= 0) {
        log_line("V302_GPSD_PREFLIGHT_TTY_OPEN=PASS fd=%d", fd);
        close(fd);
    } else {
        log_line("V302_GPSD_PREFLIGHT_TTY_OPEN=FAIL errno=%d (%s)", errno, strerror(errno));
    }

    fd = open("/sys/class/sec/gps/GPS_PWR_EN/value", O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
        char value[32];
        ssize_t n = pread(fd, value, sizeof(value) - 1, 0);
        if (n > 0) {
            value[n] = '\0';
            log_line("V302_GPSD_PREFLIGHT_POWER_OPEN=PASS fd=%d value=%s", fd, value);
        } else {
            log_line("V302_GPSD_PREFLIGHT_POWER_OPEN=PASS fd=%d read_rc=%ld errno=%d",
                     fd, (long)n, n < 0 ? errno : 0);
        }
        close(fd);
    } else {
        log_line("V302_GPSD_PREFLIGHT_POWER_OPEN=FAIL errno=%d (%s)", errno, strerror(errno));
    }

    if (stat("/data", &st) == 0)
        log_line("V304_GPSD_PREFLIGHT_DATA_PARENT=PASS mode=%#o uid=%u gid=%u",
                 (unsigned)st.st_mode, (unsigned)st.st_uid, (unsigned)st.st_gid);
    else
        log_line("V304_GPSD_PREFLIGHT_DATA_PARENT=FAIL errno=%d (%s)", errno, strerror(errno));

    if (stat("/data/system", &st) == 0)
        log_line("V304_GPSD_PREFLIGHT_SYSTEM_PARENT=PASS mode=%#o uid=%u gid=%u",
                 (unsigned)st.st_mode, (unsigned)st.st_uid, (unsigned)st.st_gid);
    else
        log_line("V304_GPSD_PREFLIGHT_SYSTEM_PARENT=FAIL errno=%d (%s)", errno, strerror(errno));

    if (stat("/data/data", &st) == 0)
        log_line("V347_GPSD_PREFLIGHT_DATA_DATA_PARENT=PASS mode=%#o uid=%u gid=%u",
                 (unsigned)st.st_mode, (unsigned)st.st_uid, (unsigned)st.st_gid);
    else
        log_line("V347_GPSD_PREFLIGHT_DATA_DATA_PARENT=FAIL errno=%d (%s)", errno, strerror(errno));

    if (stat("/data/data/com.android.angryGps", &st) == 0)
        log_line("V347_GPSD_PREFLIGHT_ANGRYGPS_DIR=PASS mode=%#o uid=%u gid=%u",
                 (unsigned)st.st_mode, (unsigned)st.st_uid, (unsigned)st.st_gid);
    else
        log_line("V347_GPSD_PREFLIGHT_ANGRYGPS_DIR=FAIL errno=%d (%s)", errno, strerror(errno));

    fd = open("/data/data/com.android.angryGps/secgps.conf", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        char byte;
        ssize_t n = read(fd, &byte, 1);
        log_line("V347_GPSD_PREFLIGHT_SECGPS_OPEN=PASS fd=%d bytes=%ld", fd, (long)n);
        close(fd);
    } else {
        log_line("V347_GPSD_PREFLIGHT_SECGPS_OPEN=FAIL errno=%d (%s)", errno, strerror(errno));
    }

    if (stat("/data/system/gps", &st) == 0) {
        log_line("V302_GPSD_PREFLIGHT_DATA_STAT=PASS mode=%#o uid=%u gid=%u",
                 (unsigned)st.st_mode, (unsigned)st.st_uid, (unsigned)st.st_gid);
    } else {
        log_line("V302_GPSD_PREFLIGHT_DATA_STAT=FAIL errno=%d (%s)", errno, strerror(errno));
    }
    fd = open("/data/system/gps/.v302-write-probe",
              O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0660);
    if (fd >= 0) {
        static const char ok[] = "ok\n";
        (void)write(fd, ok, sizeof(ok) - 1);
        close(fd);
        (void)unlink("/data/system/gps/.v302-write-probe");
        log_line("V302_GPSD_PREFLIGHT_DATA_WRITE=PASS");
    } else {
        log_line("V302_GPSD_PREFLIGHT_DATA_WRITE=FAIL errno=%d (%s)", errno, strerror(errno));
    }

    if (stat("/sdcard/gps/broadcom/storage", &st) == 0)
        log_line("V302_GPSD_PREFLIGHT_LOGDIR=PASS mode=%#o uid=%u gid=%u",
                 (unsigned)st.st_mode, (unsigned)st.st_uid, (unsigned)st.st_gid);
    else
        log_line("V302_GPSD_PREFLIGHT_LOGDIR=MISS errno=%d (%s)", errno, strerror(errno));

    if (stat("/dev/socket", &st) == 0)
        log_line("V302_GPSD_PREFLIGHT_DEV_SOCKET=PASS mode=%#o uid=%u gid=%u",
                 (unsigned)st.st_mode, (unsigned)st.st_uid, (unsigned)st.st_gid);
    else
        log_line("V302_GPSD_PREFLIGHT_DEV_SOCKET=FAIL errno=%d (%s)", errno, strerror(errno));

    if (stat("/sys/power/wake_lock", &st) == 0) {
        log_line("V304_GPSD_PREFLIGHT_WAKE_LOCK_STAT=PASS mode=%#o uid=%u gid=%u",
                 (unsigned)st.st_mode, (unsigned)st.st_uid, (unsigned)st.st_gid);
        fd = open("/sys/power/wake_lock", O_WRONLY | O_CLOEXEC);
        if (fd >= 0) {
            log_line("V304_GPSD_PREFLIGHT_WAKE_LOCK_OPEN=PASS fd=%d", fd);
            close(fd);
        } else {
            log_line("V304_GPSD_PREFLIGHT_WAKE_LOCK_OPEN=FAIL errno=%d (%s)", errno, strerror(errno));
        }
    } else {
        log_line("V304_GPSD_PREFLIGHT_WAKE_LOCK_STAT=FAIL errno=%d (%s)", errno, strerror(errno));
    }

    log_line("V302_GPSD_PREFLIGHT_ANDROID_SOCKET_GPS=%s",
             getenv("ANDROID_SOCKET_gps") ? getenv("ANDROID_SOCKET_gps") : "UNSET");
}

/*
 * V305 restores the exact Android-init contract used by the working API25
 * image.  Stock init.target.rc declares:
 *   socket gps seqpacket 0660 gps system
 * and exports the inherited fd as ANDROID_SOCKET_gps.  V301 temporarily
 * removed this while exit(255) was still misattributed to the socket; v302-v304
 * proved the real exit(255) cause was the inaccessible .gpsd.lock instead.
 */
static int make_gpsd_init_socket(void) {
    struct sockaddr_un address;
    int fd;
    int flags;

    fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return -errno;

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "/dev/socket/gps") >=
        (int)sizeof(address.sun_path)) {
        close(fd);
        return -ENAMETOOLONG;
    }
    (void)unlink(address.sun_path);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        int saved = errno;
        close(fd);
        return -saved;
    }
    /* Stock: socket gps seqpacket 0660 gps system. */
    if (chown(address.sun_path, 1021, 1000) != 0 || chmod(address.sun_path, 0660) != 0) {
        int saved = errno;
        (void)unlink(address.sun_path);
        close(fd);
        return -saved;
    }
    if (listen(fd, 4) != 0) {
        int saved = errno;
        (void)unlink(address.sun_path);
        close(fd);
        return -saved;
    }

    flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0 || fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC) != 0) {
        int saved = errno;
        (void)unlink(address.sun_path);
        close(fd);
        return -saved;
    }
    return fd;
}


/* v319 diagnostic: capture Samsung OEM GPS requests from the exact API25 gpsd,
 * never forward them to the Android 11 modem/RIL, and emulate the solicited
 * request-complete callback that libsecril-client would normally deliver.
 *
 * v315/v316 only returned RIL_CLIENT_ERR_SUCCESS from InvokeOemRequestHookRaw().
 * Samsung libsecril-client is asynchronous: successful transport submission is
 * followed later by RilOnComplete(client, data, datalen).  Without that callback
 * gpsd can remain blocked at the first OEM GPS request and never expose the next
 * control-plane/frequency-aiding request.  v319 records the handler registered for
 * RIL_REQUEST_OEM_HOOK_RAW (59) and injects an empty successful completion on a
 * detached thread after capture.  No OEM request bytes are sent to any modem.
 *
 * Do NOT use getenv() to distinguish the nested gpsd here.  On this exact old
 * API25 loader getenv("ANDROID_SOCKET_gps") is still NULL while preload
 * constructors run.  Instead scan inherited descriptors for the stock init
 * SOCK_SEQPACKET bound at /dev/socket/gps. */
typedef int (*v319_ril_on_complete_fn)(void *client, const void *data, size_t datalen);
typedef int (*v319_register_request_complete_fn)(void *client, uint32_t id,
                                                 v319_ril_on_complete_fn handler);
typedef void *(*v328_open_client_fn)(void);
typedef int (*v328_client_fn)(void *client);
typedef int (*v328_register_unsolicited_fn)(void *client, uint32_t id,
                                            v319_ril_on_complete_fn handler);
typedef int (*v375_invoke_oem_fn)(void *client, char *data, size_t len);

static int g_v319_gpsd_capture_process;
static int g_v319_gpsd_control_fd = -1;
static unsigned g_v319_oem_capture_seq;
static pthread_mutex_t g_v319_ril_lock = PTHREAD_MUTEX_INITIALIZER;
static void *g_v319_oem_client;
static v319_ril_on_complete_fn g_v319_oem_complete;
static unsigned g_v319_oem_handler_generation;
static int g_v375_real_ril_connected;
static int g_v375_real_oem_handler_ready;
static int g_v375_real_unsol_ready;
static v319_ril_on_complete_fn g_v375_unsol_11009_handler;
static int g_v430_freq_complete_pending;
static unsigned g_v430_freq_complete_seq;
static unsigned g_v430_freq_complete_enable;

static int v319_find_gpsd_control_fd(void) {
    int fd;
    for (fd = 3; fd < 64; ++fd) {
        int type = 0;
        socklen_t type_len = sizeof(type);
        struct sockaddr_un addr;
        socklen_t addr_len = sizeof(addr);
        size_t path_len;

        if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_len) != 0) continue;
        if (type != SOCK_SEQPACKET) continue;
        memset(&addr, 0, sizeof(addr));
        if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) != 0) continue;
        if (addr.sun_family != AF_UNIX) continue;
        if (addr_len <= (socklen_t)offsetof(struct sockaddr_un, sun_path)) continue;
        path_len = (size_t)addr_len - offsetof(struct sockaddr_un, sun_path);
        if (path_len > sizeof(addr.sun_path)) path_len = sizeof(addr.sun_path);
        if (addr.sun_path[0] == '\0') continue;
        if (strncmp(addr.sun_path, "/dev/socket/gps", path_len) == 0 &&
            strlen("/dev/socket/gps") + 1u >= path_len) {
            return fd;
        }
    }
    return -1;
}

static int v319_is_nested_gpsd(void) {
    int fd;
    if (g_v319_gpsd_capture_process) return 1;
    fd = v319_find_gpsd_control_fd();
    if (fd < 0) return 0;
    g_v319_gpsd_capture_process = 1;
    g_v319_gpsd_control_fd = fd;
    return 1;
}

static int v377_loader_probe_only(void) {
    const char *value = getenv("LT013G_BRIDGE_PROBE_ONLY");
    return value && value[0] == '1' && value[1] == '\0';
}

static int v377_multiclient_present(void) {
    static const char needle[] = "Multiclient";
    int fd = open("/proc/net/unix", O_RDONLY | O_CLOEXEC);
    char buf[2048];
    size_t matched = 0;
    ssize_t nr;
    if (fd < 0) return 0;
    while ((nr = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t i;
        for (i = 0; i < nr; ++i) {
            if (buf[i] == needle[matched]) {
                if (++matched == sizeof(needle) - 1u) {
                    close(fd);
                    return 1;
                }
            } else {
                matched = (buf[i] == needle[0]) ? 1u : 0u;
            }
        }
    }
    close(fd);
    return 0;
}

static int v375_real_oem_complete(void *client, const void *data, size_t datalen) {
    v319_ril_on_complete_fn handler;
    int freq_pending;
    unsigned freq_seq;
    unsigned freq_enable;
    char line[768];
    size_t off = 0;
    size_t i;
    pthread_mutex_lock(&g_v319_ril_lock);
    handler = g_v319_oem_complete;
    freq_pending = g_v430_freq_complete_pending;
    freq_seq = g_v430_freq_complete_seq;
    freq_enable = g_v430_freq_complete_enable;
    if (freq_pending) g_v430_freq_complete_pending = 0;
    pthread_mutex_unlock(&g_v319_ril_lock);
    off += (size_t)snprintf(line + off, sizeof(line) - off,
                            "V375_REAL_OEM_COMPLETE client=%p data_len=%zu handler=%s hex=",
                            client, datalen, handler ? "SET" : "NULL");
    if (data) {
        const uint8_t *bytes = (const uint8_t *)data;
        for (i = 0; i < datalen && i < 128 && off + 3 < sizeof(line); ++i)
            off += (size_t)snprintf(line + off, sizeof(line) - off, "%02X", (unsigned)bytes[i]);
    }
    if (off + 2 < sizeof(line)) { line[off++] = '\n'; line[off] = '\0'; }
    (void)write(STDERR_FILENO, line, off);
    if (freq_pending) {
        int n = snprintf(line, sizeof(line),
                         "V430_FREQ_AID_COMPLETE seq=%u enable=%u len=%zu\n",
                         freq_seq, freq_enable, datalen);
        if (n > 0) (void)write(STDERR_FILENO, line,
                               (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
        if (data && datalen == 2u) {
            const uint8_t *bytes = (const uint8_t *)data;
            n = snprintf(line, sizeof(line),
                         "V430_FREQ_AID_COMPLETE_2B seq=%u enable=%u lock_status=%u afc_update=%u\n",
                         freq_seq, freq_enable, (unsigned)bytes[0], (unsigned)bytes[1]);
            if (n > 0) (void)write(STDERR_FILENO, line,
                                   (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
        }
    }
    return handler ? handler(client, data, datalen) : 0;
}

static int v375_unsol_11009(void *client, const void *data, size_t datalen) {
    v319_ril_on_complete_fn handler;
    char line[1024];
    size_t off = 0;
    size_t i;
    pthread_mutex_lock(&g_v319_ril_lock);
    handler = g_v375_unsol_11009_handler;
    pthread_mutex_unlock(&g_v319_ril_lock);
    off += (size_t)snprintf(line + off, sizeof(line) - off,
                            "V375_REAL_RIL_UNSOL_11009 client=%p len=%zu hex=",
                            client, datalen);
    if (data) {
        const uint8_t *bytes = (const uint8_t *)data;
        for (i = 0; i < datalen && i < 256 && off + 3 < sizeof(line); ++i)
            off += (size_t)snprintf(line + off, sizeof(line) - off, "%02X", (unsigned)bytes[i]);
    }
    if (off + 2 < sizeof(line)) { line[off++] = '\n'; line[off] = '\0'; }
    (void)write(STDERR_FILENO, line, off);
    if (handler && data && datalen == 2u) {
        const uint8_t *bytes = (const uint8_t *)data;
        uint8_t compat[3] = { 0x37u, bytes[0], bytes[1] };
        int n = snprintf(line, sizeof(line),
                         "V443_REAL_FREQ_NOTI_COMPAT_2B lock_status=%u afc_update=%u out=37%02X%02X\n",
                         (unsigned)bytes[0], (unsigned)bytes[1],
                         (unsigned)bytes[0], (unsigned)bytes[1]);
        if (n > 0) (void)write(STDERR_FILENO, line,
                               (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
        return handler(client, compat, sizeof(compat));
    }
    if (handler && data && datalen == 9u) {
        const uint8_t *bytes = (const uint8_t *)data;
        if (bytes[0] == 0x09u && bytes[4] == 0x11u && bytes[5] == 0x28u && bytes[6] == 0x03u) {
            uint8_t compat[3] = { 0x37u, bytes[7], bytes[8] };
            int n = snprintf(line, sizeof(line),
                             "V443_REAL_FREQ_NOTI_COMPAT lock_status=%u afc_update=%u in_len=%zu out=37%02X%02X\n",
                             (unsigned)bytes[7], (unsigned)bytes[8], datalen,
                             (unsigned)bytes[7], (unsigned)bytes[8]);
            if (n > 0) (void)write(STDERR_FILENO, line,
                                   (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
            return handler(client, compat, sizeof(compat));
        }
    }
    {
        int n = snprintf(line, sizeof(line),
                         "V443_UNSOL_11009_DROP len=%zu reason=NON_FREQ_ENVELOPE\n", datalen);
        if (n > 0) (void)write(STDERR_FILENO, line,
                               (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
    }
    return 0;
}

void *OpenClient_RILD(void) {
    static v328_open_client_fn real_fn;
    void *client;
    int nested;
    if (v377_loader_probe_only()) {
        static const char msg[] = "V377_PRELOAD_PROBE_RIL_ISOLATION op=OpenClient_RILD action=synthetic-client\n";
        (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return (void *)(uintptr_t)1u;
    }
    nested = v319_is_nested_gpsd();
    if (!real_fn) real_fn = (v328_open_client_fn)dlsym(RTLD_NEXT, "OpenClient_RILD");
    client = real_fn ? real_fn() : NULL;
    if (nested) {
        char line[192];
        int n = snprintf(line, sizeof(line),
                         "V328_REAL_RIL_OPEN pid=%ld client=%p real=%s\n",
                         (long)getpid(), client, real_fn ? "SET" : "MISSING");
        if (n > 0) (void)write(STDERR_FILENO, line, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
    }
    return client;
}

int Connect_RILD(void *client) {
    static v328_client_fn real_fn;
    int nested;
    int present;
    int rc;
    if (v377_loader_probe_only()) {
        static const char msg[] = "V377_PRELOAD_PROBE_RIL_ISOLATION op=Connect_RILD action=capture-connected\n";
        (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return 0;
    }
    nested = v319_is_nested_gpsd();
    if (!real_fn) real_fn = (v328_client_fn)dlsym(RTLD_NEXT, "Connect_RILD");
    if (nested && !client) {
        static const char msg[] = "V335_RIL_NULL_GUARD op=Connect_RILD client=NULL rc=4\n";
        (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return 4;
    }
    if (!nested) return real_fn ? real_fn(client) : 7;
    present = v377_multiclient_present();
    rc = (present && real_fn) ? real_fn(client) : 4;
    pthread_mutex_lock(&g_v319_ril_lock);
    g_v375_real_ril_connected = (rc == 0);
    pthread_mutex_unlock(&g_v319_ril_lock);
    {
        char line[256];
        int n = snprintf(line, sizeof(line),
                         "V375_REAL_RIL_CONNECT pid=%ld client=%p multiclient=%d rc=%d real=%s fallback_capture=%d\n",
                         (long)getpid(), client, present, rc, real_fn ? "SET" : "MISSING", rc == 0 ? 0 : 1);
        if (n > 0) (void)write(STDERR_FILENO, line, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
    }
    return 0;
}

int isConnected_RILD(void *client) {
    static v328_client_fn real_fn;
    int nested;
    int real_connected;
    int value;
    if (v377_loader_probe_only()) return 1;
    nested = v319_is_nested_gpsd();
    if (!real_fn) real_fn = (v328_client_fn)dlsym(RTLD_NEXT, "isConnected_RILD");
    if (nested && !client) {
        static const char msg[] = "V335_RIL_NULL_GUARD op=isConnected_RILD client=NULL value=0\n";
        (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return 0;
    }
    if (!nested) return real_fn ? real_fn(client) : 0;
    pthread_mutex_lock(&g_v319_ril_lock);
    real_connected = g_v375_real_ril_connected;
    pthread_mutex_unlock(&g_v319_ril_lock);
    if (!real_connected) return 1;
    value = real_fn ? real_fn(client) : 0;
    if (!value) {
        pthread_mutex_lock(&g_v319_ril_lock);
        g_v375_real_ril_connected = 0;
        g_v375_real_oem_handler_ready = 0;
        g_v375_real_unsol_ready = 0;
        g_v375_unsol_11009_handler = NULL;
        pthread_mutex_unlock(&g_v319_ril_lock);
    }
    {
        char line[192];
        int n = snprintf(line, sizeof(line),
                         "V375_REAL_RIL_IS_CONNECTED pid=%ld client=%p value=%d\n",
                         (long)getpid(), client, value);
        if (n > 0) (void)write(STDERR_FILENO, line, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
    }
    return value;
}

int RegisterUnsolicitedHandler(void *client, uint32_t id,
                               v319_ril_on_complete_fn handler) {
    static v328_register_unsolicited_fn real_fn;
    v319_ril_on_complete_fn real_handler = handler;
    int rc;
    int nested;
    if (v377_loader_probe_only()) {
        static const char msg[] = "V377_PRELOAD_PROBE_RIL_ISOLATION op=RegisterUnsolicitedHandler action=noop-success\n";
        (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return 0;
    }
    nested = v319_is_nested_gpsd();
    if (!real_fn) {
        real_fn = (v328_register_unsolicited_fn)
            dlsym(RTLD_NEXT, "RegisterUnsolicitedHandler");
    }
    if (nested && id == 11009u) {
        pthread_mutex_lock(&g_v319_ril_lock);
        g_v375_unsol_11009_handler = handler;
        pthread_mutex_unlock(&g_v319_ril_lock);
        real_handler = handler ? v375_unsol_11009 : NULL;
    }
    rc = real_fn ? real_fn(client, id, real_handler) : 7;
    if (nested && id == 11009u) {
        pthread_mutex_lock(&g_v319_ril_lock);
        g_v375_real_unsol_ready = (rc == 0 && handler != NULL);
        pthread_mutex_unlock(&g_v319_ril_lock);
    }
    if (nested) {
        char line[256];
        int n = snprintf(line, sizeof(line),
                         "V328_REAL_RIL_REGISTER_UNSOL pid=%ld client=%p id=%" PRIu32 " handler=%s rc=%d real=%s wrapped=%d\n",
                         (long)getpid(), client, id, handler ? "SET" : "CLEAR",
                         rc, real_fn ? "SET" : "MISSING", id == 11009u && handler ? 1 : 0);
        if (n > 0) (void)write(STDERR_FILENO, line, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
    }
    return rc;
}

int CloseClient_RILD(void *client) {
    static v328_client_fn real_fn;
    int rc;
    int nested;
    if (v377_loader_probe_only()) return 0;
    nested = v319_is_nested_gpsd();
    if (!real_fn) real_fn = (v328_client_fn)dlsym(RTLD_NEXT, "CloseClient_RILD");
    rc = real_fn ? real_fn(client) : 7;
    if (nested) {
        char line[192];
        pthread_mutex_lock(&g_v319_ril_lock);
        g_v375_real_ril_connected = 0;
        g_v375_real_oem_handler_ready = 0;
        g_v375_real_unsol_ready = 0;
        g_v375_unsol_11009_handler = NULL;
        pthread_mutex_unlock(&g_v319_ril_lock);
        int n = snprintf(line, sizeof(line),
                         "V328_REAL_RIL_CLOSE pid=%ld client=%p rc=%d real=%s\n",
                         (long)getpid(), client, rc, real_fn ? "SET" : "MISSING");
        if (n > 0) (void)write(STDERR_FILENO, line, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
    }
    return rc;
}

int RegisterRequestCompleteHandler(void *client, uint32_t id,
                                   v319_ril_on_complete_fn handler) {
    static v319_register_request_complete_fn real_fn;
    v319_ril_on_complete_fn real_handler = handler;
    int rc;
    int nested;

    if (v377_loader_probe_only()) {
        static const char msg[] = "V377_PRELOAD_PROBE_RIL_ISOLATION op=RegisterRequestCompleteHandler action=noop-success\n";
        (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return 0;
    }
    nested = v319_is_nested_gpsd();
    if (!real_fn) {
        real_fn = (v319_register_request_complete_fn)
            dlsym(RTLD_NEXT, "RegisterRequestCompleteHandler");
    }
    if (!real_fn) {
        if (nested) {
            static const char msg[] =
                "V319_REQUEST_COMPLETE_REGISTER_REAL_MISSING\n";
            (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
        }
        return 7;
    }

    if (nested && id == 59u) {
        pthread_mutex_lock(&g_v319_ril_lock);
        g_v319_oem_client = client;
        g_v319_oem_complete = handler;
        ++g_v319_oem_handler_generation;
        pthread_mutex_unlock(&g_v319_ril_lock);
        real_handler = handler ? v375_real_oem_complete : NULL;
    }
    rc = real_fn(client, id, real_handler);
    if (nested && id == 59u) {
        pthread_mutex_lock(&g_v319_ril_lock);
        g_v375_real_oem_handler_ready = (rc == 0 && handler != NULL);
        pthread_mutex_unlock(&g_v319_ril_lock);
    }
    if (nested) {
        char line[256];
        int n;
        if (id == 59u) {
            pthread_mutex_lock(&g_v319_ril_lock);
            n = snprintf(line, sizeof(line),
                         "V319_REQUEST_COMPLETE_REGISTER id=%" PRIu32
                         " handler=%s generation=%u real_rc=%d wrapped=%d\n",
                         id, handler ? "SET" : "CLEAR",
                         g_v319_oem_handler_generation, rc, handler ? 1 : 0);
            pthread_mutex_unlock(&g_v319_ril_lock);
        } else {
            n = snprintf(line, sizeof(line),
                         "V319_REQUEST_COMPLETE_REGISTER_OTHER id=%" PRIu32
                         " handler=%s real_rc=%d\n",
                         id, handler ? "SET" : "CLEAR", rc);
        }
        if (n > 0) {
            size_t out = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
            (void)write(STDERR_FILENO, line, out);
        }
    }
    return rc;
}

struct v319_complete_job {
    void *client;
    v319_ril_on_complete_fn callback;
    unsigned seq;
    unsigned generation;
};

static void *v319_complete_thread(void *opaque) {
    struct v319_complete_job *job = (struct v319_complete_job *)opaque;
    char line[256];
    int cb_rc;
    int n;

    /* Preserve the asynchronous ordering of a real RIL response: the public
     * InvokeOemRequestHookRaw() returns first, then the client reader normally
     * invokes the completion callback. */
    usleep(20000);
    cb_rc = job->callback(job->client, NULL, 0);
    n = snprintf(line, sizeof(line),
                 "V319_REQUEST_COMPLETE_INJECT seq=%u generation=%u "
                 "data_len=0 callback_rc=%d\n",
                 job->seq, job->generation, cb_rc);
    if (n > 0) {
        size_t out = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
        (void)write(STDERR_FILENO, line, out);
    }
    free(job);
    return NULL;
}

static void v319_schedule_request_complete(unsigned seq) {
    struct v319_complete_job *job;
    pthread_t thread;
    int rc;
    char line[256];
    int n;
    void *client;
    v319_ril_on_complete_fn callback;
    unsigned generation;

    pthread_mutex_lock(&g_v319_ril_lock);
    client = g_v319_oem_client;
    callback = g_v319_oem_complete;
    generation = g_v319_oem_handler_generation;
    pthread_mutex_unlock(&g_v319_ril_lock);

    if (!client || !callback) {
        n = snprintf(line, sizeof(line),
                     "V319_REQUEST_COMPLETE_MISSING seq=%u client=%s callback=%s\n",
                     seq, client ? "SET" : "NULL", callback ? "SET" : "NULL");
        if (n > 0) {
            size_t out = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
            (void)write(STDERR_FILENO, line, out);
        }
        return;
    }

    job = (struct v319_complete_job *)calloc(1, sizeof(*job));
    if (!job) {
        static const char msg[] = "V319_REQUEST_COMPLETE_THREAD_ALLOC_FAIL\n";
        (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return;
    }
    job->client = client;
    job->callback = callback;
    job->seq = seq;
    job->generation = generation;

    rc = pthread_create(&thread, NULL, v319_complete_thread, job);
    if (rc != 0) {
        n = snprintf(line, sizeof(line),
                     "V319_REQUEST_COMPLETE_THREAD_FAIL seq=%u rc=%d\n", seq, rc);
        if (n > 0) {
            size_t out = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
            (void)write(STDERR_FILENO, line, out);
        }
        free(job);
        return;
    }
    (void)pthread_detach(thread);
    n = snprintf(line, sizeof(line),
                 "V319_REQUEST_COMPLETE_SCHEDULE seq=%u generation=%u delay_ms=20\n",
                 seq, generation);
    if (n > 0) {
        size_t out = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
        (void)write(STDERR_FILENO, line, out);
    }
}

int InvokeOemRequestHookRaw(void *client, char *data, size_t len) {
    static v375_invoke_oem_fn real_fn;
    char line[2048];
    size_t off = 0;
    size_t i;
    unsigned func_id = 0;
    unsigned subf_id = 0;
    unsigned declared_len = 0;
    unsigned seq;
    int real_connected;
    int handler_ready;
    int unsol_ready;
    int is_freq_aiding;

    if (v377_loader_probe_only()) return 0;
    if (!v319_is_nested_gpsd()) return 7;
    if (!client) {
        static const char msg[] = "V335_RIL_NULL_GUARD op=InvokeOemRequestHookRaw client=NULL rc=4\n";
        (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return 4;
    }
    if (!real_fn) real_fn = (v375_invoke_oem_fn)dlsym(RTLD_NEXT, "InvokeOemRequestHookRaw");
    if (data && len > 0) func_id = (unsigned)(uint8_t)data[0];
    if (data && len > 1) subf_id = (unsigned)(uint8_t)data[1];
    if (data && len > 3)
        declared_len = ((unsigned)(uint8_t)data[2] << 8) |
                       (unsigned)(uint8_t)data[3];

    seq = ++g_v319_oem_capture_seq;
    off += (size_t)snprintf(line + off, sizeof(line) - off,
                            "V319_OEM_RIL_CAPTURE seq=%u pid=%ld control_fd=%d "
                            "len=%zu func=0x%02X cmd=0x%02X declared_len_be=%u "
                            "captured_bytes=%zu truncated=%u hex=",
                            seq, (long)getpid(), g_v319_gpsd_control_fd,
                            len, func_id, subf_id, declared_len,
                            len < 512u ? len : 512u, len > 512u ? 1u : 0u);
    if (data) {
        for (i = 0; i < len && i < 512 && off + 3 < sizeof(line); ++i)
            off += (size_t)snprintf(line + off, sizeof(line) - off, "%02X",
                                    (unsigned)(uint8_t)data[i]);
    }
    if (off + 2 < sizeof(line)) {
        line[off++] = '\n';
        line[off] = '\0';
    }
    (void)write(STDERR_FILENO, line, off);

    pthread_mutex_lock(&g_v319_ril_lock);
    real_connected = g_v375_real_ril_connected;
    handler_ready = g_v375_real_oem_handler_ready;
    unsol_ready = g_v375_real_unsol_ready;
    pthread_mutex_unlock(&g_v319_ril_lock);
    is_freq_aiding = data && len == 5u &&
                     (uint8_t)data[0] == 0x0eu &&
                     (uint8_t)data[1] == 0x37u &&
                     (uint8_t)data[2] == 0x00u &&
                     (uint8_t)data[3] == 0x05u &&
                     ((uint8_t)data[4] == 0x00u || (uint8_t)data[4] == 0x01u);

    if (is_freq_aiding && real_connected && handler_ready && unsol_ready && real_fn) {
        uint8_t translated[13] = {
            0x0eu, 0x37u, 0x00u, 0x0du,
            0x00u,
            0x08u, 0x00u, 0x00u, 0x00u, 0x11u, 0x28u, 0x03u, 0x00u
        };
        unsigned enable = (unsigned)(uint8_t)data[4];
        int rc;
        char forward_line[320];
        int n;
        translated[12] = (uint8_t)enable;
        pthread_mutex_lock(&g_v319_ril_lock);
        g_v430_freq_complete_pending = 1;
        g_v430_freq_complete_seq = seq;
        g_v430_freq_complete_enable = enable;
        pthread_mutex_unlock(&g_v319_ril_lock);
        rc = real_fn(client, (char *)translated, sizeof(translated));
        if (rc != 0) {
            pthread_mutex_lock(&g_v319_ril_lock);
            if (g_v430_freq_complete_pending && g_v430_freq_complete_seq == seq)
                g_v430_freq_complete_pending = 0;
            pthread_mutex_unlock(&g_v319_ril_lock);
        }
        n = snprintf(forward_line, sizeof(forward_line),
                     "V441_FREQ_IPC41_TRANSLATE seq=%u enable=%u original=0E370005%02X outer=0E37000D00 inner=08000000112803%02X rc=%d\n",
                     seq, enable, enable, enable, rc);
        if (n > 0) (void)write(STDERR_FILENO, forward_line,
                               (size_t)n < sizeof(forward_line) ? (size_t)n : sizeof(forward_line) - 1);
        n = snprintf(forward_line, sizeof(forward_line),
                     "V375_FREQ_AID_REAL_FORWARD seq=%u enable=%u len=%zu rc=%d\n",
                     seq, enable, len, rc);
        if (n > 0) (void)write(STDERR_FILENO, forward_line,
                               (size_t)n < sizeof(forward_line) ? (size_t)n : sizeof(forward_line) - 1);
        if (rc == 0) return 0;
    }

    {
        char fallback_line[256];
        int n = snprintf(fallback_line, sizeof(fallback_line),
                         "V375_OEM_CAPTURE_FALLBACK seq=%u freq=%d real_connected=%d handler_ready=%d unsol_ready=%d real=%s\n",
                         seq, is_freq_aiding, real_connected, handler_ready, unsol_ready,
                         real_fn ? "SET" : "MISSING");
        if (n > 0) (void)write(STDERR_FILENO, fallback_line,
                               (size_t)n < sizeof(fallback_line) ? (size_t)n : sizeof(fallback_line) - 1);
    }
    v319_schedule_request_complete(seq);
    return 0;
}

static int spawn_gpsd_locked(void) {
    pid_t child;
    int control_fd;
    if (g_gpsd_pid > 0 && kill(g_gpsd_pid, 0) == 0) return 0;

    log_line("V305_GPSD_LAUNCH_CONTRACT=STOCK_INIT_SOCKET_GPS");
    log_gpsd_runtime_preflight();
    control_fd = make_gpsd_init_socket();
    if (control_fd < 0) {
        log_line("V305_GPSD_INIT_SOCKET_FAIL rc=%d", control_fd);
        return control_fd;
    }
    log_line("V305_GPSD_INIT_SOCKET=PASS fd=%d type=SEQPACKET mode=0660 uid=1021 gid=1000 path=/dev/socket/gps",
             control_fd);

    child = fork();
    if (child < 0) {
        int saved = errno;
        close(control_fd);
        (void)unlink("/dev/socket/gps");
        return -saved;
    }
    if (child == 0) {
        int fd;
        char socket_env[64];
        char *const argv[] = {"/system/bin/gpsd", "-c", "/system/etc/gps.xml", NULL};
        char *envp[] = {
            "LD_LIBRARY_PATH=/system/lib:/system/vendor/lib",
            "LD_PRELOAD=/system/lib/liblt013g-gps-bridge-v290.so",
            "ANDROID_ROOT=/system",
            "ANDROID_DATA=/data",
            "TZ=GMT0",
            socket_env,
            NULL,
        };
        static const char exec_marker[] = "V305_GPSD_CHILD=execve-enter\n";
        static const char exec_fail[] = "V305_GPSD_CHILD=execve-failed\n";
        (void)snprintf(socket_env, sizeof(socket_env), "ANDROID_SOCKET_gps=%d", control_fd);
        for (fd = 3; fd < g_close_fd_limit; ++fd) {
            if (fd != control_fd) (void)close(fd);
        }
        (void)write(STDERR_FILENO, exec_marker, sizeof(exec_marker) - 1);
        execve(argv[0], argv, envp);
        (void)write(STDERR_FILENO, exec_fail, sizeof(exec_fail) - 1);
        _exit(127);
    }
    close(control_fd);
    g_gpsd_pid = child;
    ++g_gpsd_generation;
    if (g_gpsd_generation == 0) g_gpsd_generation = 1;
    log_line("V201_GPSD_SPAWN pid=%ld generation=%u", (long)child, g_gpsd_generation);
    send_gpsd_event(LT013G_GPSD_EVENT_SPAWN, g_gpsd_generation, 0, 0);
    return 0;
}

static int wait_child_timeout(pid_t pid, int *status, unsigned timeout_ms) {
    unsigned waited = 0;
    while (waited < timeout_ms) {
        pid_t result = waitpid(pid, status, WNOHANG);
        if (result == pid) return 1;
        if (result < 0) {
            if (errno == EINTR) continue;
            return errno == ECHILD ? 1 : -errno;
        }
        usleep(10000);
        waited += 10;
    }
    return 0;
}

static void stop_gpsd_locked(void) {
    pid_t pid = g_gpsd_pid;
    uint32_t generation = g_gpsd_generation;
    int status = 0;
    int reaped;
    g_want_gpsd = 0;
    g_gpsd_pid = -1;
    if (pid <= 0) return;
    (void)kill(pid, SIGTERM);
    reaped = wait_child_timeout(pid, &status, 500);
    if (reaped <= 0) {
        (void)kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
    (void)unlink("/dev/socket/gps");
    log_line("V195_GPSD_STOP pid=%ld generation=%u status=%d", (long)pid, generation, status);
    send_gpsd_event(LT013G_GPSD_EVENT_EXIT, generation, status, 1);
}

static void *gpsd_monitor(void *unused) {
    unsigned char events[64];
    (void)unused;
    for (;;) {
        ssize_t n;
        do {
            n = read(g_signal_pipe[0], events, sizeof(events));
        } while (n < 0 && errno == EINTR);

        if (g_stop_server) break;
        if (n < 0 && errno != EAGAIN) {
            log_line("V201_GPSD_MONITOR_READ_FAIL errno=%d", errno);
            continue;
        }

        for (;;) {
            int status = 0;
            int recycle_process = 0;
            int tracking = 0;
            int expected_idle_exit = 0;
            uint32_t generation = 0;
            pid_t pid = -1;
            pid_t result;

            pthread_mutex_lock(&g_state_lock);
            pid = g_gpsd_pid;
            if (pid <= 0) {
                pthread_mutex_unlock(&g_state_lock);
                break;
            }

            result = waitpid(pid, &status, WNOHANG);
            if (result == 0) {
                pthread_mutex_unlock(&g_state_lock);
                break;
            }
            if (result < 0) {
                int saved_errno = errno;
                pthread_mutex_unlock(&g_state_lock);
                if (saved_errno != ECHILD)
                    log_line("V201_GPSD_WAITPID_FAIL pid=%ld errno=%d",
                             (long)pid, saved_errno);
                break;
            }

            generation = g_gpsd_generation;
            pthread_mutex_lock(&g_hal_lock);
            tracking = g_tracking;
            expected_idle_exit = g_idle_stop_expected;
            if (expected_idle_exit) g_idle_stop_expected = 0;
            pthread_mutex_unlock(&g_hal_lock);
            log_line("V201_GPSD_EXIT pid=%ld generation=%u status=%d want=%d tracking=%d",
                     (long)pid, generation, status, g_want_gpsd, tracking);
            if (WIFEXITED(status)) {
                log_line("V302_GPSD_EXIT_DECODE pid=%ld exited=1 exit_code=%d raw_status=%d",
                         (long)pid, WEXITSTATUS(status), status);
            } else if (WIFSIGNALED(status)) {
                log_line("V302_GPSD_EXIT_DECODE pid=%ld signaled=1 signal=%d raw_status=%d",
                         (long)pid, WTERMSIG(status), status);
            } else {
                log_line("V302_GPSD_EXIT_DECODE pid=%ld other=1 raw_status=%d",
                         (long)pid, status);
            }
            g_gpsd_pid = -1;
            if (expected_idle_exit)
                log_line("V311_GPSD_IDLE_STOP_EXIT_EXPECTED generation=%u status=%d", generation, status);
            send_gpsd_event(LT013G_GPSD_EVENT_EXIT, generation, status, expected_idle_exit ? 1u : 0u);

            /*
             * Preserve v197 semantics: idle crashes are parked, while a gpsd
             * failure during active tracking retires the entire isolated
             * legacy process because Samsung's blob owns detached threads.
             */
            recycle_process = g_want_gpsd && tracking;
            if (!recycle_process && g_want_gpsd)
                log_line("V201_GPSD_IDLE_EXIT parked generation=%u", generation);
            pthread_mutex_unlock(&g_state_lock);

            if (recycle_process) {
                log_line("V201_LEGACY_PROCESS_RECYCLE generation=%u reason=active_gpsd_exit",
                         generation);
                usleep(50000);
                _exit(90);
            }
            break;
        }
    }
    log_line("V201_GPSD_MONITOR_STOP event_driven=1");
    return NULL;
}

static int load_hal(void) {
    get_gps_interface_fn get_iface;
    int rc;
    struct stat socket_st;
    struct stat hal_st;
    const char *dlerr;
    int saved_errno;
    if (g_iface) return 0;

    if (stat("/dev/socket/gps", &socket_st) == 0) {
        log_line("V305_HAL_SOCKET_STAT=PASS mode=%#o uid=%u gid=%u",
                 (unsigned)socket_st.st_mode, (unsigned)socket_st.st_uid,
                 (unsigned)socket_st.st_gid);
    } else {
        log_line("V305_HAL_SOCKET_STAT=FAIL errno=%d (%s)", errno, strerror(errno));
    }
    if (stat("/system/lib/hw/gps.default.so", &hal_st) == 0) {
        log_line("V305_HAL_FILE_STAT=PASS mode=%#o uid=%u gid=%u size=%lld",
                 (unsigned)hal_st.st_mode, (unsigned)hal_st.st_uid,
                 (unsigned)hal_st.st_gid, (long long)hal_st.st_size);
    } else {
        log_line("V305_HAL_FILE_STAT=FAIL errno=%d (%s)", errno, strerror(errno));
    }

    (void)dlerror();
    errno = 0;
    g_hal_handle = dlopen("/system/lib/hw/gps.default.so", RTLD_NOW | RTLD_GLOBAL);
    saved_errno = errno;
    if (!g_hal_handle) {
        dlerr = dlerror();
        log_line("V305_HAL_DLOPEN_FAIL errno=%d (%s) dlerror=%s",
                 saved_errno, strerror(saved_errno), dlerr ? dlerr : "<null>");
        return -ENOENT;
    }
    log_line("V305_HAL_DLOPEN_PASS handle=%p", g_hal_handle);
    dlerror();
    get_iface = (get_gps_interface_fn)dlsym(g_hal_handle, "gps_proxy_get_gps_interface");
    if (!get_iface) {
        log_line("V195_HAL_DLSYM_FAIL %s", dlerror());
        dlclose(g_hal_handle);
        g_hal_handle = NULL;
        return -EINVAL;
    }
    g_iface = get_iface();
    stage_line("V423_HAL_PROXY_ENTRYPOINT_USED=PASS symbol=gps_proxy_get_gps_interface");
    if (!g_iface || !g_iface->init || !g_iface->start || !g_iface->stop || !g_iface->cleanup) {
        g_iface = NULL;
        dlclose(g_hal_handle);
        g_hal_handle = NULL;
        return -EINVAL;
    }
    memset(&g_legacy_callbacks, 0, sizeof(g_legacy_callbacks));
    g_legacy_callbacks.size = sizeof(g_legacy_callbacks);
    g_legacy_callbacks.location_cb = location_cb;
    g_legacy_callbacks.status_cb = status_cb;
    g_legacy_callbacks.sv_status_cb = sv_status_cb;
    g_legacy_callbacks.nmea_cb = nmea_cb;
    g_legacy_callbacks.set_capabilities_cb = capabilities_cb;
    g_legacy_callbacks.acquire_wakelock_cb = acquire_wakelock_cb;
    g_legacy_callbacks.release_wakelock_cb = release_wakelock_cb;
    g_legacy_callbacks.create_thread_cb = create_thread_cb;
    g_legacy_callbacks.request_utc_time_cb = request_utc_time_cb;
    g_legacy_callbacks.set_system_info_cb = system_info_cb;
    g_legacy_callbacks.gnss_sv_status_cb = gnss_sv_status_cb;
    rc = g_iface->init(&g_legacy_callbacks);
    if (rc != 0) {
        /*
         * init() may already have created detached vendor threads.  Never
         * dlclose the Samsung blob after calling init(), even on failure.
         * Return the error, send the response, then retire this process.
         */
        log_line("V195_HAL_INIT_FAIL rc=%d process_retire=1", rc);
        g_retire_after_response = 1;
        return rc;
    }
    log_line("V195_HAL_INIT_PASS");
    /* v363: keep proven XTRA + AGPS/SUPL and add only the standard
     * legacy AGPS-RIL surface. GPS-NI and vendor sensor fusion remain masked. */
    g_xtra_iface = NULL;
    g_agps_iface = NULL;
    g_agps_ril_iface = NULL;
    g_v356_extension_mask = 0;
    if (g_iface->get_extension) {
        const void *xtra = g_iface->get_extension("gps-xtra");
        const void *agps = g_iface->get_extension("agps");
        const void *agps_ril = g_iface->get_extension("agps_ril");
        const void *gps_sensor = g_iface->get_extension("gps-sensor");
        const void *gps_ni = g_iface->get_extension("gps-ni");
        log_line("V392_EXTENSION_DISCOVER gps_sensor=%d gps_ni=%d",
                 gps_sensor != NULL, gps_ni != NULL);
        g_xtra_iface = (const Api25GpsXtraInterface *)xtra;
        g_agps_iface = (const Api25AGpsInterface *)agps;
        g_agps_ril_iface = (const Api25AGpsRilInterface *)agps_ril;
        if (g_xtra_iface) g_v356_extension_mask |= (1u << 0);
        if (g_agps_iface) g_v356_extension_mask |= (1u << 1);
        if (g_agps_ril_iface) g_v356_extension_mask |= (1u << 2);
        log_line("V363_XTRA_DISCOVER present=%d size=%u inject=%d",
                 g_xtra_iface != NULL,
                 g_xtra_iface ? (unsigned)g_xtra_iface->size : 0u,
                 g_xtra_iface && g_xtra_iface->inject_xtra_data);
        log_line("V363_AGPS_DISCOVER present=%d size=%u set_server=%d open=%d",
                 g_agps_iface != NULL,
                 g_agps_iface ? (unsigned)g_agps_iface->size : 0u,
                 g_agps_iface && g_agps_iface->set_server,
                 g_agps_iface && (g_agps_iface->data_conn_open || g_agps_iface->data_conn_open_with_apn_ip_type));
        log_line("V363_AGPS_RIL_DISCOVER present=%d size=%u init=%d ref=%d setid=%d net=%d avail=%d",
                 g_agps_ril_iface != NULL,
                 g_agps_ril_iface ? (unsigned)g_agps_ril_iface->size : 0u,
                 g_agps_ril_iface && g_agps_ril_iface->init,
                 g_agps_ril_iface && g_agps_ril_iface->set_ref_location,
                 g_agps_ril_iface && g_agps_ril_iface->set_set_id,
                 g_agps_ril_iface && g_agps_ril_iface->update_network_state,
                 g_agps_ril_iface && g_agps_ril_iface->update_network_availability);
    }
    {
        struct lt013g_bridge_packet packet;
        struct lt013g_cb_legacy_extensions payload;
        payload.mask = g_v356_extension_mask;
        packet_init(&packet, LT013G_CB_LEGACY_EXTENSIONS, 0);
        packet.payload_size = sizeof(payload);
        memcpy(packet.payload, &payload, sizeof(payload));
        (void)send_packet(&packet);
    }
    log_line("V363_API25_EXTENSIONS_XTRA_AGPS_RIL mask=0x%08x", g_v356_extension_mask);

    /* Do not wait for the proprietary blob to call set_capabilities_cb.  On
     * this provider that callback is not guaranteed before the framework's
     * first position-mode decision.  Publish a conservative contract now so
     * Android 11 cannot reuse/derive MS_BASED while assistance is masked.
     * If the vendor callback arrives later, capabilities_cb republishes the
     * filtered real value. */
    v357_publish_capabilities(g_v357_have_raw_capabilities ?
                              g_v357_last_raw_capabilities : 0u,
                              "post-extension-mask");
    return 0;
}

static int v379_safe_hostname(const char *hostname) {
    const unsigned char *p = (const unsigned char *)hostname;
    if (!hostname || !hostname[0]) return 0;
    for (; *p; ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '.' || *p == '-') continue;
        return 0;
    }
    return 1;
}

static int v379_apply_dns_seed(const struct lt013g_cmd_dns_seed *payload) {
    static const char base[] = "127.0.0.1       localhost\n::1             ip6-localhost\n";
    char desired[2048];
    char previous[sizeof(desired) + 1u];
    size_t used = sizeof(base) - 1u;
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    uint32_t i;
    int fd;
    int gai;
    int rc;
    int unchanged = 0;

    /* Validate ALL input before ever truncating the legacy chroot hosts file.
     * This also avoids rewriting/fsyncing identical A-GPS DNS seed data on eMMC.
     * A fresh seed with changed address data must still replace stale mappings. */
    if (!payload || payload->address_count == 0 ||
        payload->address_count > LT013G_DNS_MAX_ADDRS ||
        strnlen(payload->hostname, sizeof(payload->hostname)) >= sizeof(payload->hostname) ||
        !v379_safe_hostname(payload->hostname)) return -EINVAL;
    memcpy(desired, base, used);
    for (i = 0; i < payload->address_count; ++i) {
        const char *ip = payload->address[i];
        size_t len = strnlen(ip, LT013G_DNS_ADDR_MAX);
        struct in_addr address;
        int n;
        if (len == 0 || len == LT013G_DNS_ADDR_MAX ||
            inet_pton(AF_INET, ip, &address) != 1) return -EINVAL;
        n = snprintf(desired + used, sizeof(desired) - used, "%s %s\n",
                     ip, payload->hostname);
        if (n < 0 || (size_t)n >= sizeof(desired) - used) return -EINVAL;
        used += (size_t)n;
    }
    fd = open("/system/etc/hosts", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t n;
        do {
            n = read(fd, previous, sizeof(previous));
        } while (n < 0 && errno == EINTR);
        unchanged = n >= 0 && (size_t)n == used &&
                    memcmp(previous, desired, used) == 0;
        close(fd);
    }
    if (!unchanged) {
        fd = open("/system/etc/hosts", O_WRONLY | O_TRUNC | O_CLOEXEC);
        if (fd < 0) return -errno;
        rc = v355_write_full(fd, desired, used);
        if (rc == 0 && fsync(fd) != 0) rc = -errno;
        close(fd);
        if (rc != 0) return rc;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    gai = getaddrinfo(payload->hostname, NULL, &hints, &result);
    if (gai == 0) freeaddrinfo(result);
    log_line("V379_API25_DNS_SEED host=%s addresses=%u resolve_rc=%d pass=%d cache=%s",
             payload->hostname, payload->address_count, gai, gai == 0,
             unchanged ? "UNCHANGED" : "UPDATED");
    if (gai != 0) return -EHOSTUNREACH;
    return 0;
}

static int handle_command(const struct lt013g_bridge_packet *packet) {
    int rc = 0;
    if (!packet || packet->magic != LT013G_BRIDGE_MAGIC ||
        packet->version != LT013G_BRIDGE_VERSION ||
        packet->payload_size > LT013G_BRIDGE_PAYLOAD_MAX) return -EPROTO;
    switch (packet->type) {
        case LT013G_CMD_PING:
            return 0;
        case LT013G_CMD_INIT:
            pthread_mutex_lock(&g_state_lock);
            g_want_gpsd = 1;
            rc = spawn_gpsd_locked();
            pthread_mutex_unlock(&g_state_lock);
            if (rc != 0) return rc;
            usleep(700000);
            pthread_mutex_lock(&g_hal_lock);
            rc = load_hal();
            pthread_mutex_unlock(&g_hal_lock);
            return rc;
        case LT013G_CMD_START:
            pthread_mutex_lock(&g_state_lock);
            g_want_gpsd = 1;
            rc = spawn_gpsd_locked();
            pthread_mutex_unlock(&g_state_lock);
            if (rc != 0) return rc;
            pthread_mutex_lock(&g_hal_lock);
            if (!g_iface) rc = -ENODEV;
            else {
                rc = g_iface->start();
                if (rc == 0) {
                    g_tracking = 1;
                    g_idle_stop_expected = 0;
                    if (g_xtra_pending || g_xtra_buffer || g_xtra_total || g_xtra_received) {
                        log_line("V450_XTRA_PENDING_DROP_AT_START bytes=%u received=%u pending=%d",
                                 g_xtra_total, g_xtra_received, g_xtra_pending);
                        free(g_xtra_buffer);
                        g_xtra_buffer = NULL;
                        g_xtra_total = 0;
                        g_xtra_received = 0;
                        g_xtra_pending = 0;
                    }
                }
            }
            pthread_mutex_unlock(&g_hal_lock);
            return rc;
        case LT013G_CMD_STOP:
            pthread_mutex_lock(&g_hal_lock);
            if (!g_iface) rc = -ENODEV;
            else {
                rc = g_iface->stop();
                if (rc == 0) {
                    g_tracking = 0;
                    g_idle_stop_expected = 1;
                }
            }
            pthread_mutex_unlock(&g_hal_lock);
            return rc;
        case LT013G_CMD_CLEANUP:
            /*
             * The stock HAL owns detached threads.  stop() is safe, but
             * cleanup()+dlclose() can later jump through unloaded code.
             * Stop gpsd and retire the complete isolated process only after
             * the success response has reached the Android 11 proxy.
             */
            pthread_mutex_lock(&g_state_lock);
            g_want_gpsd = 0;
            stop_gpsd_locked();
            pthread_mutex_unlock(&g_state_lock);
            pthread_mutex_lock(&g_hal_lock);
            g_tracking = 0;
            g_idle_stop_expected = 0;
            g_mode_valid = 0;
            free(g_xtra_buffer);
            g_xtra_buffer = NULL;
            g_xtra_total = 0;
            g_xtra_received = 0;
            g_xtra_pending = 0;
            pthread_mutex_unlock(&g_hal_lock);
            g_retire_after_response = 1;
            return 0;
        case LT013G_CMD_INJECT_TIME:
            if (packet->payload_size < sizeof(struct lt013g_cmd_inject_time)) return -EINVAL;
            {
                struct lt013g_cmd_inject_time payload;
                memcpy(&payload, packet->payload, sizeof(payload));
                pthread_mutex_lock(&g_hal_lock);
                rc = (!g_iface || !g_iface->inject_time) ? -ENOSYS :
                     g_iface->inject_time(payload.time_ms, payload.reference_ms,
                                          payload.uncertainty_ms);
                log_line("V359_TIME_INJECT time_ms=%lld reference_ms=%lld uncertainty_ms=%d rc=%d",
                         (long long)payload.time_ms, (long long)payload.reference_ms,
                         payload.uncertainty_ms, rc);
                pthread_mutex_unlock(&g_hal_lock);
                return rc;
            }
        case LT013G_CMD_INJECT_LOCATION:
            if (packet->payload_size < sizeof(struct lt013g_cmd_inject_location)) return -EINVAL;
            {
                struct lt013g_cmd_inject_location payload;
                memcpy(&payload, packet->payload, sizeof(payload));
                pthread_mutex_lock(&g_hal_lock);
                rc = (!g_iface || !g_iface->inject_location) ? -ENOSYS :
                     g_iface->inject_location(payload.latitude, payload.longitude,
                                              payload.accuracy_m);
                pthread_mutex_unlock(&g_hal_lock);
                return rc;
            }
        case LT013G_CMD_DELETE_AIDING:
            if (packet->payload_size < sizeof(struct lt013g_cmd_delete_aiding)) return -EINVAL;
            {
                struct lt013g_cmd_delete_aiding payload;
                memcpy(&payload, packet->payload, sizeof(payload));
                pthread_mutex_lock(&g_hal_lock);
                if (!g_iface || !g_iface->delete_aiding_data) rc = -ENOSYS;
                else {
                    g_iface->delete_aiding_data(payload.flags);
                    rc = 0;
                }
                pthread_mutex_unlock(&g_hal_lock);
                log_line("V392_DELETE_AIDING flags=0x%04x rc=%d",
                         (unsigned)payload.flags, rc);
                return rc;
            }
        case LT013G_CMD_SET_POSITION_MODE:
            if (packet->payload_size < sizeof(struct lt013g_cmd_position_mode)) return -EINVAL;
            {
                struct lt013g_cmd_position_mode payload;
                memcpy(&payload, packet->payload, sizeof(payload));
                log_line("V353_POSITION_MODE_LEGACY mode=%u recurrence=%u min_interval_ms=%u preferred_accuracy_m=%u preferred_time_ms=%u",
                         (unsigned)payload.mode, (unsigned)payload.recurrence,
                         (unsigned)payload.min_interval_ms, (unsigned)payload.preferred_accuracy_m,
                         (unsigned)payload.preferred_time_ms);
                pthread_mutex_lock(&g_hal_lock);
                rc = v355_sync_secgps_mode(payload.mode);
                if (rc != 0) {
                    log_line("V355_POSITION_MODE_BLOCKED_BEFORE_HAL mode=%u rc=%d",
                             (unsigned)payload.mode, rc);
                } else if (!g_iface || !g_iface->set_position_mode) rc = -ENOSYS;
                else {
                    rc = g_iface->set_position_mode(payload.mode, payload.recurrence,
                                                    payload.min_interval_ms,
                                                    payload.preferred_accuracy_m,
                                                    payload.preferred_time_ms);
                    if (rc == 0) {
                        g_mode = payload.mode;
                        g_recurrence = payload.recurrence;
                        g_min_interval = payload.min_interval_ms;
                        g_preferred_accuracy = payload.preferred_accuracy_m;
                        g_preferred_time = payload.preferred_time_ms;
                        g_mode_valid = 1;
                    }
                }
                pthread_mutex_unlock(&g_hal_lock);
                return rc;
            }
        case LT013G_CMD_XTRA_INIT:
            pthread_mutex_lock(&g_hal_lock);
            if (!g_xtra_iface || !g_xtra_iface->init) rc = -ENOSYS;
            else {
                memset(&g_v331_xtra_callbacks, 0, sizeof(g_v331_xtra_callbacks));
                g_v331_xtra_callbacks.download_request_cb = v331_xtra_download_request_cb;
                g_v331_xtra_callbacks.create_thread_cb = create_thread_cb;
                rc = g_xtra_iface->init(&g_v331_xtra_callbacks);
                log_line("V341_XTRA_INIT rc=%d size=%u", rc, (unsigned)g_xtra_iface->size);
            }
            pthread_mutex_unlock(&g_hal_lock);
            return rc;
        case LT013G_CMD_XTRA_BEGIN:
            if (packet->payload_size < sizeof(struct lt013g_cmd_xtra_begin)) return -EINVAL;
            {
                struct lt013g_cmd_xtra_begin payload;
                memcpy(&payload, packet->payload, sizeof(payload));
                if (payload.total_length == 0 || payload.total_length > LT013G_XTRA_TOTAL_MAX) return -EMSGSIZE;
                free(g_xtra_buffer);
                g_xtra_buffer = (uint8_t *)malloc(payload.total_length);
                if (!g_xtra_buffer) { g_xtra_total = 0; g_xtra_received = 0; g_xtra_pending = 0; return -ENOMEM; }
                g_xtra_total = payload.total_length;
                g_xtra_received = 0;
                g_xtra_pending = 0;
                log_line("V341_XTRA_BEGIN bytes=%u", payload.total_length);
                return 0;
            }
        case LT013G_CMD_XTRA_CHUNK:
            if (packet->payload_size < offsetof(struct lt013g_cmd_xtra_chunk, data)) return -EINVAL;
            {
                struct lt013g_cmd_xtra_chunk payload;
                size_t header = offsetof(struct lt013g_cmd_xtra_chunk, data);
                memset(&payload, 0, sizeof(payload));
                memcpy(&payload, packet->payload, packet->payload_size);
                if (!g_xtra_buffer || payload.length > LT013G_XTRA_CHUNK_DATA_MAX ||
                    packet->payload_size < header + payload.length ||
                    payload.offset != g_xtra_received ||
                    payload.offset + payload.length > g_xtra_total) return -EINVAL;
                memcpy(g_xtra_buffer + payload.offset, payload.data, payload.length);
                g_xtra_received += payload.length;
                return 0;
            }
        case LT013G_CMD_XTRA_COMMIT:
            pthread_mutex_lock(&g_hal_lock);
            if (!g_xtra_buffer || g_xtra_total == 0 || g_xtra_received != g_xtra_total) {
                rc = -EINVAL;
            } else if (!lt013g_v357_lto_envelope_valid(
                               g_xtra_buffer, g_xtra_total)) {
                log_line("G357_LTO_REJECT_INNER invalid_envelope length=%u",
                         g_xtra_total);
                rc = -EINVAL;
            } else if (!g_xtra_iface || !g_xtra_iface->inject_xtra_data ||
                       !g_tracking) {
                log_line("G357_LTO_REJECT_INNER no_live_native_receiver");
                rc = -ENODEV;
            } else {
                rc = g_xtra_iface->inject_xtra_data(
                        (char *)g_xtra_buffer, (int)g_xtra_total);
                log_line("G357_LTO_OEM_INJECT_ATTEMPT bytes=%u rc=%d", g_xtra_total, rc);
                free(g_xtra_buffer);
                g_xtra_buffer = NULL;
                g_xtra_total = 0;
                g_xtra_received = 0;
                g_xtra_pending = 0;
            }
            pthread_mutex_unlock(&g_hal_lock);
            if (rc != 0) {
                free(g_xtra_buffer);
                g_xtra_buffer = NULL;
                g_xtra_total = 0;
                g_xtra_received = 0;
                g_xtra_pending = 0;
            }
            return rc;
        case LT013G_CMD_AGPS_INIT:
            pthread_mutex_lock(&g_hal_lock);
            if (!g_agps_iface || !g_agps_iface->init) rc = -ENOSYS;
            else {
                memset(&g_v331_agps_callbacks, 0, sizeof(g_v331_agps_callbacks));
                g_v331_agps_callbacks.status_cb = v331_agps_status_cb;
                g_v331_agps_callbacks.create_thread_cb = create_thread_cb;
                g_agps_iface->init(&g_v331_agps_callbacks);
                log_line("V341_AGPS_INIT rc=0 size=%u", (unsigned)g_agps_iface->size);
                rc = 0;
            }
            pthread_mutex_unlock(&g_hal_lock);
            return rc;
        case LT013G_CMD_AGPS_DATA_CONN_OPEN:
            if (packet->payload_size < sizeof(struct lt013g_cmd_agps_data_conn_open)) return -EINVAL;
            {
                struct lt013g_cmd_agps_data_conn_open payload;
                memcpy(&payload, packet->payload, sizeof(payload));
                payload.apn[LT013G_AGPS_STRING_MAX - 1] = '\0';
                pthread_mutex_lock(&g_hal_lock);
                if (!g_agps_iface) rc = -ENODEV;
                else if (g_agps_iface->data_conn_open_with_apn_ip_type)
                    rc = g_agps_iface->data_conn_open_with_apn_ip_type(payload.apn, payload.apn_ip_type);
                else if (g_agps_iface->data_conn_open) rc = g_agps_iface->data_conn_open(payload.apn);
                else rc = -ENOSYS;
                pthread_mutex_unlock(&g_hal_lock);
                log_line("V341_AGPS_DATA_CONN_OPEN apn=%s iptype=%u rc=%d", payload.apn, payload.apn_ip_type, rc);
                return rc;
            }
        case LT013G_CMD_AGPS_DATA_CONN_CLOSED:
            pthread_mutex_lock(&g_hal_lock);
            rc = (!g_agps_iface || !g_agps_iface->data_conn_closed) ? -ENOSYS : g_agps_iface->data_conn_closed();
            pthread_mutex_unlock(&g_hal_lock);
            log_line("V341_AGPS_DATA_CONN_CLOSED rc=%d", rc);
            return rc;
        case LT013G_CMD_AGPS_DATA_CONN_FAILED:
            pthread_mutex_lock(&g_hal_lock);
            rc = (!g_agps_iface || !g_agps_iface->data_conn_failed) ? -ENOSYS : g_agps_iface->data_conn_failed();
            pthread_mutex_unlock(&g_hal_lock);
            log_line("V341_AGPS_DATA_CONN_FAILED rc=%d", rc);
            return rc;
        case LT013G_CMD_AGPS_SET_SERVER:
            if (packet->payload_size < sizeof(struct lt013g_cmd_agps_set_server)) return -EINVAL;
            {
                struct lt013g_cmd_agps_set_server payload;
                memcpy(&payload, packet->payload, sizeof(payload));
                payload.hostname[LT013G_AGPS_STRING_MAX - 1] = '\0';
                pthread_mutex_lock(&g_hal_lock);
                rc = (!g_agps_iface || !g_agps_iface->set_server) ? -ENOSYS :
                    g_agps_iface->set_server(payload.type, payload.hostname, payload.port);
                pthread_mutex_unlock(&g_hal_lock);
                log_line("V363_AGPS_SET_SERVER_REAL type=%u host=%s port=%d rc=%d", payload.type, payload.hostname, payload.port, rc);
                log_line("V341_AGPS_SET_SERVER type=%u host=%s port=%d rc=%d", payload.type, payload.hostname, payload.port, rc);
                return rc;
            }
        case LT013G_CMD_AGPS_RIL_INIT:
            pthread_mutex_lock(&g_hal_lock);
            if (!g_agps_ril_iface || !g_agps_ril_iface->init) rc = -ENOSYS;
            else {
                memset(&g_v331_agps_ril_callbacks, 0, sizeof(g_v331_agps_ril_callbacks));
                g_v331_agps_ril_callbacks.request_setid = v331_agps_ril_request_setid_cb;
                g_v331_agps_ril_callbacks.request_refloc = v331_agps_ril_request_refloc_cb;
                g_v331_agps_ril_callbacks.create_thread_cb = create_thread_cb;
                g_agps_ril_iface->init(&g_v331_agps_ril_callbacks);
                log_line("V341_AGPS_RIL_INIT rc=0 size=%u", (unsigned)g_agps_ril_iface->size);
                rc = 0;
            }
            pthread_mutex_unlock(&g_hal_lock);
            return rc;
        case LT013G_CMD_AGPS_RIL_SET_REF_LOCATION:
            if (packet->payload_size < sizeof(struct lt013g_cmd_agps_ril_ref_location)) return -EINVAL;
            {
                struct lt013g_cmd_agps_ril_ref_location payload;
                Api25AGpsRefLocation loc;
                memcpy(&payload, packet->payload, sizeof(payload));
                memset(&loc, 0, sizeof(loc));
                loc.type = payload.type;
                if (payload.type == 3) memcpy(loc.u.mac.mac, payload.mac, sizeof(loc.u.mac.mac));
                else {
                    loc.u.cellID.type = payload.type; loc.u.cellID.mcc = payload.mcc;
                    loc.u.cellID.mnc = payload.mnc; loc.u.cellID.lac = payload.lac;
                    loc.u.cellID.foo = 0;
                    loc.u.cellID.cid = payload.cid;
                }
                pthread_mutex_lock(&g_hal_lock);
                if (!g_agps_ril_iface || !g_agps_ril_iface->set_ref_location) rc = -ENOSYS;
                else { g_agps_ril_iface->set_ref_location(&loc, 16u); rc = 0; }
                pthread_mutex_unlock(&g_hal_lock);
                if (payload.type == 3) {
                    log_line("V445_REFLOC_SAMSUNG_MAGIC outer_type=%u mac=1 union_off=%u struct_size=%u arg_size=16 rc=%d",
                             (unsigned)payload.type, (unsigned)offsetof(Api25AGpsRefLocation, u),
                             (unsigned)sizeof(loc), rc);
                } else {
                    log_line("V445_REFLOC_SAMSUNG_MAGIC outer_type=%u cell_type=%u mcc=%u mnc=%u lac=%u foo=%u cid=%u union_off=%u cid_off=%u struct_size=%u arg_size=16 rc=%d",
                             (unsigned)payload.type, (unsigned)loc.u.cellID.type,
                             (unsigned)loc.u.cellID.mcc, (unsigned)loc.u.cellID.mnc,
                             (unsigned)loc.u.cellID.lac, (unsigned)loc.u.cellID.foo,
                             (unsigned)loc.u.cellID.cid,
                             (unsigned)offsetof(Api25AGpsRefLocation, u),
                             (unsigned)offsetof(Api25AGpsRefLocation, u.cellID.cid),
                             (unsigned)sizeof(loc), rc);
                }
                log_line("V371_AGPS_RIL_SET_REF_LOCATION_VENDOR16 type=%u size=16 psc=0 rc=%d",
                         payload.type, rc);
                log_line("V341_AGPS_RIL_SET_REF_LOCATION type=%u rc=%d", payload.type, rc);
                return rc;
            }
        case LT013G_CMD_AGPS_RIL_SET_SET_ID:
            if (packet->payload_size < sizeof(struct lt013g_cmd_agps_ril_set_id)) return -EINVAL;
            {
                struct lt013g_cmd_agps_ril_set_id payload;
                memcpy(&payload, packet->payload, sizeof(payload));
                payload.setid[LT013G_AGPS_STRING_MAX - 1] = '\0';
                pthread_mutex_lock(&g_hal_lock);
                if (!g_agps_ril_iface || !g_agps_ril_iface->set_set_id) rc = -ENOSYS;
                else { g_agps_ril_iface->set_set_id(payload.type, payload.setid); rc = 0; }
                pthread_mutex_unlock(&g_hal_lock);
                log_line("V419_SETID type=%u len=%u rc=%d",
                         (unsigned)payload.type, (unsigned)strlen(payload.setid), rc);
                log_line("V341_AGPS_RIL_SET_SET_ID type=%u rc=%d", payload.type, rc);
                return rc;
            }
        case LT013G_CMD_AGPS_RIL_UPDATE_NETWORK_STATE:
            if (packet->payload_size < sizeof(struct lt013g_cmd_agps_ril_network_state)) return -EINVAL;
            {
                struct lt013g_cmd_agps_ril_network_state payload; memcpy(&payload, packet->payload, sizeof(payload));
                pthread_mutex_lock(&g_hal_lock);
                if (!g_agps_ril_iface || !g_agps_ril_iface->update_network_state) rc = -ENOSYS;
                else { g_agps_ril_iface->update_network_state(payload.connected, payload.type, payload.roaming, NULL); rc = 0; }
                pthread_mutex_unlock(&g_hal_lock);
                log_line("V341_AGPS_RIL_NETWORK_STATE connected=%d type=%d roaming=%d rc=%d", payload.connected, payload.type, payload.roaming, rc);
                return rc;
            }
        case LT013G_CMD_AGPS_RIL_UPDATE_NETWORK_AVAILABILITY:
            if (packet->payload_size < sizeof(struct lt013g_cmd_agps_ril_network_availability)) return -EINVAL;
            {
                struct lt013g_cmd_agps_ril_network_availability payload; memcpy(&payload, packet->payload, sizeof(payload));
                payload.apn[LT013G_AGPS_STRING_MAX - 1] = '\0';
                pthread_mutex_lock(&g_hal_lock);
                if (!g_agps_ril_iface || !g_agps_ril_iface->update_network_availability) rc = -ENOSYS;
                else { g_agps_ril_iface->update_network_availability(payload.available, payload.apn); rc = 0; }
                pthread_mutex_unlock(&g_hal_lock);
                log_line("V341_AGPS_RIL_NETWORK_AVAILABILITY available=%d apn=%s rc=%d", payload.available, payload.apn, rc);
                return rc;
            }
        case LT013G_CMD_DNS_SEED:
            if (packet->payload_size < sizeof(struct lt013g_cmd_dns_seed)) return -EINVAL;
            {
                struct lt013g_cmd_dns_seed payload;
                memcpy(&payload, packet->payload, sizeof(payload));
                payload.hostname[LT013G_AGPS_STRING_MAX - 1] = '\0';
                for (uint32_t i = 0; i < LT013G_DNS_MAX_ADDRS; ++i)
                    payload.address[i][LT013G_DNS_ADDR_MAX - 1] = '\0';
                return v379_apply_dns_seed(&payload);
            }
        default:
            return -EINVAL;
    }
}

static int make_server_socket(void) {
    struct sockaddr_un address;
    socklen_t length;
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return -errno;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
        int error = -errno;
        close(fd);
        return error;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    address.sun_path[0] = '\0';
    memcpy(address.sun_path + 1, LT013G_BRIDGE_SOCKET, sizeof(LT013G_BRIDGE_SOCKET) - 1u);
    length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1u + sizeof(LT013G_BRIDGE_SOCKET) - 1u);
    if (bind(fd, (struct sockaddr *)&address, length) != 0) {
        int error = -errno;
        close(fd);
        return error;
    }
    if (listen(fd, 1) != 0) {
        int error = -errno;
        close(fd);
        return error;
    }
    return fd;
}

static void wake_monitor_from_signal(void) {
    unsigned char byte = 1;
    int saved_errno = errno;
    if (g_signal_pipe[1] >= 0)
        (void)write(g_signal_pipe[1], &byte, sizeof(byte));
    errno = saved_errno;
}

static void termination_signal_handler(int signal_number) {
    (void)signal_number;
    g_stop_server = 1;
    wake_monitor_from_signal();
}

static void sigchld_handler(int signal_number) {
    (void)signal_number;
    wake_monitor_from_signal();
}

static int setup_signal_pipe(void) {
    int flags;
    int saved_errno;

    if (pipe(g_signal_pipe) != 0) return -errno;

    /*
     * HOTFIX1: the monitor side MUST remain blocking.  Making the read end
     * O_NONBLOCK turns the supposedly event-driven loop into a permanent
     * EAGAIN busy-spin while gpsd is alive/idle.  Only the signal-handler
     * write end is nonblocking so write() can never stall in signal context.
     */
    flags = fcntl(g_signal_pipe[1], F_GETFL, 0);
    if (flags < 0 || fcntl(g_signal_pipe[1], F_SETFL, flags | O_NONBLOCK) != 0)
        goto fail;
    if (fcntl(g_signal_pipe[0], F_SETFD, FD_CLOEXEC) != 0) goto fail;
    if (fcntl(g_signal_pipe[1], F_SETFD, FD_CLOEXEC) != 0) goto fail;
    return 0;

fail:
    saved_errno = errno;
    close(g_signal_pipe[0]);
    close(g_signal_pipe[1]);
    g_signal_pipe[0] = -1;
    g_signal_pipe[1] = -1;
    return -saved_errno;
}

static int lt013g_bridge_main(void) {
    (void)prctl(PR_SET_NAME, (unsigned long)"lt013g-gps-v290", 0, 0, 0);
    log_line("V290_PRELOAD_BRIDGE_MAIN pid=%ld", (long)getpid());
    stage_line("V290_STAGE=bridge-main-enter");
    int server_fd;
    int setup_rc;
    pthread_t monitor;
    struct sigaction action;
    struct rlimit limit;

    setup_rc = setup_signal_pipe();
    if (setup_rc != 0) {
        log_line("V201_SIGNAL_PIPE_FAIL rc=%d", setup_rc);
        return 39;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = termination_signal_handler;
    (void)sigaction(SIGTERM, &action, NULL);
    (void)sigaction(SIGINT, &action, NULL);

    memset(&action, 0, sizeof(action));
    action.sa_handler = sigchld_handler;
    action.sa_flags = SA_NOCLDSTOP;
    (void)sigaction(SIGCHLD, &action, NULL);

    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_IGN;
    (void)sigaction(SIGPIPE, &action, NULL);

    log_line("V202_KITKAT_ABI signal_mask=ZERO_INIT_NO_SIGEMPTYSET");

    log_line("V290_API25_GNSS_ABI gps_sv_info=%zu gps_sv_status=%zu gnss_sv_info=%zu gnss_sv_status=%zu callbacks=%zu",
             sizeof(GpsSvInfo), sizeof(GpsSvStatus), sizeof(GnssSvInfo),
             sizeof(GnssSvStatus), sizeof(GpsCallbacks));
    log_line("V201_GPSD_MONITOR event_driven=SIGCHLD_SELF_PIPE blocking_read=1 idle_poll_ms=0");
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur > 3) {
        rlim_t capped = limit.rlim_cur > 32768 ? 32768 : limit.rlim_cur;
        g_close_fd_limit = (int)capped;
    }
    server_fd = make_server_socket();
    if (server_fd < 0) {
        log_line("V195_BRIDGE_BIND_FAIL rc=%d", server_fd);
        return 40;
    }
    if (pthread_create(&monitor, NULL, gpsd_monitor, NULL) != 0) {
        close(server_fd);
        return 41;
    }
    log_line("V195_LEGACY_SERVER_READY socket=%s", LT013G_BRIDGE_SOCKET);
    stage_line("V290_STAGE=server-ready");
    while (!g_stop_server) {
        int client = accept(server_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fcntl(client, F_SETFD, FD_CLOEXEC) != 0) {
            close(client);
            continue;
        }
        {
            struct ucred credential;
            socklen_t credential_size = sizeof(credential);
            memset(&credential, 0, sizeof(credential));
            if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &credential,
                           &credential_size) != 0 ||
                (credential.uid != 1021 && credential.uid != 0)) {
                log_line("V195_BRIDGE_CLIENT_REJECT uid=%lu",
                         (unsigned long)credential.uid);
                close(client);
                continue;
            }
            log_line("V195_BRIDGE_CLIENT_PEER pid=%ld uid=%lu gid=%lu",
                     (long)credential.pid, (unsigned long)credential.uid,
                     (unsigned long)credential.gid);
        }
        pthread_mutex_lock(&g_send_lock);
        g_client_fd = client;
        pthread_mutex_unlock(&g_send_lock);
        log_line("V195_BRIDGE_CLIENT_CONNECTED");
        while (!g_stop_server) {
            struct lt013g_bridge_packet packet;
            ssize_t received;
            memset(&packet, 0, sizeof(packet));
            received = recv(client, &packet, sizeof(packet), MSG_TRUNC);
            int result;
            if (received == 0) {
                log_line("V307_SERVER_CLIENT_RECV_EXIT reason=peer-eof fd=%d", client);
                break;
            }
            if (received < 0) {
                if (errno == EINTR) continue;
                log_line("V307_SERVER_CLIENT_RECV_EXIT reason=recv-error fd=%d errno=%d (%s)",
                         client, errno, strerror(errno));
                break;
            }
            if (received != (ssize_t)sizeof(packet)) {
                send_response(packet.sequence, -EMSGSIZE);
                continue;
            }
            result = handle_command(&packet);
            send_response(packet.sequence, result);
            if (g_retire_after_response) {
                log_line("V195_LEGACY_PROCESS_RETIRE reason=framework_cleanup rc=%d", result);
                usleep(10000);
                _exit(result == 0 ? 0 : 91);
            }
        }
        pthread_mutex_lock(&g_send_lock);
        g_client_fd = -1;
        pthread_mutex_unlock(&g_send_lock);
        close(client);
        pthread_mutex_lock(&g_state_lock);
        g_want_gpsd = 0;
        stop_gpsd_locked();
        pthread_mutex_unlock(&g_state_lock);
        log_line("V195_LEGACY_PROCESS_RETIRE reason=client_disconnect");
        _exit(0);
    }
    g_stop_server = 1;
    pthread_join(monitor, NULL);
    pthread_mutex_lock(&g_state_lock);
    stop_gpsd_locked();
    pthread_mutex_unlock(&g_state_lock);
    close(server_fd);
    if (g_signal_pipe[0] >= 0) close(g_signal_pipe[0]);
    if (g_signal_pipe[1] >= 0) close(g_signal_pipe[1]);
    log_line("V201_LEGACY_PROCESS_RETIRE reason=server_shutdown");
    _exit(0);
}


#ifdef LT013G_PRELOAD_BRIDGE
__attribute__((constructor))
static void lt013g_preload_bridge_entry(void) {
    int rc;
    const char *probe_only;
    {
        int gps_fd = v319_find_gpsd_control_fd();
        if (gps_fd >= 0) {
            char marker[160];
            int n;
            g_v319_gpsd_capture_process = 1;
            g_v319_gpsd_control_fd = gps_fd;
            n = snprintf(marker, sizeof(marker),
                         "V319_GPSD_OEM_CAPTURE_SHIM=ACTIVE control_fd=%d discriminator=SEQPACKET:/dev/socket/gps\n",
                         gps_fd);
            if (n > 0) {
                size_t len = (size_t)n < sizeof(marker) ? (size_t)n : sizeof(marker) - 1;
                (void)write(STDERR_FILENO, marker, len);
            }
            return;
        }
    }
    stage_line("V295_STAGE=constructor-enter");
    log_line("V295_PRELOAD_ENTRY host=api25-gpsd pid=%ld", (long)getpid());

    /*
     * Loader-only qualification: the exact API25 gpsd executable is only a
     * proven host for the old linker/libc dependency graph.  Exit directly
     * from the constructor so gpsd main() never runs during the probe.
     */
    probe_only = getenv("LT013G_BRIDGE_PROBE_ONLY");
    if (probe_only && strcmp(probe_only, "1") == 0) {
        const char *probe_fd_text = getenv("LT013G_BRIDGE_PROBE_FD");
        if (probe_fd_text && *probe_fd_text) {
            char *end = NULL;
            long probe_fd = strtol(probe_fd_text, &end, 10);
            if (end && *end == '\0' && probe_fd >= 0 && probe_fd <= 32767) {
                (void)write((int)probe_fd, "P", 1);
            }
        }
        stage_line("V295_STAGE=constructor-probe-pass");
        log_line("V299_PRELOAD_PROBE_CONSTRUCTOR=PASS host=api25-gpsd pid=%ld", (long)getpid());
        _exit(0);
    }

    rc = lt013g_bridge_main();
    _exit(rc);
}
#endif

#endif /* LT013G_GPSD_CRASHTRACE */
