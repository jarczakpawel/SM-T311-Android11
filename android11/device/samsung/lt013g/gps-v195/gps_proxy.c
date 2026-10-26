#define LOG_TAG "Lt013gGpsProxyV290"
#include "bridge_protocol.h"
#include "legacy_lto_v2_guard.h"

#include <hardware/gps.h>
#include <hardware/hardware.h>
#include <log/log.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/system_properties.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* Android 11 removed the legacy MAC ref-location enum constant from
 * gnss-base.h, while AGpsRefLocation still keeps the legacy mac union member.
 * Samsung/API25 used numeric value 3 for MAC. Keep that ABI value local to
 * this compatibility proxy instead of depending on a removed AOSP symbol. */
#define LT013G_AGPS_REFLOC_MAC 3u

static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_command_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_response_cond;
static pthread_cond_t g_supervisor_cond;
static pthread_once_t g_cond_once = PTHREAD_ONCE_INIT;
static int g_cond_error;
static GpsCallbacks g_callbacks_storage;
static int g_callbacks_valid;
static struct lt013g_bridge_stats g_stats;
static int g_socket = -1;
static int g_rx_running;
static uint32_t g_next_sequence = 1;
static uint32_t g_waiting_sequence;
static int g_waiting;
static int g_waiting_result;

/* Desired framework state, replayed after a complete legacy-process recycle. */
static int g_session_initialized;
static int g_session_started;
static int g_mode_valid;
static struct lt013g_cmd_position_mode g_mode_payload;
static int g_reconnect_needed;
static int g_supervisor_started;
static int g_supervisor_running;
static int g_supervisor_stop;
static uint32_t g_effective_generation = 1;
static GpsXtraCallbacks g_xtra_callbacks_storage;
static int g_xtra_callbacks_valid;
static int g_xtra_initialized;
static uint8_t *g_xtra_cache;
static uint32_t g_xtra_cache_length;
static AGpsCallbacks g_agps_callbacks_storage;
static int g_agps_callbacks_valid;
static int g_agps_initialized;
static AGpsRilCallbacks g_agps_ril_callbacks_storage;
static int g_agps_ril_callbacks_valid;
static int g_agps_ril_initialized;
static struct lt013g_cmd_agps_set_server g_agps_server_cache;
static int g_agps_server_valid;
static int g_agps_server_pending;
static struct lt013g_cmd_agps_ril_network_state g_agps_network_state_cache;
static int g_agps_network_state_valid;
static struct lt013g_cmd_agps_ril_network_availability g_agps_network_avail_cache;
static int g_agps_network_avail_valid;
static int g_agps_network_avail_synthetic;
static uint32_t g_v379_dns_generation;
static uint32_t g_v379_dns_seeded_generation;
static uint32_t g_v379_dns_inflight_generation;
/* V397: Android 11 HIDL requires a valid sockaddr family in AGpsStatus.
 * Old BCM4752 may report only legacy ipaddr or no address at all.
 * Cache the already resolved SUPL IPv4 so AGPS REQUEST/RELEASE can be
 * represented as a valid modern AF_INET status without inventing 0.0.0.0. */
static uint32_t g_v397_supl_ipv4_net;
static int g_v397_supl_ipv4_valid;

/* V398: API25 Broadcom requests a SUPL "data connection", but Android 11
 * hard-wires pre-2.0 SUPL requests to TRANSPORT_CELLULAR.  That makes AGPS
 * fail whenever user mobile data is disabled even if a validated default
 * network (typically Wi-Fi) can reach the SUPL server.  Use a short,
 * non-blocking reachability probe to the already-resolved SUPL endpoint.
 * If the default route works, acknowledge the legacy data connection
 * locally; otherwise fall back to the standard Android cellular SUPL path. */
static uint32_t g_v398_agps_generation;
/* Serialize deferred local OPEN/CLOSED commands across SUPL generations. */
static pthread_mutex_t g_v398_link_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_v398_local_data_open;
/* A released local SUPL session still needs a CP CLOSED ACK before reuse. */
static int g_v398_pending_close;
static int g_v398_framework_request_active;
static int g_v398_framework_data_open;

static int send_command(uint32_t type, const void *payload, uint32_t payload_size);

static int g_v392_delete_all_done;

static int v392_property_enabled(const char *name) {
    char value[PROP_VALUE_MAX];
    int n;

    if (!name) return 0;
    memset(value, 0, sizeof(value));
    n = __system_property_get(name, value);
    if (n <= 0) return 0;

    return strcmp(value, "1") == 0 ||
           strcmp(value, "true") == 0 ||
           strcmp(value, "on") == 0;
}

static int v392_delete_all_if_requested(void) {
    struct lt013g_cmd_delete_aiding payload;
    int rc;

    if (g_v392_delete_all_done ||
        !v392_property_enabled("debug.lt013g.gps.v392.cold_reset"))
        return 0;

    memset(&payload, 0, sizeof(payload));
    payload.flags = 0xffffu;

    rc = send_command(LT013G_CMD_DELETE_AIDING, &payload, sizeof(payload));
    ALOGI("V392_DELETE_AIDING_ALL flags=0x%04x rc=%d",
          (unsigned)payload.flags, rc);

    if (rc == 0)
        g_v392_delete_all_done = 1;

    return rc;
}

_Static_assert(sizeof(struct lt013g_bridge_packet) == 2072, "bridge packet ABI");
_Static_assert(sizeof(struct lt013g_cb_nmea) <= LT013G_BRIDGE_PAYLOAD_MAX, "NMEA payload ABI");
_Static_assert(sizeof(struct lt013g_cb_sv) <= LT013G_BRIDGE_PAYLOAD_MAX, "SV payload ABI");
_Static_assert(sizeof(struct lt013g_cb_gnss_sv) <= LT013G_BRIDGE_PAYLOAD_MAX, "GNSS SV payload ABI");

static void init_conditions(void) {
    pthread_condattr_t attr;
    g_cond_error = pthread_condattr_init(&attr);
    if (g_cond_error != 0) return;
    g_cond_error = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (g_cond_error == 0) {
        g_cond_error = pthread_cond_init(&g_response_cond, &attr);
        if (g_cond_error == 0) {
            g_cond_error = pthread_cond_init(&g_supervisor_cond, &attr);
            if (g_cond_error != 0) pthread_cond_destroy(&g_response_cond);
        }
    }
    pthread_condattr_destroy(&attr);
}

static void packet_init(struct lt013g_bridge_packet *packet, uint32_t type, uint32_t sequence) {
    memset(packet, 0, sizeof(*packet));
    packet->magic = LT013G_BRIDGE_MAGIC;
    packet->version = LT013G_BRIDGE_VERSION;
    packet->type = type;
    packet->sequence = sequence;
}

static int connect_once(void) {
    struct sockaddr_un address;
    socklen_t length;
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -errno;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    address.sun_path[0] = '\0';
    memcpy(address.sun_path + 1, LT013G_BRIDGE_SOCKET, sizeof(LT013G_BRIDGE_SOCKET) - 1u);
    length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1u + sizeof(LT013G_BRIDGE_SOCKET) - 1u);
    if (connect(fd, (struct sockaddr *)&address, length) != 0) {
        int error = -errno;
        close(fd);
        return error;
    }
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        int error = -errno;
        close(fd);
        return error;
    }
    return fd;
}

struct lt013g_agps_ril_deferred {
    uint32_t type;
    uint32_t flags;
    agps_ril_request_set_id setid_cb;
    agps_ril_request_ref_loc refloc_cb;
};

static void agps_ril_deferred_worker(void *arg) {
    struct lt013g_agps_ril_deferred *work = (struct lt013g_agps_ril_deferred *)arg;
    if (!work) return;
    ALOGI("V344_AGPS_RIL_DEFER_BEGIN type=%u flags=0x%x", work->type, work->flags);
    /* V425_AGPS_RIL_ORDER_RACEFIX_BEGIN */
    if ((unsigned)(work->type) == 114u) {
        usleep(120000);
        ALOGI("V425_AGPS_RIL_ORDER type=%u delay_us=120000 role=SETID", (unsigned)(work->type));
    } else if ((unsigned)(work->type) == 115u) {
        usleep(20000);
        ALOGI("V425_AGPS_RIL_ORDER type=%u delay_us=20000 role=REFLOC", (unsigned)(work->type));
    }
    /* V425_AGPS_RIL_ORDER_RACEFIX_END */
    if (work->type == LT013G_CB_AGPS_RIL_REQUEST_SETID) {
        if (work->setid_cb) work->setid_cb(work->flags);
    } else if (work->type == LT013G_CB_AGPS_RIL_REQUEST_REFLOC) {
        if (work->refloc_cb) work->refloc_cb(work->flags);
    }
    ALOGI("V344_AGPS_RIL_DEFER_END type=%u flags=0x%x", work->type, work->flags);
    free(work);
}

static int defer_agps_ril_callback(uint32_t type, uint32_t flags,
        agps_ril_request_set_id setid_cb, agps_ril_request_ref_loc refloc_cb,
        gps_create_thread create_thread_cb) {
    struct lt013g_agps_ril_deferred *work;
    pthread_t thread;
    if (!create_thread_cb) return -ENOSYS;
    work = (struct lt013g_agps_ril_deferred *)calloc(1, sizeof(*work));
    if (!work) return -ENOMEM;
    work->type = type;
    work->flags = flags;
    work->setid_cb = setid_cb;
    work->refloc_cb = refloc_cb;
    thread = create_thread_cb(type == LT013G_CB_AGPS_RIL_REQUEST_SETID ?
            "lt013g-setid" : "lt013g-refloc", agps_ril_deferred_worker, work);
    if ((uintptr_t)thread == 0u) {
        free(work);
        return -EAGAIN;
    }
    return 0;
}


struct v398_agps_request_work {
    uint32_t generation;
    AGpsStatus status;
    agps_status_callback framework_cb;
    uint32_t ipv4_net;
    int port;
};

static int v398_probe_default_supl(uint32_t ipv4_net, int port) {
    int fd = -1;
    int flags;
    int rc;
    int so_error = 0;
    socklen_t so_len = sizeof(so_error);
    struct sockaddr_in addr;
    struct pollfd write_ready;

    if (ipv4_net == 0 || port <= 0 || port > 65535)
        return -EINVAL;

    fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        rc = -errno;
        close(fd);
        return rc;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        rc = -errno;
        close(fd);
        return rc;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = ipv4_net;

    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc == 0) {
        close(fd);
        return 0;
    }
    if (errno != EINPROGRESS) {
        rc = -errno;
        close(fd);
        return rc;
    }

    /* G397: select/FD_SET overflows fd_set once the long-lived GNSS
     * service allocates fd >= FD_SETSIZE. poll() has no such fd-number
     * bound and preserves the existing 700 ms SUPL reachability limit. */
    memset(&write_ready, 0, sizeof(write_ready));
    write_ready.fd = fd;
    write_ready.events = POLLOUT;
    rc = poll(&write_ready, 1, 700);
    if (rc == 0) {
        close(fd);
        return -ETIMEDOUT;
    }
    if (rc < 0) {
        rc = -errno;
        close(fd);
        return rc;
    }

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_len) < 0) {
        rc = -errno;
        close(fd);
        return rc;
    }

    close(fd);
    if (so_error != 0)
        return -so_error;
    return 0;
}

/* G403: fail-closed, strictly bounded retries for transient CP transport
 * errors. A failed CLOSE previously exited the only worker while leaving
 * pending_close forever true until some unrelated later SUPL event. */
struct v398_local_close_work {
    uint32_t generation;
};

#define V403_CLOSE_RETRIES 3
#define V403_CLOSE_RETRY_US 100000

static void *v398_local_close_worker(void *opaque) {
    struct v398_local_close_work *work =
            (struct v398_local_close_work *)opaque;
    int current;
    int rc = 0;
    int attempt;

    if (!work) return NULL;
    /* Keep the CP gate during retries. Framework OPEN and other CLOSE
     * workers cannot interleave a new session or issue duplicate commands.
     * At most two 100ms backoffs; never an unbounded polling thread. */
    pthread_mutex_lock(&g_v398_link_lock);
    for (attempt = 0; attempt < V403_CLOSE_RETRIES; ++attempt) {
        pthread_mutex_lock(&g_state_lock);
        /* Re-evaluate each time: another callback can retire the request
         * while the response is in flight. A live local CP belongs to
         * that owner and must not be closed by an obsolete worker. */
        current = g_v398_pending_close && !g_v398_local_data_open;
        pthread_mutex_unlock(&g_state_lock);
        if (!current) {
            ALOGI("V403_CP_CLOSE_OBSOLETE generation=%u attempt=%d",
                  work->generation, attempt + 1);
            break;
        }

        rc = send_command(LT013G_CMD_AGPS_DATA_CONN_CLOSED, NULL, 0);
        if (rc == 0) {
            pthread_mutex_lock(&g_state_lock);
            g_v398_pending_close = 0;
            pthread_mutex_unlock(&g_state_lock);
            ALOGI("V403_CP_CLOSE_SUCCESS generation=%u attempt=%d",
                  work->generation, attempt + 1);
            break;
        }

        ALOGW("V403_CP_CLOSE_TRANSPORT_RETRY generation=%u attempt=%d rc=%d",
              work->generation, attempt + 1, rc);
        if (attempt + 1 < V403_CLOSE_RETRIES)
            usleep(V403_CLOSE_RETRY_US);
    }
    /* When all retries fail, pending_close remains set: new legitimate
     * AGPS requests can retry in their existing serialized CP path. */
    pthread_mutex_unlock(&g_v398_link_lock);
    free(work);
    return NULL;
}

static void v398_defer_local_close(uint32_t generation) {
    struct v398_local_close_work *work;
    pthread_t thread;
    int rc;

    work = (struct v398_local_close_work *)calloc(1, sizeof(*work));
    if (!work) {
        ALOGE("V398_LOCAL_DATA_CONN_CLOSE_ALLOC_FAIL");
        return;
    }
    work->generation = generation;
    rc = pthread_create(&thread, NULL, v398_local_close_worker, work);
    if (rc == 0) {
        (void)pthread_detach(thread);
    } else {
        free(work);
        ALOGE("V398_LOCAL_DATA_CONN_CLOSE_THREAD_FAIL rc=%d", rc);
    }
}

static void *v398_agps_request_worker(void *opaque) {
    struct v398_agps_request_work *work =
            (struct v398_agps_request_work *)opaque;
    struct lt013g_cmd_agps_data_conn_open open_payload;
    uint32_t generation;
    int still_current;
    int pending_close;
    int close_rc = 0;
    int probe_rc;
    int open_rc = -ECANCELED;
    int stale_after_open = 0;
    struct in_addr addr;
    char ipbuf[INET_ADDRSTRLEN];
    const char *iptext = NULL;

    if (!work)
        return NULL;

    generation = work->generation;
    /* G717: A RELEASE or replacement REQUEST may invalidate this work
     * before its thread even starts.  Do not issue a needless TCP SUPL
     * probe (up to 700 ms), and do not produce off-network traffic for a
     * request that has already been retired.  The existing check below,
     * after the probe and under the link lock, is still REQUIRED: a
     * request can be invalidated while poll() is waiting. */
    pthread_mutex_lock(&g_state_lock);
    still_current = generation == g_v398_agps_generation &&
                    !g_v398_local_data_open &&
                    !g_v398_framework_request_active &&
                    !g_v398_framework_data_open;
    pthread_mutex_unlock(&g_state_lock);
    if (!still_current) {
        ALOGI("G717_AGPS_REQUEST_STALE_BEFORE_PROBE generation=%u",
              generation);
        free(work);
        return NULL;
    }

    addr.s_addr = work->ipv4_net;
    if (work->ipv4_net != 0)
        iptext = inet_ntop(AF_INET, &addr, ipbuf, sizeof(ipbuf));

    probe_rc = v398_probe_default_supl(work->ipv4_net, work->port);

    /* An earlier deferred CLOSED must finish before this generation's
     * OPEN, and a delayed older CLOSED must not close a newer session. */
    pthread_mutex_lock(&g_v398_link_lock);
    pthread_mutex_lock(&g_state_lock);
    still_current =
            generation == g_v398_agps_generation &&
            !g_v398_local_data_open &&
            !g_v398_framework_request_active &&
            !g_v398_framework_data_open;
    pthread_mutex_unlock(&g_state_lock);

    if (!still_current) {
        ALOGI("V398_AGPS_REQUEST_CANCELLED generation=%u probe_rc=%d",
              generation, probe_rc);
        pthread_mutex_unlock(&g_v398_link_lock);
        free(work);
        return NULL;
    }

    pthread_mutex_lock(&g_state_lock);
    pending_close = g_v398_pending_close;
    pthread_mutex_unlock(&g_state_lock);
    if (pending_close) {
        close_rc = send_command(LT013G_CMD_AGPS_DATA_CONN_CLOSED, NULL, 0);
        if (close_rc == 0) {
            pthread_mutex_lock(&g_state_lock);
            g_v398_pending_close = 0;
            pthread_mutex_unlock(&g_state_lock);
        } else {
            /* Do not layer another local OPEN on an unclosed CP session. */
            probe_rc = close_rc;
        }
    }
    /* RELEASE may have arrived while waiting for the old CLOSED result. */
    pthread_mutex_lock(&g_state_lock);
    still_current = generation == g_v398_agps_generation &&
                    !g_v398_local_data_open &&
                    !g_v398_framework_request_active &&
                    !g_v398_framework_data_open;
    pthread_mutex_unlock(&g_state_lock);
    if (!still_current) {
        pthread_mutex_unlock(&g_v398_link_lock);
        free(work);
        return NULL;
    }

    if (probe_rc == 0) {
        memset(&open_payload, 0, sizeof(open_payload));
        open_payload.apn_ip_type = APN_IP_IPV4;
        strncpy(open_payload.apn, "default", sizeof(open_payload.apn) - 1u);

        open_rc = send_command(
                LT013G_CMD_AGPS_DATA_CONN_OPEN,
                &open_payload,
                sizeof(open_payload));

        pthread_mutex_lock(&g_state_lock);
        if (generation == g_v398_agps_generation && open_rc == 0) {
            g_v398_local_data_open = 1;
        } else if (open_rc == 0) {
            stale_after_open = 1;
            g_v398_pending_close = 1;
        }
        pthread_mutex_unlock(&g_state_lock);

        ALOGI("V398_DEFAULT_NET_SUPL_PROBE ip=%s port=%d rc=%d",
              iptext ? iptext : "none", work->port, probe_rc);
        ALOGI("V398_LOCAL_DATA_CONN_OPEN generation=%u apn=%s iptype=%u rc=%d stale=%d",
              generation, open_payload.apn, open_payload.apn_ip_type,
              open_rc, stale_after_open);

        if (stale_after_open) {
            close_rc = send_command(
                    LT013G_CMD_AGPS_DATA_CONN_CLOSED, NULL, 0);
            if (close_rc == 0) {
                pthread_mutex_lock(&g_state_lock);
                g_v398_pending_close = 0;
                pthread_mutex_unlock(&g_state_lock);
            }
        }

        if (open_rc == 0) {
            pthread_mutex_unlock(&g_v398_link_lock);
            free(work);
            return NULL;
        }
    } else {
        ALOGI("V398_DEFAULT_NET_SUPL_PROBE ip=%s port=%d rc=%d",
              iptext ? iptext : "none", work->port, probe_rc);
    }

    pthread_mutex_lock(&g_state_lock);
    still_current =
            generation == g_v398_agps_generation &&
            !g_v398_local_data_open;
    if (still_current)
        g_v398_framework_request_active = 1;
    pthread_mutex_unlock(&g_state_lock);

    pthread_mutex_unlock(&g_v398_link_lock);
    if (still_current && work->framework_cb) {
        ALOGI("V398_FRAMEWORK_SUPL_FALLBACK generation=%u reason=default_route_unusable probe_rc=%d open_rc=%d",
              generation, probe_rc, open_rc);
        work->framework_cb(&work->status);
    }

    free(work);
    return NULL;
}

static int v398_defer_agps_request(
        const AGpsStatus *status,
        agps_status_callback framework_cb,
        uint32_t ipv4_net,
        int port) {
    struct v398_agps_request_work *work;
    pthread_t thread;
    uint32_t generation;
    int rc;

    if (!status || !framework_cb)
        return -EINVAL;

    work = (struct v398_agps_request_work *)calloc(1, sizeof(*work));
    if (!work)
        return -ENOMEM;

    pthread_mutex_lock(&g_state_lock);
    generation = ++g_v398_agps_generation;
    if (generation == 0)
        generation = ++g_v398_agps_generation;
    /* A replacement REQUEST can arrive without RELEASE, regardless of
     * whether local Broadcom or Android framework opened the old CP
     * connection. Both must be closed before a new local OPEN. */
    if (g_v398_local_data_open || g_v398_framework_data_open)
        g_v398_pending_close = 1;
    g_v398_local_data_open = 0;
    g_v398_framework_request_active = 0;
    g_v398_framework_data_open = 0;
    pthread_mutex_unlock(&g_state_lock);

    work->generation = generation;
    work->status = *status;
    work->framework_cb = framework_cb;
    work->ipv4_net = ipv4_net;
    work->port = port;

    rc = pthread_create(&thread, NULL, v398_agps_request_worker, work);
    if (rc != 0) {
        free(work);
        return -rc;
    }
    (void)pthread_detach(thread);
    ALOGI("V398_AGPS_REQUEST_DEFER generation=%u ip_valid=%d port=%d",
          generation, ipv4_net != 0, port);
    return 0;
}

/* BCM4752 emits GPS and GLONASS GSV on independent NMEA talkers.
 * Report only complete, checksum-verified snapshots, merging the fresh
 * constellations.  Native legacy SV callbacks keep precedence. */
#define V308_GSV_TALKERS 3u /* GPS/SBAS, GLONASS, QZSS */
#define V308_GSV_SVS_PER_TALKER 32u
#define V308_GSV_STALE_MS 4000LL
struct v308_gsv_group {
    unsigned pages;
    unsigned total;
    unsigned received_pages;
    int64_t partial_started_ms;
    uint32_t seen_svs;
    GnssSvInfo pending[V308_GSV_SVS_PER_TALKER];
    GnssSvInfo complete[V308_GSV_SVS_PER_TALKER];
    unsigned complete_count;
    int64_t complete_at_ms;
    int16_t used_svid[12];
    unsigned used_count;
    int64_t used_at_ms;
};
/* A single legacy SV event must not disable NMEA reporting for the rest
 * of a long navigation session if proprietary gpsd stops issuing updates.
 * Keep native callbacks preferred while they are actually fresh. */
#define V314_NATIVE_SV_FRESH_MS 5000LL
static int64_t g_v314_native_sv_at_ms;
static uint32_t g_v346_gps_sv_count;
static uint32_t g_v346_gnss_sv_count;
static uint32_t g_v346_gpgsv_count;
static uint32_t g_v346_glgsv_count;
static uint32_t g_v348_gqgsv_count;
static uint32_t g_v346_nmea_count;
static int64_t g_v346_last_report_ms;

static int v314_native_sv_recent(void);
static void v346_gnss_report(void) {
    struct timespec ts;
    int64_t now_ms;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return;
    now_ms = (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    if (g_v346_last_report_ms && now_ms - g_v346_last_report_ms < 30000LL) return;
    g_v346_last_report_ms = now_ms;
    ALOGI("G348_GNSS_SOURCE gps_sv=%u gnss_sv=%u nmea=%u gp_gsv=%u gl_gsv=%u gq_gsv=%u native_suppresses_nmea=%d",
          g_v346_gps_sv_count, g_v346_gnss_sv_count, g_v346_nmea_count,
          g_v346_gpgsv_count, g_v346_glgsv_count, g_v348_gqgsv_count, v314_native_sv_recent());
}

static int v314_native_sv_recent(void) {
    struct timespec ts;
    int64_t now_ms;

    if (!g_v314_native_sv_at_ms ||
        clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    now_ms = (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    return now_ms >= g_v314_native_sv_at_ms &&
           now_ms - g_v314_native_sv_at_ms < V314_NATIVE_SV_FRESH_MS;
}

static void v314_native_sv_received(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        g_v314_native_sv_at_ms =
                (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static struct v308_gsv_group g_v308_gsv[V308_GSV_TALKERS];
static int64_t g_v347_native_gnss_at_ms;

static int64_t v347_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int v347_native_gnss_recent(void) {
    int64_t now = v347_now_ms();
    return now && g_v347_native_gnss_at_ms &&
           now >= g_v347_native_gnss_at_ms &&
           now - g_v347_native_gnss_at_ms < V314_NATIVE_SV_FRESH_MS;
}

/*
 * G586: The Android 11 HIDL adapter forwards BOTH gpsSvStatusCb() and
 * gnssSvStatusCb() to the same framework listener. Some Broadcom firmware
 * versions emit an empty legacy GpsSvStatus between valid NMEA GPGSV cycles.
 * An empty legacy callback must not erase a fresh, nonempty GPS-only GSV
 * snapshot (the older merge gate requires a second constellation).
 *
 * This is strictly a display/status de-flicker guard: no invented satellites,
 * no CN0 edits and no suppression once the NMEA snapshot expires.
 */
static int v586_gps_nmea_fresh(void) {
    const struct v308_gsv_group *gps = &g_v308_gsv[0];
    int64_t now_ms = v347_now_ms();

    return now_ms > 0 && gps->complete_count > 0 &&
           gps->complete_at_ms > 0 &&
           now_ms >= gps->complete_at_ms &&
           now_ms - gps->complete_at_ms <= V308_GSV_STALE_MS;
}

static int v347_merged_nmea_fresh(void) {
    int64_t now = v347_now_ms();
    unsigned i, j;
    int gps_fresh = 0, extra_constellation = 0;
    if (!now) return 0;
    for (i = 0; i < V308_GSV_TALKERS; ++i) {
        const struct v308_gsv_group *group = &g_v308_gsv[i];
        if (!group->complete_count || !group->complete_at_ms ||
            now < group->complete_at_ms ||
            now - group->complete_at_ms > V308_GSV_STALE_MS) continue;
        if (i == 0) gps_fresh = 1;
        for (j = 0; j < group->complete_count; ++j)
            if (group->complete[j].constellation != GNSS_CONSTELLATION_GPS)
                extra_constellation = 1;
    }
    /* Keep the native GPS-only status until we can replace it with
     * a complete GPS snapshot plus real SBAS / GLONASS / QZSS. */
    return gps_fresh && extra_constellation;
}

static int v308_nmea_number(const char *field, unsigned max, unsigned *value) {
    char *end;
    unsigned long parsed;
    if (!field || !*field) return 0;
    errno = 0;
    parsed = strtoul(field, &end, 10);
    if (errno || *end || parsed > max) return 0;
    *value = (unsigned)parsed;
    return 1;
}

static int v308_nmea_checksum(const char *line, size_t length,
                              char *copy, size_t size) {
    const char *star;
    unsigned checksum = 0, expected;
    const char *p;
    if (length + 1 > size || length < 12 || line[0] != '$') return 0;
    /* A counted NMEA frame must not hide extra bytes behind an interior NUL.
     * Require exactly *HH, optionally followed by CR, LF or CRLF. */
    if (memchr(line, '\0', length)) return 0;
    memcpy(copy, line, length);
    copy[length] = '\0';
    star = strchr(copy, '*');
    if (!star) return 0;
    /* Count bytes rather than probing star[4]/star[5] past the input. */
    size_t suffix = length - (size_t)(star - copy);
    if (suffix < 3 || suffix > 5 ||
        !isxdigit((unsigned char)star[1]) ||
        !isxdigit((unsigned char)star[2])) return 0;
    if (suffix == 4 && star[3] != '\r' && star[3] != '\n') return 0;
    if (suffix == 5 && (star[3] != '\r' || star[4] != '\n')) return 0;
    for (p = copy + 1; p < star; ++p) checksum ^= (unsigned char)*p;
    expected = (unsigned)strtoul(star + 1, NULL, 16);
    if ((checksum & 0xffu) != expected) return 0;
    copy[star - copy] = '\0';
    return 1;
}

static int v308_nmea_svid(unsigned talker, unsigned prn,
                           int16_t *svid, GnssConstellationType *constellation) {
    if (talker == 0) {
        if (prn >= 1 && prn <= 32) {
            *svid = (int16_t)prn;
            *constellation = GNSS_CONSTELLATION_GPS;
        } else if (prn >= 33 && prn <= 64) {
            /* Legacy NMEA GPGSV offsets SBAS PRN33..64 to 120..151. */
            *svid = (int16_t)(prn + 87);
            *constellation = GNSS_CONSTELLATION_SBAS;
        } else if ((prn >= 120 && prn <= 151) ||
                   (prn >= 183 && prn <= 192)) {
            /* NMEA receivers can also publish canonical SBAS PRNs.
             * Accept only the ranges defined by Android GNSS API, do
             * not re-offset them or invent unsupported PRNs. */
            *svid = (int16_t)prn;
            *constellation = GNSS_CONSTELLATION_SBAS;
        } else if (prn >= 193 && prn <= 200) {
            *svid = (int16_t)prn;
            *constellation = GNSS_CONSTELLATION_QZSS;
        } else {
            return 0;
        }
    } else if (talker == 2) {
        /* BCM4752 LIVE emits $QZGSV local SV=02; other receivers emit
         * $GQGSV SV=193..200. Map receiver-local 01..08 to Android's
         * SVID 193..200, preserving already-global PRN 193..200. */
        if (prn >= 1 && prn <= 8) {
            *svid = (int16_t)(prn + 192u);
        } else if (prn >= 193 && prn <= 200) {
            *svid = (int16_t)prn;
        } else {
            return 0;
        }
        *constellation = GNSS_CONSTELLATION_QZSS;
    } else if (talker == 1) {
        if (prn >= 65 && prn <= 88) {
            *svid = (int16_t)(prn - 64);
        } else if (prn >= 1 && prn <= 24) {
            *svid = (int16_t)prn;
        } else {
            /* Raw NMEA 89..96 is not an Android GNSS OSN. Without
             * receiver-provided FCN metadata, do not invent an orbit. */
            return 0;
        }
        *constellation = GNSS_CONSTELLATION_GLONASS;
    } else {
        return 0;
    }
    return 1;
}


/* GSA contains receiver-confirmed satellites used in a fix. Prefer GP/GL
 * talker IDs; GN is accepted only with explicit NMEA system ID. The physical
 * Broadcom also emits GN mode=1 without system ID, which clears both sets. */
static void v312_gsa_from_nmea(const struct lt013g_cb_nmea *payload) {
    char line[LT013G_BRIDGE_NMEA_MAX + 1];
    char *cursor, *field;
    unsigned talker, mode, system_id = 0, prn, raw[12], raw_count = 0;
    unsigned i, count = 0;
    int16_t used[12];
    struct timespec ts;
    int64_t now_ms;
    struct v308_gsv_group *group;

    if (!payload || v347_native_gnss_recent() ||
        payload->length > LT013G_BRIDGE_NMEA_MAX ||
        payload->length < 12) return;
    if (memcmp(payload->data, "$GPGSA,", 7) == 0)
        talker = 0;
    else if (memcmp(payload->data, "$GLGSA,", 7) == 0)
        talker = 1;
    else if (memcmp(payload->data, "$GQGSA,", 7) == 0 ||
             memcmp(payload->data, "$QZGSA,", 7) == 0)
        talker = 2;
    else if (memcmp(payload->data, "$GNGSA,", 7) == 0)
        talker = V308_GSV_TALKERS; /* GN: system ID follows DOP fields */
    else
        return;

    if (!v308_nmea_checksum(payload->data, payload->length,
                            line, sizeof(line))) return;
    cursor = line + 1;
    (void)strsep(&cursor, ","); /* talker */
    if (!strsep(&cursor, ",")) return; /* A or M */
    if (!v308_nmea_number(strsep(&cursor, ","), 3, &mode) ||
        mode < 1) return;
    for (i = 0; i < 12; ++i) {
        field = strsep(&cursor, ",");
        if (!field) return;
        if (*field && v308_nmea_number(field, 200, &prn))
            raw[raw_count++] = prn;
    }
    if (talker == V308_GSV_TALKERS) {
        /* NMEA 4.10+ $GNGSA field 18 is the constellation System ID:
         * 1=GPS/SBAS, 2=GLONASS. Without an ID, ambiguous PRNs cannot
         * be reliably attributed. No-fix GN with no ID clears both. */
        for (i = 0; i < 3; ++i)
            if (!strsep(&cursor, ",")) return; /* PDOP, HDOP, VDOP */
        field = strsep(&cursor, ",");
        if (field && *field && v308_nmea_number(field, 6, &system_id)) {
            if (system_id == 1)
                talker = 0;
            else if (system_id == 2)
                talker = 1;
            else if (system_id == 5)
                talker = 2; /* NMEA 4.x QZSS system ID */
            else
                return;
        } else if (mode != 1) {
            return;
        }
    }
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return;
    now_ms = (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;

    if (mode == 1 && talker == V308_GSV_TALKERS) {
        /* Broadcom's actual $GNGSA,A,1 carries no System ID. */
        for (i = 0; i < V308_GSV_TALKERS; ++i) {
            g_v308_gsv[i].used_count = 0;
            g_v308_gsv[i].used_at_ms = now_ms;
        }
        return;
    }
    if (talker >= V308_GSV_TALKERS) return;
    for (i = 0; i < raw_count && mode != 1; ++i) {
        int16_t svid;
        GnssConstellationType constellation;
        if (v308_nmea_svid(talker, raw[i], &svid, &constellation))
            used[count++] = svid;
    }
    group = &g_v308_gsv[talker];
    memcpy(group->used_svid, used, count * sizeof(used[0]));
    group->used_count = count;
    group->used_at_ms = now_ms;
}

static void v308_gsv_from_nmea(const struct lt013g_cb_nmea *payload,
                               gnss_sv_status_callback callback) {
    char line[LT013G_BRIDGE_NMEA_MAX + 1];
    char *cursor, *field;
    struct v308_gsv_group *group;
    unsigned talker, pages, page, total, prn, elevation, azimuth, cn0;
    unsigned i, j;
    struct timespec ts;
    int64_t now_ms;
    GnssSvStatus status;

    if (!callback || v347_native_gnss_recent() ||
        payload->length > LT013G_BRIDGE_NMEA_MAX ||
        payload->length < 12) return;
    if (payload->length >= 7 &&
        memcmp(payload->data, "$GPGSV,", 7) == 0)
        talker = 0;
    else if (payload->length >= 7 &&
             memcmp(payload->data, "$GLGSV,", 7) == 0)
        talker = 1;
    else if (payload->length >= 7 &&
             (memcmp(payload->data, "$GQGSV,", 7) == 0 ||
              memcmp(payload->data, "$QZGSV,", 7) == 0))
        talker = 2;
    else
        return;
    if (!v308_nmea_checksum(payload->data, payload->length, line, sizeof(line)))
        return;
    cursor = line + 1;
    field = strsep(&cursor, ",");
    if (!field || !v308_nmea_number(strsep(&cursor, ","), 8, &pages) ||
        pages == 0 || !v308_nmea_number(strsep(&cursor, ","), 8, &page) ||
        page == 0 || !v308_nmea_number(strsep(&cursor, ","), 32, &total) ||
        page > pages || total > V308_GSV_SVS_PER_TALKER ||
        (total == 0 ? (pages != 1 || page != 1) :
                      pages != (total + 3u) / 4u)) return;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return;
    now_ms = (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    group = &g_v308_gsv[talker];
    /* GSV frames can arrive as 2/2 then 1/2 on the real BCM4752.
     * Start on ANY page; reassemble the matching fresh cycle. A repeated
     * page or an older partial cycle begins a new snapshot. */
    if (!group->received_pages || group->pages != pages ||
        group->total != total ||
        now_ms - group->partial_started_ms > 1500LL ||
        (group->received_pages & (1u << (page - 1)))) {
        group->pages = pages;
        group->total = total;
        group->received_pages = 0;
        group->seen_svs = 0;
        group->partial_started_ms = now_ms;
        memset(group->pending, 0, sizeof(group->pending));
    }

    for (i = 0; i < 4 && (page - 1) * 4 + i < total; ++i) {
        GnssSvInfo *sat;
        unsigned index = (page - 1) * 4 + i;
        int16_t svid;
        GnssConstellationType constellation;
        char *prn_field = strsep(&cursor, ",");
        char *elevation_field = strsep(&cursor, ",");
        char *azimuth_field = strsep(&cursor, ",");
        char *cn0_field = strsep(&cursor, ",");
        if (!v308_nmea_number(prn_field, 200, &prn) ||
            !v308_nmea_svid(talker, prn, &svid, &constellation)) continue;
        elevation = 0;
        azimuth = 0;
        if ((elevation_field && *elevation_field &&
             !v308_nmea_number(elevation_field, 90, &elevation)) ||
            (azimuth_field && *azimuth_field &&
             !v308_nmea_number(azimuth_field, 359, &azimuth))) continue;
        cn0 = 0;
        if (cn0_field && *cn0_field &&
            !v308_nmea_number(cn0_field, 99, &cn0)) continue;
        sat = &group->pending[index];
        group->seen_svs |= (uint32_t)1u << index;
        sat->size = sizeof(*sat);
        sat->svid = svid;
        sat->constellation = constellation;
        sat->c_n0_dbhz = (float)cn0;
        sat->elevation = (float)elevation;
        sat->azimuth = (float)azimuth;
    }
    group->received_pages |= 1u << (page - 1);
    if (group->received_pages != (1u << pages) - 1u) return;
    group->received_pages = 0;
    group->complete_count = 0;
    for (i = 0; i < total; ++i) {
        if ((group->seen_svs & ((uint32_t)1u << i)) == 0) continue;
        group->complete[group->complete_count++] = group->pending[i];
    }
    group->complete_at_ms = now_ms;
    if (v314_native_sv_recent() && !v347_merged_nmea_fresh()) return;

    memset(&status, 0, sizeof(status));
    status.size = sizeof(status);
    for (i = 0; i < V308_GSV_TALKERS; ++i) {
        const struct v308_gsv_group *fresh = &g_v308_gsv[i];
        if (!fresh->complete_at_ms ||
            now_ms - fresh->complete_at_ms > V308_GSV_STALE_MS) continue;
        for (j = 0; j < fresh->complete_count && status.num_svs < GNSS_MAX_SVS; ++j) {
            unsigned k;
            GnssSvInfo *sat;
            /* GP can also contain QZSS and GQ can repeat the same PRN.
             * Merge by constellation+SVID, never double-count a satellite. */
            for (k = 0; k < (unsigned)status.num_svs; ++k)
                if (status.gnss_sv_list[k].constellation == fresh->complete[j].constellation &&
                    status.gnss_sv_list[k].svid == fresh->complete[j].svid) break;
            if (k < (unsigned)status.num_svs) {
                sat = &status.gnss_sv_list[k];
                if (fresh->complete[j].c_n0_dbhz > sat->c_n0_dbhz)
                    sat->c_n0_dbhz = fresh->complete[j].c_n0_dbhz;
            } else {
                sat = &status.gnss_sv_list[status.num_svs++];
                *sat = fresh->complete[j];
            }
            if (fresh->used_at_ms && now_ms - fresh->used_at_ms <= V308_GSV_STALE_MS)
                for (k = 0; k < fresh->used_count; ++k)
                    if (sat->svid == fresh->used_svid[k]) {
                        sat->flags |= GNSS_SV_FLAGS_USED_IN_FIX;
                        break;
                    }
        }
    }
    {
        static int last_count = -1;
        static int64_t last_notice_ms;
        if (last_count != status.num_svs ||
            now_ms - last_notice_ms >= 30000LL) {
            unsigned gps_count = 0, gl_count = 0, qz_count = 0, sbas_count = 0;
            for (i = 0; i < (unsigned)status.num_svs; ++i) {
                if (status.gnss_sv_list[i].constellation == GNSS_CONSTELLATION_GPS) ++gps_count;
                else if (status.gnss_sv_list[i].constellation == GNSS_CONSTELLATION_GLONASS) ++gl_count;
                else if (status.gnss_sv_list[i].constellation == GNSS_CONSTELLATION_QZSS) ++qz_count;
                else if (status.gnss_sv_list[i].constellation == GNSS_CONSTELLATION_SBAS) ++sbas_count;
            }
            ALOGI("G348_NMEA_GNSS_SV gps=%u glonass=%u qzss=%u sbas=%u total=%d",
                  gps_count, gl_count, qz_count, sbas_count, status.num_svs);
            last_count = status.num_svs;
            last_notice_ms = now_ms;
        }
    }
    callback(&status);
}

/* The BCM4752 in this tablet still emits RMC dates one 1024-week GPS
 * epoch behind.  Only correct the public NMEA sentence when the result
 * agrees with the real Android UTC date (within 14 days). This never changes
 * the receiver, almanac, SUPL state, location fix flags or user clock. */
static int v309_correct_rmc_date(const struct lt013g_cb_nmea *payload,
                                 char *out, size_t capacity) {
    const int64_t rollover_seconds = 1024LL * 7LL * 86400LL;
    const int64_t window_seconds = 14LL * 86400LL;
    struct tm old, modern;
    time_t old_time, current_time, new_time;
    char *p, *star, *date;
    size_t i;
    unsigned day, month, year, checksum;
    if (!payload || payload->length + 1u > capacity ||
        payload->length < 17u) return 0;
    if (memcmp(payload->data, "$GPRMC,", 7) != 0 &&
        memcmp(payload->data, "$GNRMC,", 7) != 0) return 0;
    memcpy(out, payload->data, payload->length);
    out[payload->length] = '\0';
    star = strchr(out, '*');
    if (!star || star[1] == '\0' || star[2] == '\0' ||
        !isxdigit((unsigned char)star[1]) ||
        !isxdigit((unsigned char)star[2])) return 0;
    checksum = 0;
    for (p = out + 1; p < star; ++p) checksum ^= (unsigned char)*p;
    if ((checksum & 0xffu) != (unsigned)strtoul(star + 1, NULL, 16))
        return 0;
    p = out + 7;
    for (i = 0; i < 8; ++i) {
        p = strchr(p, ',');
        if (!p || p >= star) return 0;
        ++p;
    }
    date = p;
    if (date + 6 >= star || date[6] != ',') return 0;
    for (i = 0; i < 6; ++i)
        if (!isdigit((unsigned char)date[i])) return 0;
    day = (unsigned)(date[0] - '0') * 10u + (unsigned)(date[1] - '0');
    month = (unsigned)(date[2] - '0') * 10u + (unsigned)(date[3] - '0');
    year = (unsigned)(date[4] - '0') * 10u + (unsigned)(date[5] - '0');
    if (day == 0 || day > 31 || month == 0 || month > 12) return 0;
    memset(&old, 0, sizeof(old));
    old.tm_year = (int)(2000 + year) - 1900;
    old.tm_mon = (int)month - 1;
    old.tm_mday = (int)day;
    old_time = timegm(&old);
    current_time = time(NULL);
    new_time = old_time + rollover_seconds;
    if (current_time <= 0 || new_time <= 0 ||
        llabs((long long)(new_time - current_time)) > window_seconds ||
        !gmtime_r(&new_time, &modern)) return 0;
    day = (unsigned)modern.tm_mday;
    month = (unsigned)modern.tm_mon + 1u;
    year = (unsigned)(modern.tm_year + 1900) % 100u;
    date[0] = (char)('0' + day / 10u);
    date[1] = (char)('0' + day % 10u);
    date[2] = (char)('0' + month / 10u);
    date[3] = (char)('0' + month % 10u);
    date[4] = (char)('0' + year / 10u);
    date[5] = (char)('0' + year % 10u);
    checksum = 0;
    for (p = out + 1; p < star; ++p) checksum ^= (unsigned char)*p;
    static const char hex[] = "0123456789ABCDEF";
    star[1] = hex[(checksum >> 4) & 15u];
    star[2] = hex[checksum & 15u];
    return 1;
}

static void dispatch_callback(const struct lt013g_bridge_packet *packet) {
    GpsCallbacks callbacks_copy;
    const GpsCallbacks *callbacks = &callbacks_copy;
    int session_started;
    pthread_mutex_lock(&g_state_lock);
    if (!g_callbacks_valid) {
        pthread_mutex_unlock(&g_state_lock);
        return;
    }
    memcpy(&callbacks_copy, &g_callbacks_storage, sizeof(callbacks_copy));
    session_started = g_session_started;
    pthread_mutex_unlock(&g_state_lock);
    switch (packet->type) {
        case LT013G_CB_LOCATION:
            if (callbacks->location_cb && packet->payload_size >= sizeof(struct lt013g_cb_location)) {
                struct lt013g_cb_location payload;
                GpsLocation location;
                memcpy(&payload, packet->payload, sizeof(payload));
                memset(&location, 0, sizeof(location));
                location.size = sizeof(location);
                location.flags = payload.flags;
                location.latitude = payload.latitude;
                location.longitude = payload.longitude;
                location.altitude = payload.altitude;
                location.speed = payload.speed;
                location.bearing = payload.bearing;
                location.accuracy = payload.accuracy;
                location.timestamp = payload.timestamp_ms;
                callbacks->location_cb(&location);
            }
            break;
        case LT013G_CB_STATUS:
            if (callbacks->status_cb && packet->payload_size >= sizeof(struct lt013g_cb_status)) {
                struct lt013g_cb_status payload;
                GpsStatus status;
                memcpy(&payload, packet->payload, sizeof(payload));
                memset(&status, 0, sizeof(status));
                status.size = sizeof(status);
                status.status = payload.status;
                callbacks->status_cb(&status);
            }
            break;
        case LT013G_CB_SV:
            if (callbacks->sv_status_cb && packet->payload_size >= sizeof(struct lt013g_cb_sv)) {
                struct lt013g_cb_sv payload;
                GpsSvStatus status;
                int count;
                int i;
                memcpy(&payload, packet->payload, sizeof(payload));
                memset(&status, 0, sizeof(status));
                status.size = sizeof(status);
                count = payload.num_svs;
                if (count < 0) count = 0;
                if (count > GPS_MAX_SVS) count = GPS_MAX_SVS;
                status.num_svs = count;
                status.ephemeris_mask = payload.ephemeris_mask;
                status.almanac_mask = payload.almanac_mask;
                status.used_in_fix_mask = payload.used_in_fix_mask;
                for (i = 0; i < count; ++i) {
                    status.sv_list[i].size = sizeof(status.sv_list[i]);
                    status.sv_list[i].prn = payload.sv[i].prn;
                    status.sv_list[i].snr = payload.sv[i].snr;
                    status.sv_list[i].elevation = payload.sv[i].elevation;
                    status.sv_list[i].azimuth = payload.sv[i].azimuth;
                }
                if (count > 0) v314_native_sv_received();
                ++g_v346_gps_sv_count;
                v346_gnss_report();
                /* G586: an empty native GPS SV callback must not blank a
                 * fresh nonempty NMEA GPS snapshot on Android HIDL 1.0.
                 * Keep every nonempty native callback and the native-only
                 * fallback path unchanged. */
                if (!v347_merged_nmea_fresh() &&
                    !(count == 0 && v586_gps_nmea_fresh()))
                    callbacks->sv_status_cb(&status);
            }
            break;
        case LT013G_CB_GNSS_SV:
            if (callbacks->gnss_sv_status_cb &&
                packet->payload_size >= sizeof(struct lt013g_cb_gnss_sv)) {
                struct lt013g_cb_gnss_sv payload;
                GnssSvStatus status;
                int count;
                int i;
                memcpy(&payload, packet->payload, sizeof(payload));
                memset(&status, 0, sizeof(status));
                status.size = sizeof(status);
                count = payload.num_svs;
                if (count < 0) count = 0;
                if (count > GNSS_MAX_SVS) count = GNSS_MAX_SVS;
                status.num_svs = count;
                for (i = 0; i < count; ++i) {
                    status.gnss_sv_list[i].size = sizeof(status.gnss_sv_list[i]);
                    status.gnss_sv_list[i].svid = payload.sv[i].svid;
                    status.gnss_sv_list[i].constellation =
                            (GnssConstellationType)payload.sv[i].constellation;
                    status.gnss_sv_list[i].c_n0_dbhz = payload.sv[i].c_n0_dbhz;
                    status.gnss_sv_list[i].elevation = payload.sv[i].elevation;
                    status.gnss_sv_list[i].azimuth = payload.sv[i].azimuth;
                    status.gnss_sv_list[i].flags = payload.sv[i].flags;
                }
                if (count > 0) {
                    v314_native_sv_received();
                    g_v347_native_gnss_at_ms = v347_now_ms();
                }
                ++g_v346_gnss_sv_count;
                v346_gnss_report();
                callbacks->gnss_sv_status_cb(&status);
            }
            break;
        case LT013G_CB_SYSTEM_INFO:
            if (callbacks->set_system_info_cb &&
                packet->payload_size >= sizeof(struct lt013g_cb_system_info)) {
                struct lt013g_cb_system_info payload;
                GnssSystemInfo info;
                memcpy(&payload, packet->payload, sizeof(payload));
                memset(&info, 0, sizeof(info));
                info.size = sizeof(info);
                info.year_of_hw = payload.year_of_hw;
                callbacks->set_system_info_cb(&info);
            }
            break;
        case LT013G_CB_LEGACY_EXTENSIONS:
            if (packet->payload_size >= sizeof(struct lt013g_cb_legacy_extensions)) {
                struct lt013g_cb_legacy_extensions payload;
                memcpy(&payload, packet->payload, sizeof(payload));
                ALOGI("V290_API25_EXTENSIONS mask=0x%08x gps-xtra=%u agps=%u agps_ril=%u gps-ni=%u geofence=%u debug=%u",
                      payload.mask,
                      !!(payload.mask & (1u << 0)), !!(payload.mask & (1u << 1)),
                      !!(payload.mask & (1u << 2)), !!(payload.mask & (1u << 3)),
                      !!(payload.mask & (1u << 4)), !!(payload.mask & (1u << 5)));
            }
            break;
        case LT013G_CB_NMEA:
            if (packet->payload_size >= sizeof(struct lt013g_cb_nmea)) {
                struct lt013g_cb_nmea payload;
                memcpy(&payload, packet->payload, sizeof(payload));
                ++g_v346_nmea_count;
                if (payload.length >= 7 && memcmp(payload.data, "$GPGSV,", 7) == 0)
                    ++g_v346_gpgsv_count;
                else if (payload.length >= 7 && memcmp(payload.data, "$GLGSV,", 7) == 0)
                    ++g_v346_glgsv_count;
                else if (payload.length >= 7 &&
                         (memcmp(payload.data, "$GQGSV,", 7) == 0 ||
                          memcmp(payload.data, "$QZGSV,", 7) == 0))
                    ++g_v348_gqgsv_count;
                v346_gnss_report();
                pthread_mutex_lock(&g_state_lock);
                ++g_stats.nmea_count;
                if (g_stats.gpsd_last_generation > 1) {
                    ++g_stats.nmea_after_restart;
                    g_stats.last_nmea_generation = g_stats.gpsd_last_generation;
                }
                pthread_mutex_unlock(&g_state_lock);

                /* The Android GNSS satellite-status listener is independent
                 * of the optional raw NMEA listener. The latter must not
                 * suppress GSV/GSA processing or used-in-fix flags. */
                if (session_started && callbacks->gnss_sv_status_cb) {
                    v312_gsa_from_nmea(&payload);
                    v308_gsv_from_nmea(&payload, callbacks->gnss_sv_status_cb);
                }
                if (callbacks->nmea_cb) {
                    char modern_nmea[LT013G_BRIDGE_NMEA_MAX + 1];
                    const char *nmea = payload.data;
                    uint32_t length = payload.length;
                    if (length > LT013G_BRIDGE_NMEA_MAX)
                        length = LT013G_BRIDGE_NMEA_MAX;
                    if (v309_correct_rmc_date(&payload, modern_nmea,
                                              sizeof(modern_nmea))) {
                        nmea = modern_nmea;
                    }
                    callbacks->nmea_cb(payload.timestamp_ms, nmea, (int)length);
                }
            }
            break;
        case LT013G_CB_CAPABILITIES:
            if (callbacks->set_capabilities_cb && packet->payload_size >= sizeof(struct lt013g_cb_capabilities)) {
                struct lt013g_cb_capabilities payload;
                memcpy(&payload, packet->payload, sizeof(payload));
                /* The API25 engine's inject_time is bridged on Android 11.
                 * Offer NTP assistance to the 2007-week-era Broadcom receiver. */
                payload.capabilities |= GPS_CAPABILITY_ON_DEMAND_TIME;
                ALOGI("V357_CAPABILITIES_PROXY exposed=0x%08x msb=%u msa=%u",
                      payload.capabilities,
                      !!(payload.capabilities & GPS_CAPABILITY_MSB),
                      !!(payload.capabilities & GPS_CAPABILITY_MSA));
                callbacks->set_capabilities_cb(payload.capabilities);
            }
            break;
        case LT013G_CB_WAKELOCK_ACQUIRE:
            if (callbacks->acquire_wakelock_cb) callbacks->acquire_wakelock_cb();
            break;
        case LT013G_CB_WAKELOCK_RELEASE:
            if (callbacks->release_wakelock_cb) callbacks->release_wakelock_cb();
            break;
        case LT013G_CB_REQUEST_UTC:
            if (callbacks->request_utc_time_cb) callbacks->request_utc_time_cb();
            break;
        case LT013G_CB_GPSD_EVENT:
            if (packet->payload_size >= sizeof(struct lt013g_cb_gpsd_event)) {
                struct lt013g_cb_gpsd_event payload;
                memcpy(&payload, packet->payload, sizeof(payload));
                uint32_t effective_generation;
                pthread_mutex_lock(&g_state_lock);
                effective_generation = g_effective_generation;
                if (effective_generation < payload.generation) effective_generation = payload.generation;
                g_stats.gpsd_last_generation = effective_generation;
                if (payload.event == LT013G_GPSD_EVENT_SPAWN) ++g_stats.gpsd_spawn_count;
                else if (payload.event == LT013G_GPSD_EVENT_EXIT) {
                    int idle_session = g_session_initialized && !g_session_started && !g_supervisor_stop;
                    if (payload.expected || idle_session) ++g_stats.gpsd_expected_exit_count;
                    else ++g_stats.gpsd_unexpected_exit_count;
                    if (idle_session) {
                        /* Broadcom's API25 gpsd exits with code 1 after a normal
                         * HAL stop().  Keep the bridge process parked while idle,
                         * but force INIT/mode replay before the next command that
                         * needs gpsd (DELETE_AIDING / SET_POSITION_MODE / START). */
                        g_reconnect_needed = 1;
                        ALOGI("V311_IDLE_GPSD_EXIT_LAZY_REPLAY generation=%u status=%d",
                              payload.generation, payload.status);
                    }
                } else if (payload.event == LT013G_GPSD_EVENT_HAL_RECOVERY && payload.status == 0) {
                    ++g_stats.gpsd_hal_recovery_count;
                }
                pthread_mutex_unlock(&g_state_lock);
                ALOGI("gpsd event=%u generation=%u effective=%u status=%d expected=%u",
                      payload.event, payload.generation, effective_generation,
                      payload.status, payload.expected);
            }
            break;
        case LT013G_CB_XTRA_DOWNLOAD_REQUEST:
            pthread_mutex_lock(&g_state_lock);
            if (g_xtra_callbacks_valid && g_xtra_callbacks_storage.download_request_cb) {
                gps_xtra_download_request cb = g_xtra_callbacks_storage.download_request_cb;
                pthread_mutex_unlock(&g_state_lock);
                if (v392_property_enabled("debug.lt013g.gps.v357.lto")) {
                    ALOGI("G357_XTRA_DOWNLOAD_REQUEST_TO_FRAMEWORK gated=1");
                    cb();
                } else {
                    ALOGI("G357_XTRA_DOWNLOAD_REQUEST_SUPPRESSED gated=0");
                }
            } else {
                pthread_mutex_unlock(&g_state_lock);
                ALOGW("V341_XTRA_DOWNLOAD_REQUEST proxy_forward=0 callbacks_missing=1");
            }
            break;
        case LT013G_CB_AGPS_STATUS:
            if (packet->payload_size >= sizeof(struct lt013g_cb_agps_status)) {
                struct lt013g_cb_agps_status payload;
                AGpsStatus status;
                agps_status_callback cb = NULL;
                memcpy(&payload, packet->payload, sizeof(payload));
                memset(&status, 0, sizeof(status));
                uint16_t output_family = payload.family;
                const char *compat_source = "native";
                uint32_t compat_ipv4_net = 0;
                int compat_ipv4_valid = 0;

                status.size = sizeof(status);
                status.type = payload.type;
                status.status = payload.status;
                status.ipaddr = payload.ipaddr;

                if (payload.family == AF_INET) {
                    struct sockaddr_in *in = (struct sockaddr_in *)&status.addr;
                    in->sin_family = AF_INET;
                    memcpy(&in->sin_addr, payload.address, sizeof(in->sin_addr));
                } else if (payload.family == AF_INET6) {
                    struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&status.addr;
                    in6->sin6_family = AF_INET6;
                    memcpy(&in6->sin6_addr, payload.address, sizeof(in6->sin6_addr));
                } else if (payload.family == 0) {
                    if (payload.ipaddr != 0) {
                        /*
                         * Legacy AGpsStatus_v2 ipaddr is host byte order.
                         * Android's compatibility wrapper historically applies
                         * htonl() before forwarding it to the framework.
                         */
                        compat_ipv4_net = htonl(payload.ipaddr);
                        compat_ipv4_valid = 1;
                        compat_source = "legacy-ipaddr";
                    } else {
                        pthread_mutex_lock(&g_state_lock);
                        if (g_v397_supl_ipv4_valid) {
                            compat_ipv4_net = g_v397_supl_ipv4_net;
                            compat_ipv4_valid = 1;
                        }
                        pthread_mutex_unlock(&g_state_lock);
                        compat_source = "dns-cache";
                    }

                    if (compat_ipv4_valid) {
                        struct sockaddr_in *in = (struct sockaddr_in *)&status.addr;
                        in->sin_family = AF_INET;
                        in->sin_addr.s_addr = compat_ipv4_net;
                        output_family = AF_INET;
                    } else if (payload.type == AGPS_TYPE_SUPL &&
                               (payload.status == GPS_REQUEST_AGPS_DATA_CONN ||
                                payload.status == GPS_RELEASE_AGPS_DATA_CONN)) {
                        /*
                         * G396: legacy BCM4752 can request/release SUPL
                         * before DNS resolves and with no IPv4 in AGpsStatus.
                         * AF_UNSPEC is silently dropped by Android 11's
                         * GNSS HIDL 1.0 AGnss::statusCb() implementation.
                         * AOSP's JNI convertToIpV4(INADDR_NONE) explicitly
                         * translates 255.255.255.255 to a NULL Java IP;
                         * Android can then request the SUPL network without
                         * trying to route to an imaginary host.
                         * This is a callback marker, NEVER a socket target.
                         */
                        struct sockaddr_in *in =
                                (struct sockaddr_in *)&status.addr;
                        in->sin_family = AF_INET;
                        in->sin_addr.s_addr = INADDR_NONE;
                        output_family = AF_INET;
                        compat_source = "no-supl-ip-jni-null";
                    }
                }

                if (payload.family != 0 && payload.family != AF_INET &&
                    payload.family != AF_INET6) {
                    output_family = payload.family;
                    status.addr.ss_family = payload.family;
                }

                pthread_mutex_lock(&g_state_lock);
                if (g_agps_callbacks_valid) cb = g_agps_callbacks_storage.status_cb;
                pthread_mutex_unlock(&g_state_lock);

                if (payload.family == 0) {
                    struct in_addr a;
                    char ipbuf[INET_ADDRSTRLEN];
                    const char *iptext = NULL;
                    a.s_addr = compat_ipv4_net;
                    if (compat_ipv4_valid)
                        iptext = inet_ntop(AF_INET, &a, ipbuf, sizeof(ipbuf));
                    ALOGI("V397_AGPS_STATUS_COMPAT type=%u status=%u input_family=0 input_ipaddr=0x%08x output_family=%u source=%s ip=%s valid=%d",
                          payload.type, payload.status, payload.ipaddr,
                          output_family, compat_source,
                          iptext ? iptext : "none", compat_ipv4_valid);
                }

                ALOGI("V341_AGPS_STATUS proxy_forward=%d type=%u status=%u family=%u output_family=%u",
                      cb != NULL, payload.type, payload.status,
                      payload.family, output_family);

                if (cb && payload.type == AGPS_TYPE_SUPL &&
                    payload.status == GPS_REQUEST_AGPS_DATA_CONN) {
                    uint32_t request_ipv4 = 0;
                    int request_port = 0;
                    int defer_rc;

                    if (status.addr.ss_family == AF_INET) {
                        const struct sockaddr_in *in =
                                (const struct sockaddr_in *)&status.addr;
                        request_ipv4 = in->sin_addr.s_addr;
                    }
                    /* G396: JNI-only NULL-address marker must never be used
                     * as the destination of the local TCP SUPL probe. */
                    if (request_ipv4 == INADDR_NONE)
                        request_ipv4 = 0;

                    pthread_mutex_lock(&g_state_lock);
                    if (request_ipv4 == 0 && g_v397_supl_ipv4_valid)
                        request_ipv4 = g_v397_supl_ipv4_net;
                    if (g_agps_server_valid)
                        request_port = g_agps_server_cache.port;
                    pthread_mutex_unlock(&g_state_lock);

                    defer_rc = v398_defer_agps_request(
                            &status, cb, request_ipv4, request_port);
                    ALOGI("V398_AGPS_REQUEST_ROUTE defer_rc=%d ip_valid=%d port=%d",
                          defer_rc, request_ipv4 != 0, request_port);
                    if (defer_rc != 0) {
                        uint32_t fallback_generation;
                        int old_cp_open_or_pending;

                        /* G401: if allocating/spawning the async SUPL worker
                         * fails, a direct framework fallback must still retire
                         * the old CP connection. Otherwise an old local OPEN
                         * suppresses the replacement framework OPEN forever.
                         * Serialize CP CLOSED with the existing link mutex. */
                        pthread_mutex_lock(&g_state_lock);
                        fallback_generation = ++g_v398_agps_generation;
                        if (fallback_generation == 0)
                            fallback_generation = ++g_v398_agps_generation;
                        old_cp_open_or_pending = g_v398_local_data_open ||
                                g_v398_framework_data_open ||
                                g_v398_pending_close;
                        if (old_cp_open_or_pending)
                            g_v398_pending_close = 1;
                        g_v398_local_data_open = 0;
                        g_v398_framework_data_open = 0;
                        g_v398_framework_request_active = 1;
                        pthread_mutex_unlock(&g_state_lock);
                        ALOGI("G401_AGPS_WORKER_FAILURE_CP_RETIRE generation=%u old_cp=%d rc=%d",
                              fallback_generation, old_cp_open_or_pending, defer_rc);
                        if (old_cp_open_or_pending)
                            v398_defer_local_close(fallback_generation);
                        cb(&status);
                    }
                } else if (cb && payload.type == AGPS_TYPE_SUPL &&
                           payload.status == GPS_RELEASE_AGPS_DATA_CONN) {
                    int local_open;
                    int framework_active;
                    int pending_close;
                    int retire_orphan;
                    uint32_t close_generation;

                    pthread_mutex_lock(&g_state_lock);
                    close_generation = ++g_v398_agps_generation;
                    local_open = g_v398_local_data_open;
                    pending_close = g_v398_pending_close;
                    framework_active = g_v398_framework_request_active ||
                                       g_v398_framework_data_open;
                    g_v398_local_data_open = 0;
                    /* G400: a replacement REQUEST already retired the old
                     * CP owner and queued CLOSED, but RELEASE can invalidate
                     * its worker before it executes. Do not lose that close.
                     * A live framework owner must still receive its RELEASE
                     * callback and handle its own CP connection. */
                    retire_orphan = pending_close && !framework_active;
                    if (local_open || retire_orphan) {
                        g_v398_pending_close = 1;
                        g_v398_framework_request_active = 0;
                        g_v398_framework_data_open = 0;
                    }
                    pthread_mutex_unlock(&g_state_lock);

                    if (local_open || retire_orphan) {
                        ALOGI("G400_AGPS_RELEASE_DRAIN_OLD_CP local=%d orphan=%d",
                              local_open, retire_orphan);
                        v398_defer_local_close(close_generation);
                    } else {
                        ALOGI("V398_AGPS_RELEASE_ROUTE local=0 framework=%d",
                              framework_active);
                        if (framework_active)
                            cb(&status);
                    }
                } else if (cb) {
                    cb(&status);
                }
            }
            break;
        case LT013G_CB_AGPS_RIL_REQUEST_SETID:
        case LT013G_CB_AGPS_RIL_REQUEST_REFLOC:
            if (packet->payload_size >= sizeof(struct lt013g_cb_agps_ril_request)) {
                struct lt013g_cb_agps_ril_request payload;
                AGpsRilCallbacks ril_callbacks;
                int ril_callbacks_valid;
                int defer_rc;
                memcpy(&payload, packet->payload, sizeof(payload));
                pthread_mutex_lock(&g_state_lock);
                ril_callbacks = g_agps_ril_callbacks_storage;
                ril_callbacks_valid = g_agps_ril_callbacks_valid;
                pthread_mutex_unlock(&g_state_lock);
                if (!ril_callbacks_valid) {
                    ALOGW("V344_AGPS_RIL_DEFER_SKIP type=%u callbacks_missing=1", packet->type);
                    break;
                }
                defer_rc = defer_agps_ril_callback(packet->type, payload.flags,
                        ril_callbacks.request_setid, ril_callbacks.request_refloc,
                        ril_callbacks.create_thread_cb);
                ALOGI("V344_AGPS_RIL_DEFER type=%u flags=0x%x rc=%d", packet->type, payload.flags, defer_rc);
            }
            break;
        default:
            break;
    }
}

static void receiver_thread(void *unused) {
    (void)unused;
    for (;;) {
        struct lt013g_bridge_packet packet;
        int fd;
        ssize_t received;
        pthread_mutex_lock(&g_state_lock);
        fd = g_socket;
        pthread_mutex_unlock(&g_state_lock);
        if (fd < 0) break;
        do {
            received = recv(fd, &packet, sizeof(packet), MSG_TRUNC);
        } while (received < 0 && errno == EINTR);
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            ALOGW("V307_BRIDGE_RX_EAGAIN fd=%d", fd);
            usleep(10000);
            continue;
        }
        if (received != (ssize_t)sizeof(packet)) {
            const int saved_errno = errno;
            ALOGW("V307_BRIDGE_RX_EXIT fd=%d received=%zd expected=%zu errno=%d (%s)",
                  fd, received, sizeof(packet), saved_errno,
                  received < 0 ? strerror(saved_errno) : "peer-eof-or-short-packet");
            break;
        }
        if (packet.magic != LT013G_BRIDGE_MAGIC || packet.version != LT013G_BRIDGE_VERSION) continue;
        if (packet.payload_size > LT013G_BRIDGE_PAYLOAD_MAX) continue;
        if (packet.type == LT013G_MSG_RESPONSE) {
            pthread_mutex_lock(&g_state_lock);
            if (g_waiting && packet.sequence == g_waiting_sequence) {
                g_waiting_result = packet.result;
                g_waiting = 0;
                pthread_cond_broadcast(&g_response_cond);
            }
            pthread_mutex_unlock(&g_state_lock);
        } else {
            dispatch_callback(&packet);
        }
    }
    pthread_mutex_lock(&g_state_lock);
    if (g_socket >= 0) close(g_socket);
    g_socket = -1;
    g_rx_running = 0;
    pthread_cond_broadcast(&g_supervisor_cond);
    if (g_session_initialized && !g_supervisor_stop) {
        g_reconnect_needed = 1;
        pthread_cond_broadcast(&g_supervisor_cond);
    }
    if (g_waiting) {
        g_waiting_result = -EPIPE;
        g_waiting = 0;
        pthread_cond_broadcast(&g_response_cond);
    }
    pthread_mutex_unlock(&g_state_lock);
    ALOGW("V197 bridge receiver stopped");
}

static int ensure_connected(void) {
    int attempt;
    for (attempt = 0; attempt < 150; ++attempt) {
        int fd;
        pthread_mutex_lock(&g_state_lock);
        if (g_socket >= 0 && g_rx_running) {
            pthread_mutex_unlock(&g_state_lock);
            return 0;
        }
        if (g_supervisor_stop) {
            pthread_mutex_unlock(&g_state_lock);
            return -ECANCELED;
        }
        pthread_mutex_unlock(&g_state_lock);
        fd = connect_once();
        if (fd >= 0) {
            pthread_t thread;
            GpsCallbacks callbacks_copy;
            pthread_mutex_lock(&g_state_lock);
            if (g_supervisor_stop) {
                pthread_mutex_unlock(&g_state_lock);
                close(fd);
                return -ECANCELED;
            }
            g_socket = fd;
            g_rx_running = 1;
            if (g_callbacks_valid) memcpy(&callbacks_copy, &g_callbacks_storage, sizeof(callbacks_copy));
            else memset(&callbacks_copy, 0, sizeof(callbacks_copy));
            pthread_mutex_unlock(&g_state_lock);
            if (!callbacks_copy.create_thread_cb) {
                close(fd);
                pthread_mutex_lock(&g_state_lock);
                g_socket = -1;
                g_rx_running = 0;
                pthread_mutex_unlock(&g_state_lock);
                return -EINVAL;
            }
            thread = callbacks_copy.create_thread_cb("lt013g-gnss-rx", receiver_thread, NULL);
            if ((uintptr_t)thread == 0u) {
                close(fd);
                pthread_mutex_lock(&g_state_lock);
                g_socket = -1;
                g_rx_running = 0;
                pthread_mutex_unlock(&g_state_lock);
                return -EIO;
            }
            ALOGI("connected to legacy GNSS bridge");
            return 0;
        }
        usleep(100000);
    }
    return -ETIMEDOUT;
}

static int send_command_wire_locked(uint32_t type, const void *payload, uint32_t payload_size) {
    struct lt013g_bridge_packet packet;
    struct timespec deadline;
    uint32_t sequence;
    int fd;
    int rc;
    if (payload_size > LT013G_BRIDGE_PAYLOAD_MAX) return -EMSGSIZE;
    rc = ensure_connected();
    if (rc != 0) return rc;
    pthread_mutex_lock(&g_state_lock);
    if (g_socket < 0 || !g_rx_running) {
        pthread_mutex_unlock(&g_state_lock);
        return -ENOTCONN;
    }
    sequence = g_next_sequence++;
    if (g_next_sequence == 0) g_next_sequence = 1;
    g_waiting_sequence = sequence;
    g_waiting_result = -ETIMEDOUT;
    g_waiting = 1;
    fd = g_socket;
    packet_init(&packet, type, sequence);
    packet.payload_size = payload_size;
    if (payload && payload_size) memcpy(packet.payload, payload, payload_size);
    ssize_t written;
    do {
        written = send(fd, &packet, sizeof(packet), MSG_NOSIGNAL);
    } while (written < 0 && errno == EINTR);
    if (written != (ssize_t)sizeof(packet)) {
        const int saved_errno = errno;
        rc = written < 0 ? -saved_errno : -EIO;
        ALOGW("V307_CMD_SEND_FAIL type=%u seq=%u fd=%d written=%zd errno=%d (%s)",
              type, sequence, fd, written, saved_errno,
              written < 0 ? strerror(saved_errno) : "short-send");
        g_waiting = 0;
        g_reconnect_needed = g_session_initialized && !g_supervisor_stop;
        if (g_reconnect_needed) pthread_cond_broadcast(&g_supervisor_cond);
        if (g_socket >= 0) shutdown(g_socket, SHUT_RDWR);
        pthread_mutex_unlock(&g_state_lock);
        return rc;
    }
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += 8;
    while (g_waiting) {
        int wait_rc = pthread_cond_timedwait(&g_response_cond, &g_state_lock, &deadline);
        if (wait_rc != 0) {
            ALOGW("V307_CMD_WAIT_FAIL type=%u seq=%u wait_rc=%d", type, sequence, wait_rc);
            g_waiting = 0;
            g_waiting_result = -wait_rc;
            g_reconnect_needed = g_session_initialized && !g_supervisor_stop;
            if (g_reconnect_needed) pthread_cond_broadcast(&g_supervisor_cond);
            if (g_socket >= 0) shutdown(g_socket, SHUT_RDWR);
            break;
        }
    }
    rc = g_waiting_result;
    ALOGI("V307_CMD_RESULT type=%u seq=%u rc=%d", type, sequence, rc);
    pthread_mutex_unlock(&g_state_lock);
    return rc;
}

static int xtra_inject_wire_locked(const uint8_t *data, uint32_t length) {
    struct lt013g_cmd_xtra_begin begin;
    uint32_t offset = 0;
    int rc;
    if (!data || length == 0 || length > LT013G_XTRA_TOTAL_MAX) return -EINVAL;
    begin.total_length = length;
    rc = send_command_wire_locked(LT013G_CMD_XTRA_BEGIN, &begin, sizeof(begin));
    while (rc == 0 && offset < length) {
        struct lt013g_cmd_xtra_chunk chunk;
        uint32_t n = length - offset;
        uint32_t payload_size;
        if (n > LT013G_XTRA_CHUNK_DATA_MAX) n = LT013G_XTRA_CHUNK_DATA_MAX;
        memset(&chunk, 0, sizeof(chunk));
        chunk.offset = offset;
        chunk.length = n;
        memcpy(chunk.data, data + offset, n);
        payload_size = (uint32_t)offsetof(struct lt013g_cmd_xtra_chunk, data) + n;
        rc = send_command_wire_locked(LT013G_CMD_XTRA_CHUNK, &chunk, payload_size);
        offset += n;
    }
    if (rc == 0) rc = send_command_wire_locked(LT013G_CMD_XTRA_COMMIT, NULL, 0);
    return rc;
}


static int v381_build_dns_seed(
        const struct lt013g_cmd_agps_set_server *server,
        struct lt013g_cmd_dns_seed *seed) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;
    int gai;
    uint32_t i;

    if (!seed) return -EINVAL;
    memset(seed, 0, sizeof(*seed));

    if (!server || !server->hostname[0]) return -EINVAL;

    seed->port = server->port;
    strncpy(seed->hostname, server->hostname, sizeof(seed->hostname) - 1u);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    gai = getaddrinfo(server->hostname, NULL, &hints, &result);
    if (gai != 0) return -EHOSTUNREACH;

    for (it = result;
         it && seed->address_count < LT013G_DNS_MAX_ADDRS;
         it = it->ai_next) {
        const struct sockaddr_in *sin;
        char address[LT013G_DNS_ADDR_MAX];
        int duplicate = 0;

        if (it->ai_family != AF_INET ||
            !it->ai_addr ||
            it->ai_addrlen < (socklen_t)sizeof(*sin))
            continue;

        sin = (const struct sockaddr_in *)it->ai_addr;

        if (!inet_ntop(AF_INET, &sin->sin_addr,
                       address, sizeof(address)))
            continue;

        for (i = 0; i < seed->address_count; ++i) {
            if (strcmp(seed->address[i], address) == 0) {
                duplicate = 1;
                break;
            }
        }

        if (duplicate) continue;

        strncpy(seed->address[seed->address_count],
                address,
                LT013G_DNS_ADDR_MAX - 1u);
        ++seed->address_count;
    }

    freeaddrinfo(result);

    return seed->address_count > 0 ? 0 : -EHOSTUNREACH;
}

static int replay_session_locked(void) {
    struct lt013g_cmd_position_mode mode;
    int initialized;
    int started;
    int mode_valid;
    int xtra_initialized;
    int agps_initialized;
    int agps_ril_initialized;
    int agps_server_valid;
    int agps_network_state_valid;
    int agps_network_avail_valid;
    struct lt013g_cmd_agps_set_server agps_server;
    struct lt013g_cmd_agps_ril_network_state agps_network_state;
    struct lt013g_cmd_agps_ril_network_availability agps_network_avail;
    uint8_t *xtra_copy = NULL;
    uint32_t xtra_length = 0;
    int rc;
    uint32_t generation;
    uint32_t dns_generation;

    pthread_mutex_lock(&g_state_lock);
    initialized = g_session_initialized;
    started = g_session_started;
    mode_valid = g_mode_valid;
    mode = g_mode_payload;
    xtra_initialized = g_xtra_initialized;
    agps_initialized = g_agps_initialized;
    agps_ril_initialized = g_agps_ril_initialized;
    agps_server_valid = g_agps_server_valid; agps_server = g_agps_server_cache;
    dns_generation = g_v379_dns_generation;
    agps_network_state_valid = g_agps_network_state_valid; agps_network_state = g_agps_network_state_cache;
    agps_network_avail_valid = g_agps_network_avail_valid; agps_network_avail = g_agps_network_avail_cache;
    if (g_xtra_cache && g_xtra_cache_length) {
        xtra_length = g_xtra_cache_length;
        xtra_copy = (uint8_t *)malloc(xtra_length);
        if (xtra_copy) memcpy(xtra_copy, g_xtra_cache, xtra_length);
        else xtra_length = 0;
    }
    if (!initialized || g_supervisor_stop) {
        g_reconnect_needed = 0;
        pthread_mutex_unlock(&g_state_lock);
        return 0;
    }
    ++g_effective_generation;
    if (g_effective_generation < 2) g_effective_generation = 2;
    generation = g_effective_generation;
    pthread_mutex_unlock(&g_state_lock);

    rc = send_command_wire_locked(LT013G_CMD_INIT, NULL, 0);
    if (rc == 0 && xtra_initialized) rc = send_command_wire_locked(LT013G_CMD_XTRA_INIT, NULL, 0);
    if (rc == 0 && agps_initialized) rc = send_command_wire_locked(LT013G_CMD_AGPS_INIT, NULL, 0);
    if (rc == 0 && agps_ril_initialized) rc = send_command_wire_locked(LT013G_CMD_AGPS_RIL_INIT, NULL, 0);
    if (rc == 0 && agps_server_valid) {
        struct lt013g_cmd_dns_seed seed;
        int resolve_rc = v381_build_dns_seed(&agps_server, &seed);
        int seed_rc = resolve_rc;
        int server_rc;

        if (resolve_rc == 0)
            seed_rc = send_command_wire_locked(
                    LT013G_CMD_DNS_SEED, &seed, sizeof(seed));

        server_rc = send_command_wire_locked(
                LT013G_CMD_AGPS_SET_SERVER,
                &agps_server,
                sizeof(agps_server));

        if (resolve_rc == 0 && seed_rc == 0 && server_rc == 0) {
            pthread_mutex_lock(&g_state_lock);
            if (dns_generation == g_v379_dns_generation) {
                g_v379_dns_seeded_generation = dns_generation;
                g_agps_server_pending = 0;
            }
            pthread_mutex_unlock(&g_state_lock);
        }

        ALOGI("V381_PROXY_DNS_TRIGGER reason=set_server_replay host=%s port=%d resolve_rc=%d addresses=%u seed_rc=%d set_server_rc=%d",
              agps_server.hostname,
              agps_server.port,
              resolve_rc,
              seed.address_count,
              seed_rc,
              server_rc);

        if (server_rc != 0)
            rc = server_rc;
    }
    if (rc == 0 && agps_network_state_valid) rc = send_command_wire_locked(LT013G_CMD_AGPS_RIL_UPDATE_NETWORK_STATE, &agps_network_state, sizeof(agps_network_state));
    if (rc == 0 && agps_network_avail_valid) rc = send_command_wire_locked(LT013G_CMD_AGPS_RIL_UPDATE_NETWORK_AVAILABILITY, &agps_network_avail, sizeof(agps_network_avail));
    if (rc == 0 && mode_valid) {
        rc = send_command_wire_locked(LT013G_CMD_SET_POSITION_MODE, &mode, sizeof(mode));
    }
    if (rc == 0 && started) rc = send_command_wire_locked(LT013G_CMD_START, NULL, 0);
    if (rc == 0 && started && xtra_initialized && xtra_copy && xtra_length) {
        ALOGI("V345_REPLAY_XTRA_AFTER_START bytes=%u", xtra_length);
        rc = xtra_inject_wire_locked(xtra_copy, xtra_length);
    }
    free(xtra_copy);

    pthread_mutex_lock(&g_state_lock);
    if (rc == 0) {
        g_reconnect_needed = 0;
        ++g_stats.gpsd_hal_recovery_count;
    } else {
        g_reconnect_needed = g_session_initialized && !g_supervisor_stop;
        if (g_reconnect_needed) pthread_cond_broadcast(&g_supervisor_cond);
        if (g_socket >= 0) shutdown(g_socket, SHUT_RDWR);
    }
    pthread_mutex_unlock(&g_state_lock);
    ALOGI("V197 legacy process replay generation=%u rc=%d started=%d mode=%d",
          generation, rc, started, mode_valid);
    return rc;
}

static void supervisor_thread(void *unused) {
    unsigned backoff_ms = 100;
    (void)unused;
    pthread_mutex_lock(&g_state_lock);
    g_supervisor_running = 1;
    pthread_cond_broadcast(&g_supervisor_cond);
    for (;;) {
        int rc;
        while (!g_supervisor_stop &&
               !(g_reconnect_needed && g_session_initialized && g_session_started)) {
            /* Event-driven idle: no 100 ms polling and no automatic legacy
             * process resurrection while framework is not tracking GPS. */
            pthread_cond_wait(&g_supervisor_cond, &g_state_lock);
        }
        if (g_supervisor_stop) break;
        pthread_mutex_unlock(&g_state_lock);

        pthread_mutex_lock(&g_command_lock);
        pthread_mutex_lock(&g_state_lock);
        int replay = g_reconnect_needed && g_session_initialized &&
                g_session_started && !g_supervisor_stop;
        pthread_mutex_unlock(&g_state_lock);
        rc = replay ? replay_session_locked() : 0;
        pthread_mutex_unlock(&g_command_lock);

        pthread_mutex_lock(&g_state_lock);
        if (rc == 0) {
            backoff_ms = 100;
        } else if (!g_supervisor_stop && g_session_started) {
            struct timespec wake;
            clock_gettime(CLOCK_MONOTONIC, &wake);
            wake.tv_sec += backoff_ms / 1000;
            wake.tv_nsec += (long)(backoff_ms % 1000) * 1000000L;
            if (wake.tv_nsec >= 1000000000L) {
                ++wake.tv_sec;
                wake.tv_nsec -= 1000000000L;
            }
            ALOGW("V197 supervisor replay failed rc=%d backoff_ms=%u", rc, backoff_ms);
            (void)pthread_cond_timedwait(&g_supervisor_cond, &g_state_lock, &wake);
            if (backoff_ms < 2000) {
                backoff_ms *= 2;
                if (backoff_ms > 2000) backoff_ms = 2000;
            }
        }
    }
    g_supervisor_running = 0;
    g_supervisor_started = 0;
    pthread_cond_broadcast(&g_supervisor_cond);
    pthread_mutex_unlock(&g_state_lock);
    ALOGI("V197 supervisor stopped");
}

static int start_supervisor(void) {
    GpsCallbacks callbacks_copy;
    pthread_t thread;
    pthread_mutex_lock(&g_state_lock);
    if (g_supervisor_started) {
        pthread_mutex_unlock(&g_state_lock);
        return 0;
    }
    if (!g_callbacks_valid || !g_callbacks_storage.create_thread_cb) {
        pthread_mutex_unlock(&g_state_lock);
        return -EINVAL;
    }
    callbacks_copy = g_callbacks_storage;
    g_supervisor_stop = 0;
    g_supervisor_started = 1;
    g_supervisor_running = 1;
    pthread_mutex_unlock(&g_state_lock);
    thread = callbacks_copy.create_thread_cb("lt013g-gnss-supervisor", supervisor_thread, NULL);
    if ((uintptr_t)thread == 0u) {
        pthread_mutex_lock(&g_state_lock);
        g_supervisor_started = 0;
        g_supervisor_running = 0;
        pthread_mutex_unlock(&g_state_lock);
        return -EIO;
    }
    return 0;
}

static int send_command(uint32_t type, const void *payload, uint32_t payload_size) {
    int rc = 0;
    int need_replay;
    int replay_will_satisfy_start = 0;
    pthread_mutex_lock(&g_command_lock);
    pthread_mutex_lock(&g_state_lock);
    if (!g_callbacks_valid || (g_supervisor_stop && type != LT013G_CMD_CLEANUP)) {
        pthread_mutex_unlock(&g_state_lock);
        pthread_mutex_unlock(&g_command_lock);
        return type == LT013G_CMD_STOP || type == LT013G_CMD_CLEANUP ? 0 : -ENODEV;
    }
    if (type == LT013G_CMD_STOP || type == LT013G_CMD_CLEANUP) {
        g_session_started = 0;
        pthread_cond_broadcast(&g_supervisor_cond);
        if (g_reconnect_needed || g_socket < 0 || !g_rx_running) {
            pthread_mutex_unlock(&g_state_lock);
            pthread_mutex_unlock(&g_command_lock);
            return 0;
        }
    }
    need_replay = g_reconnect_needed && g_session_initialized &&
                  type != LT013G_CMD_INIT && type != LT013G_CMD_CLEANUP &&
                  type != LT013G_CMD_STOP;
    replay_will_satisfy_start = need_replay && type == LT013G_CMD_START &&
                                g_session_started;
    pthread_mutex_unlock(&g_state_lock);
    if (need_replay) rc = replay_session_locked();
    if (rc == 0 && replay_will_satisfy_start) {
        ALOGI("V356_START_DEDUP replay_already_restored_started_session=1");
    } else if (rc == 0) {
        rc = send_command_wire_locked(type, payload, payload_size);
    }
    pthread_mutex_lock(&g_state_lock);
    if (type == LT013G_CMD_START && g_session_initialized && !g_supervisor_stop) {
        if (rc == 0) {
            g_session_started = 1;
        } else if (rc == -EPIPE || rc == -ENOTCONN || rc == -ECONNRESET ||
                   rc == -ETIMEDOUT || rc == -EAGAIN || rc == -EIO) {
            /* START is desired, but transport disappeared.  Wake the event-driven
             * supervisor so it can replay INIT/mode/START against a fresh legacy
             * process without waiting for a framework retry. */
            g_session_started = 1;
            g_reconnect_needed = 1;
            pthread_cond_broadcast(&g_supervisor_cond);
        } else {
            g_session_started = 0;
        }
    }
    if (type == LT013G_CMD_SET_POSITION_MODE && rc == 0 &&
            payload != NULL && payload_size == sizeof(g_mode_payload)) {
        memcpy(&g_mode_payload, payload, sizeof(g_mode_payload));
        g_mode_valid = 1;
    }
    pthread_mutex_unlock(&g_state_lock);
    pthread_mutex_unlock(&g_command_lock);
    return rc;
}

static int v381_seed_then_set_server(
        const struct lt013g_cmd_agps_set_server *server,
        uint32_t generation,
        const char *reason) {
    struct lt013g_cmd_dns_seed seed;
    int resolve_rc;
    int seed_rc;
    int server_rc;
    int current;
    struct in_addr resolved_ipv4;
    int resolved_ipv4_valid = 0;

    memset(&seed, 0, sizeof(seed));
    memset(&resolved_ipv4, 0, sizeof(resolved_ipv4));

    resolve_rc = v381_build_dns_seed(server, &seed);
    seed_rc = resolve_rc;

    if (resolve_rc == 0 && seed.address_count > 0 &&
        inet_pton(AF_INET, seed.address[0], &resolved_ipv4) == 1) {
        resolved_ipv4_valid = 1;
    }

    if (resolve_rc == 0)
        seed_rc = send_command(
                LT013G_CMD_DNS_SEED,
                &seed,
                sizeof(seed));

    /*
     * Keep the pre-v381 behaviour available even if DNS resolution fails:
     * SET_SERVER itself is still sent and its result remains authoritative
     * for the framework callback.
     */
    server_rc = send_command(
            LT013G_CMD_AGPS_SET_SERVER,
            server,
            sizeof(*server));

    pthread_mutex_lock(&g_state_lock);

    current =
        generation == g_v379_dns_generation &&
        g_agps_server_valid &&
        strcmp(server->hostname, g_agps_server_cache.hostname) == 0 &&
        server->port == g_agps_server_cache.port;

    if (current && resolved_ipv4_valid) {
        g_v397_supl_ipv4_net = resolved_ipv4.s_addr;
        g_v397_supl_ipv4_valid = 1;
    }

    if (current &&
        resolve_rc == 0 &&
        seed_rc == 0 &&
        server_rc == 0) {
        g_v379_dns_seeded_generation = generation;
        g_agps_server_pending = 0;
    }

    pthread_mutex_unlock(&g_state_lock);

    if (current && resolved_ipv4_valid) {
        char ipbuf[INET_ADDRSTRLEN];
        const char *iptext = inet_ntop(AF_INET, &resolved_ipv4, ipbuf, sizeof(ipbuf));
        ALOGI("V397_SUPL_IPV4_CACHE valid=1 generation=%u ip=%s",
              generation, iptext ? iptext : "invalid");
    }

    ALOGI("V381_PROXY_DNS_TRIGGER reason=%s generation=%u host=%s port=%d resolve_rc=%d addresses=%u current=%d seed_rc=%d set_server_rc=%d",
          reason ? reason : "unknown",
          generation,
          server->hostname,
          server->port,
          resolve_rc,
          seed.address_count,
          current,
          seed_rc,
          server_rc);

    return server_rc;
}

static void proxy_cleanup(void);

static int proxy_init(GpsCallbacks *callbacks) {
    int rc;
    if (!callbacks || callbacks->size < sizeof(GpsCallbacks) ||
            !callbacks->create_thread_cb) return -EINVAL;
    rc = pthread_once(&g_cond_once, init_conditions);
    if (rc != 0) return -rc;
    if (g_cond_error != 0) return -g_cond_error;
    pthread_mutex_lock(&g_state_lock);
    if (g_callbacks_valid || g_rx_running || g_supervisor_started || g_supervisor_running) {
        pthread_mutex_unlock(&g_state_lock);
        return -EBUSY;
    }
    memcpy(&g_callbacks_storage, callbacks, sizeof(g_callbacks_storage));
    g_callbacks_valid = 1;
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.size = sizeof(g_stats);
    g_session_initialized = 0;
    g_session_started = 0;
    g_mode_valid = 0;
    memset(&g_mode_payload, 0, sizeof(g_mode_payload));
    g_reconnect_needed = 0;
    g_xtra_initialized = 0;
    g_xtra_callbacks_valid = 0;
    memset(&g_xtra_callbacks_storage, 0, sizeof(g_xtra_callbacks_storage));
    free(g_xtra_cache);
    g_xtra_cache = NULL;
    g_xtra_cache_length = 0;
    g_supervisor_started = 0;
    g_supervisor_running = 0;
    g_supervisor_stop = 0;
    g_effective_generation = 1;
    g_v392_delete_all_done = 0;
    pthread_mutex_unlock(&g_state_lock);
    rc = send_command(LT013G_CMD_INIT, NULL, 0);
    if (rc == 0) {
        pthread_mutex_lock(&g_state_lock);
        g_session_initialized = 1;
        pthread_mutex_unlock(&g_state_lock);
        rc = start_supervisor();
    }
    if (rc != 0) {
        proxy_cleanup();
    }
    ALOGI("init rc=%d", rc);
    return rc;
}

static int proxy_start(void) {
    struct lt013g_cmd_agps_set_server server;
    uint32_t dns_generation = 0;
    int dns_needed = 0;
    int retry_server = 0;
    int retry_init = 0;
    int rc;

    memset(&server, 0, sizeof(server));

    pthread_mutex_lock(&g_state_lock);
    if (g_agps_server_valid) {
        server = g_agps_server_cache;
        dns_generation = g_v379_dns_generation;
        dns_needed =
            g_v379_dns_seeded_generation != dns_generation;
    }
    pthread_mutex_unlock(&g_state_lock);

    if (dns_needed)
        (void)v381_seed_then_set_server(
                &server,
                dns_generation,
                "start");

    if (v392_property_enabled("debug.lt013g.gps.v392.cold_reset")) {
        int delete_rc = v392_delete_all_if_requested();
        if (delete_rc != 0) {
            ALOGE("V392_DELETE_AIDING_ALL=FAIL rc=%d", delete_rc);
            return delete_rc;
        }
    }

    rc = send_command(LT013G_CMD_START, NULL, 0);

    if (rc == 0) {
        pthread_mutex_lock(&g_state_lock);

        retry_server =
            g_agps_server_valid &&
            (g_agps_server_pending ||
             g_v379_dns_seeded_generation != g_v379_dns_generation);

        retry_init = g_agps_initialized;

        if (retry_server) {
            server = g_agps_server_cache;
            dns_generation = g_v379_dns_generation;
        }

        pthread_mutex_unlock(&g_state_lock);

        if (retry_server) {
            int init_rc = 0;
            int server_rc;

            if (retry_init)
                init_rc = send_command(
                        LT013G_CMD_AGPS_INIT,
                        NULL,
                        0);

            server_rc = v381_seed_then_set_server(
                    &server,
                    dns_generation,
                    "post_start");

            ALOGI("V363_AGPS_POST_START_RETRY init_rc=%d server_rc=%d host=%s port=%d",
                  init_rc,
                  server_rc,
                  server.hostname,
                  server.port);
        }
    }

    ALOGI("V197 start rc=%d", rc);
    return rc;
}

static int proxy_stop(void) {
    int rc = send_command(LT013G_CMD_STOP, NULL, 0);
    if (rc == -EPIPE || rc == -ENOTCONN || rc == -ECONNRESET) return 0;
    return rc;
}

static void proxy_cleanup(void) {
    int connected;
    struct timespec deadline;
    pthread_mutex_lock(&g_state_lock);
    if (!g_callbacks_valid && !g_rx_running && !g_supervisor_running) {
        pthread_mutex_unlock(&g_state_lock);
        return;
    }
    g_session_started = 0;
    g_session_initialized = 0;
    g_reconnect_needed = 0;
    ++g_v379_dns_generation;
    g_v379_dns_seeded_generation = 0;
    g_v379_dns_inflight_generation = 0;
    g_supervisor_stop = 1;
    pthread_cond_broadcast(&g_supervisor_cond);
    connected = g_socket >= 0 && g_rx_running;
    pthread_mutex_unlock(&g_state_lock);
    if (connected) (void)send_command(LT013G_CMD_CLEANUP, NULL, 0);
    pthread_mutex_lock(&g_state_lock);
    if (g_socket >= 0) shutdown(g_socket, SHUT_RDWR);
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += 2;
    while (g_rx_running || g_supervisor_running) {
        if (pthread_cond_timedwait(&g_supervisor_cond, &g_state_lock, &deadline) != 0)
            break;
    }
    g_callbacks_valid = 0;
    if (!g_supervisor_running) g_supervisor_started = 0;
    memset(&g_callbacks_storage, 0, sizeof(g_callbacks_storage));
    g_xtra_initialized = 0;
    g_xtra_callbacks_valid = 0;
    memset(&g_xtra_callbacks_storage, 0, sizeof(g_xtra_callbacks_storage));
    free(g_xtra_cache);
    g_xtra_cache = NULL;
    g_xtra_cache_length = 0;
    g_agps_initialized = 0;
    g_agps_callbacks_valid = 0;
    memset(&g_agps_callbacks_storage, 0, sizeof(g_agps_callbacks_storage));
    g_agps_ril_initialized = 0;
    g_agps_ril_callbacks_valid = 0;
    memset(&g_agps_ril_callbacks_storage, 0, sizeof(g_agps_ril_callbacks_storage));
    g_agps_server_valid = 0;
    g_agps_server_pending = 0;
    g_v379_dns_seeded_generation = 0;
    g_v379_dns_inflight_generation = 0;
    memset(&g_agps_server_cache, 0, sizeof(g_agps_server_cache));
    ++g_v398_agps_generation;
    g_v398_local_data_open = 0;
    g_v398_pending_close = 0;
    g_v398_framework_request_active = 0;
    g_v398_framework_data_open = 0;
    pthread_mutex_unlock(&g_state_lock);
}

static int proxy_inject_time(GpsUtcTime time, int64_t reference, int uncertainty) {
    struct lt013g_cmd_inject_time payload;
    payload.time_ms = time;
    payload.reference_ms = reference;
    payload.uncertainty_ms = uncertainty;
    return send_command(LT013G_CMD_INJECT_TIME, &payload, sizeof(payload));
}

static int proxy_inject_location(double latitude, double longitude, float accuracy) {
    struct lt013g_cmd_inject_location payload;
    payload.latitude = latitude;
    payload.longitude = longitude;
    payload.accuracy_m = accuracy;
    return send_command(LT013G_CMD_INJECT_LOCATION, &payload, sizeof(payload));
}

static void proxy_delete_aiding(GpsAidingData flags) {
    struct lt013g_cmd_delete_aiding payload;
    payload.flags = flags;
    (void)send_command(LT013G_CMD_DELETE_AIDING, &payload, sizeof(payload));
}

static int proxy_set_position_mode(GpsPositionMode mode, GpsPositionRecurrence recurrence,
                                   uint32_t min_interval, uint32_t preferred_accuracy,
                                   uint32_t preferred_time) {
    struct lt013g_cmd_position_mode payload;
    int rc;
    payload.mode = mode;
    payload.recurrence = recurrence;
    payload.min_interval_ms = min_interval;
    payload.preferred_accuracy_m = preferred_accuracy;
    payload.preferred_time_ms = preferred_time;

    if (v392_property_enabled("debug.lt013g.gps.v392.force_standalone")) {
        payload.mode = GPS_POSITION_MODE_STANDALONE;
        ALOGI("V392_FORCE_STANDALONE requested_mode=%u effective_mode=%u",
              (unsigned)mode, (unsigned)payload.mode);
    }

    ALOGI("V353_POSITION_MODE_PROXY mode=%u recurrence=%u min_interval_ms=%u preferred_accuracy_m=%u preferred_time_ms=%u",
          (unsigned)payload.mode, (unsigned)recurrence, (unsigned)min_interval,
          (unsigned)preferred_accuracy, (unsigned)preferred_time);
    rc = send_command(LT013G_CMD_SET_POSITION_MODE, &payload, sizeof(payload));
    return rc;
}

static int proxy_xtra_init(GpsXtraCallbacks *callbacks) {
    int rc;
    if (!callbacks || !callbacks->download_request_cb || !callbacks->create_thread_cb) return -EINVAL;
    pthread_mutex_lock(&g_state_lock);
    g_xtra_callbacks_storage = *callbacks;
    g_xtra_callbacks_valid = 1;
    pthread_mutex_unlock(&g_state_lock);
    rc = send_command(LT013G_CMD_XTRA_INIT, NULL, 0);
    pthread_mutex_lock(&g_state_lock);
    if (rc == 0) g_xtra_initialized = 1;
    pthread_mutex_unlock(&g_state_lock);
    ALOGI("V341_XTRA_PROXY_INIT rc=%d", rc);
    return rc;
}

static int proxy_xtra_inject(char *data, int length) {
    int rc, started, initialized, need_replay;
    if (!data || length <= 0 ||
        !lt013g_v357_lto_envelope_valid((const uint8_t *)data,
                                       (size_t)length)) {
        ALOGW("G357_LTO_REJECT malformed_or_nonlegacy_file bytes=%d", length);
        return -EINVAL;
    }
    if (!v392_property_enabled("debug.lt013g.gps.v357.lto")) {
        ALOGI("G357_LTO_REJECT opt_in_disabled");
        return -EACCES;
    }
    pthread_mutex_lock(&g_command_lock);
    pthread_mutex_lock(&g_state_lock);
    started = g_session_started;
    initialized = g_xtra_initialized;
    need_replay = g_reconnect_needed;
    pthread_mutex_unlock(&g_state_lock);
    if (!started || !initialized) {
        pthread_mutex_unlock(&g_command_lock);
        ALOGW("G357_LTO_REJECT session_not_started_or_xtra_unavailable");
        return -EAGAIN;
    }
    rc = need_replay ? replay_session_locked() : 0;
    if (rc == 0)
        rc = xtra_inject_wire_locked((const uint8_t *)data,
                                     (uint32_t)length);
    pthread_mutex_unlock(&g_command_lock);
    ALOGI("G357_LTO_BRIDGE_INJECT bytes=%d rc=%d", length, rc);
    return rc;
}

static void proxy_agps_init(AGpsCallbacks *callbacks) {
    int rc;
    if (!callbacks || !callbacks->status_cb || !callbacks->create_thread_cb) return;
    pthread_mutex_lock(&g_state_lock);
    g_agps_callbacks_storage = *callbacks;
    g_agps_callbacks_valid = 1;
    g_agps_initialized = 1;
    pthread_mutex_unlock(&g_state_lock);
    rc = send_command(LT013G_CMD_AGPS_INIT, NULL, 0);
    ALOGI("V363_AGPS_PROXY_INIT armed=1 rc=%d", rc);
    ALOGI("V341_AGPS_PROXY_INIT rc=%d", rc);
}

/* Framework-owned SUPL and private Broadcom SUPL share the same legacy
 * CP connection. Order all their OPEN/CLOSED/FAILED commands on one link
 * gate: framework calls must not race a deferred local CLOSED. */
static int v332_framework_data_conn_open(
        const struct lt013g_cmd_agps_data_conn_open *payload) {
    uint32_t generation;
    int local_open, pending_close, framework_open;
    int rc, close_rc;

    pthread_mutex_lock(&g_v398_link_lock);
    pthread_mutex_lock(&g_state_lock);
    generation = g_v398_agps_generation;
    local_open = g_v398_local_data_open;
    pending_close = g_v398_pending_close;
    framework_open = g_v398_framework_data_open;
    pthread_mutex_unlock(&g_state_lock);

    if (local_open) {
        ALOGI("V332_FRAMEWORK_OPEN_SUPPRESSED local=1");
        pthread_mutex_unlock(&g_v398_link_lock);
        return 0;
    }
    /* A duplicate framework OPEN (e.g. APN/network handover) must
     * retire its previous CP connection instead of opening over it.
     * The pending flag also carries an unclosed prior local session. */
    if (pending_close || framework_open) {
        close_rc = send_command(LT013G_CMD_AGPS_DATA_CONN_CLOSED, NULL, 0);
        if (close_rc != 0) {
            pthread_mutex_lock(&g_state_lock);
            g_v398_pending_close = 1;
            pthread_mutex_unlock(&g_state_lock);
            ALOGW("V333_FRAMEWORK_OPEN_BLOCKED old_close_rc=%d", close_rc);
            pthread_mutex_unlock(&g_v398_link_lock);
            return close_rc;
        }
        pthread_mutex_lock(&g_state_lock);
        g_v398_pending_close = 0;
        if (generation == g_v398_agps_generation)
            g_v398_framework_data_open = 0;
        pthread_mutex_unlock(&g_state_lock);
    }

    rc = send_command(LT013G_CMD_AGPS_DATA_CONN_OPEN,
                      payload, sizeof(*payload));
    pthread_mutex_lock(&g_state_lock);
    if (generation == g_v398_agps_generation) {
        /* A failed CP OPEN must never look like a live SUPL session. */
        g_v398_framework_data_open = rc == 0;
        if (rc != 0)
            g_v398_framework_request_active = 0;
    } else if (rc == 0) {
        /* A newer REQUEST/RELEASE arrived while the OPEN was in flight. */
        g_v398_pending_close = 1;
    }
    local_open = generation == g_v398_agps_generation;
    pthread_mutex_unlock(&g_state_lock);

    if (!local_open && rc == 0) {
        close_rc = send_command(LT013G_CMD_AGPS_DATA_CONN_CLOSED, NULL, 0);
        if (close_rc == 0) {
            pthread_mutex_lock(&g_state_lock);
            g_v398_pending_close = 0;
            pthread_mutex_unlock(&g_state_lock);
        }
        rc = -ECANCELED;
    }
    ALOGI("V332_FRAMEWORK_DATA_CONN_OPEN generation=%u iptype=%u rc=%d",
          generation, payload->apn_ip_type, rc);
    pthread_mutex_unlock(&g_v398_link_lock);
    return rc;
}

static int proxy_agps_data_conn_open(const char *apn) {
    struct lt013g_cmd_agps_data_conn_open payload;
    memset(&payload, 0, sizeof(payload));
    payload.apn_ip_type = APN_IP_IPV4V6;
    if (apn) strncpy(payload.apn, apn, sizeof(payload.apn) - 1u);
    return v332_framework_data_conn_open(&payload);
}

static int proxy_agps_data_conn_closed(void) {
    int local_open, framework_open;
    int rc;

    pthread_mutex_lock(&g_v398_link_lock);
    pthread_mutex_lock(&g_state_lock);
    local_open = g_v398_local_data_open;
    framework_open = g_v398_framework_data_open;
    g_v398_framework_request_active = 0;
    g_v398_framework_data_open = 0;
    pthread_mutex_unlock(&g_state_lock);

    if (local_open) {
        ALOGI("V332_FRAMEWORK_DATA_CONN_CLOSED_SUPPRESSED local=1");
        pthread_mutex_unlock(&g_v398_link_lock);
        return 0;
    }
    rc = send_command(LT013G_CMD_AGPS_DATA_CONN_CLOSED, NULL, 0);
    pthread_mutex_lock(&g_state_lock);
    if (rc == 0)
        g_v398_pending_close = 0;
    else if (framework_open)
        g_v398_pending_close = 1;
    pthread_mutex_unlock(&g_state_lock);
    ALOGI("V333_FRAMEWORK_DATA_CONN_CLOSED rc=%d", rc);
    pthread_mutex_unlock(&g_v398_link_lock);
    return rc;
}

static int proxy_agps_data_conn_failed(void) {
    int local_open, framework_open;
    int rc;

    pthread_mutex_lock(&g_v398_link_lock);
    pthread_mutex_lock(&g_state_lock);
    local_open = g_v398_local_data_open;
    framework_open = g_v398_framework_data_open;
    g_v398_framework_request_active = 0;
    g_v398_framework_data_open = 0;
    pthread_mutex_unlock(&g_state_lock);

    if (local_open) {
        ALOGI("V332_FRAMEWORK_DATA_CONN_FAILED_SUPPRESSED local=1");
        pthread_mutex_unlock(&g_v398_link_lock);
        return 0;
    }
    rc = send_command(LT013G_CMD_AGPS_DATA_CONN_FAILED, NULL, 0);
    if (rc != 0 && framework_open) {
        /* If legacy CP rejects FAILED, assume it might still be open.
         * Drain CLOSED before allowing any replacement SUPL OPEN. */
        pthread_mutex_lock(&g_state_lock);
        g_v398_pending_close = 1;
        pthread_mutex_unlock(&g_state_lock);
    }
    ALOGI("V333_FRAMEWORK_DATA_CONN_FAILED rc=%d", rc);
    pthread_mutex_unlock(&g_v398_link_lock);
    return rc;
}
static int proxy_agps_set_server(
        AGpsType type,
        const char *hostname,
        int port) {
    struct lt013g_cmd_agps_set_server payload;
    uint32_t dns_generation;
    int rc;

    memset(&payload, 0, sizeof(payload));
    payload.type = type;
    payload.port = port;

    if (hostname)
        strncpy(
            payload.hostname,
            hostname,
            sizeof(payload.hostname) - 1u);

    pthread_mutex_lock(&g_state_lock);

    g_agps_server_cache = payload;
    g_agps_server_valid = 1;
    g_agps_server_pending = 1;

    dns_generation = ++g_v379_dns_generation;
    if (dns_generation == 0)
        dns_generation = ++g_v379_dns_generation;

    g_v379_dns_seeded_generation = 0;
    g_v379_dns_inflight_generation = 0;
    g_v397_supl_ipv4_net = 0;
    g_v397_supl_ipv4_valid = 0;

    pthread_mutex_unlock(&g_state_lock);

    rc = v381_seed_then_set_server(
            &payload,
            dns_generation,
            "set_server");

    ALOGI("V363_AGPS_PROXY_SET_SERVER cached=1 type=%u host=%s port=%d rc=%d",
          type,
          payload.hostname,
          port,
          rc);

    ALOGI("V341_AGPS_PROXY_SET_SERVER type=%u host=%s port=%d rc=%d",
          type,
          payload.hostname,
          port,
          rc);

    return rc;
}

static int proxy_agps_data_conn_open_with_apn_ip_type(
        const char *apn, ApnIpType apn_ip_type) {
    struct lt013g_cmd_agps_data_conn_open payload;
    memset(&payload, 0, sizeof(payload));
    payload.apn_ip_type = apn_ip_type;
    if (apn) strncpy(payload.apn, apn, sizeof(payload.apn) - 1u);
    return v332_framework_data_conn_open(&payload);
}
static const AGpsInterface g_agps_interface __attribute__((unused)) = {
    .size = sizeof(AGpsInterface), .init = proxy_agps_init,
    .data_conn_open = proxy_agps_data_conn_open, .data_conn_closed = proxy_agps_data_conn_closed,
    .data_conn_failed = proxy_agps_data_conn_failed, .set_server = proxy_agps_set_server,
    .data_conn_open_with_apn_ip_type = proxy_agps_data_conn_open_with_apn_ip_type,
};

static void proxy_agps_ril_init(AGpsRilCallbacks *callbacks) {
    int rc;
    int state_valid;
    int avail_valid;
    int avail_synthetic;
    int state_rc = 0;
    int avail_rc = 0;
    struct lt013g_cmd_agps_ril_network_state state;
    struct lt013g_cmd_agps_ril_network_availability avail;
    if (!callbacks || !callbacks->request_setid || !callbacks->request_refloc || !callbacks->create_thread_cb) return;
    pthread_mutex_lock(&g_state_lock);
    g_agps_ril_callbacks_storage = *callbacks;
    g_agps_ril_callbacks_valid = 1;
    g_agps_ril_initialized = 1;
    state_valid = g_agps_network_state_valid;
    state = g_agps_network_state_cache;
    avail_valid = g_agps_network_avail_valid;
    avail = g_agps_network_avail_cache;
    if (state_valid && !avail_valid) {
        memset(&avail, 0, sizeof(avail));
        avail.available = state.connected ? 1 : 0;
        strncpy(avail.apn, "dummy-apn", sizeof(avail.apn) - 1u);
        g_agps_network_avail_cache = avail;
        g_agps_network_avail_valid = 1;
        g_agps_network_avail_synthetic = 1;
        avail_valid = 1;
    }
    avail_synthetic = g_agps_network_avail_synthetic;
    pthread_mutex_unlock(&g_state_lock);
    rc = send_command(LT013G_CMD_AGPS_RIL_INIT, NULL, 0);
    if (rc == 0 && state_valid)
        state_rc = send_command(LT013G_CMD_AGPS_RIL_UPDATE_NETWORK_STATE, &state, sizeof(state));
    if (rc == 0 && state_rc == 0 && avail_valid)
        avail_rc = send_command(LT013G_CMD_AGPS_RIL_UPDATE_NETWORK_AVAILABILITY, &avail, sizeof(avail));
    ALOGI("V372_AGPS_RIL_PROXY_NETWORK_REPLAY init_rc=%d state_valid=%d state_rc=%d avail_valid=%d avail_rc=%d",
          rc, state_valid, state_rc, avail_valid, avail_rc);
    ALOGI("V373_AGPS_RIL_API25_NETWORK_CONTRACT state_valid=%d avail_valid=%d synthetic=%d available=%d apn=%s state_rc=%d avail_rc=%d",
          state_valid, avail_valid, avail_synthetic, avail_valid ? avail.available : 0,
          avail_valid ? avail.apn : "", state_rc, avail_rc);
    ALOGI("V367_AGPS_RIL_PROXY_INIT armed=1 rc=%d", rc);
    ALOGI("V341_AGPS_RIL_PROXY_INIT rc=%d", rc);
}
static void proxy_agps_ril_set_ref_location(const AGpsRefLocation *location, size_t size) {
    struct lt013g_cmd_agps_ril_ref_location payload;
    if (!location || size < sizeof(*location)) return;
    ALOGI("V363_AGPS_RIL_PROXY_SET_REF_LOCATION type=%u size=%zu", (unsigned)location->type, size);
    memset(&payload, 0, sizeof(payload)); payload.type = location->type;
    if ((uint32_t)location->type == LT013G_AGPS_REFLOC_MAC) memcpy(payload.mac, location->u.mac.mac, sizeof(payload.mac));
    else {
        payload.cell_type = location->u.cellID.type;
        payload.mcc = location->u.cellID.mcc;
        payload.mnc = location->u.cellID.mnc;
        payload.lac = location->u.cellID.lac;
        payload.cid = location->u.cellID.cid;
        /* Android 11's generic AGpsRefLocation has LTE tac/pcid fields, while
         * the Samsung API25 ABI used by this Broadcom stack has a 3G PSC slot.
         * Do not forward potentially uninitialized modern-only fields. */
        payload.tac = 0;
        payload.pcid = 0;
    }
    ALOGI("V367_AGPS_RIL_PROXY_REFLOC_SANITIZED type=%u modern_size=%zu", (unsigned)location->type, size);
    (void)send_command(LT013G_CMD_AGPS_RIL_SET_REF_LOCATION, &payload, sizeof(payload));
}
static void proxy_agps_ril_set_set_id(AGpsSetIDType type, const char *setid) {
    struct lt013g_cmd_agps_ril_set_id payload; memset(&payload, 0, sizeof(payload)); payload.type = type;
    ALOGI("V363_AGPS_RIL_PROXY_SET_ID type=%u present=%d", (unsigned)type, setid && setid[0]);
    if (setid) strncpy(payload.setid, setid, sizeof(payload.setid) - 1u);
    (void)send_command(LT013G_CMD_AGPS_RIL_SET_SET_ID, &payload, sizeof(payload));
}
static void proxy_agps_ril_ni_message(uint8_t *msg, size_t len) { (void)msg; ALOGW("V341_AGPS_RIL_NI_MESSAGE unsupported_len=%zu", len); }
static void proxy_agps_ril_update_network_state(int connected, int type, int roaming, const char *extra_info) {
    struct lt013g_cmd_agps_ril_network_state payload;
    struct lt013g_cmd_agps_ril_network_availability avail;
    int rc;
    int avail_rc = 0;
    int send_synthetic = 0;
    (void)extra_info;
    if (type == 1 && roaming) {
        ALOGI("V373_AGPS_RIL_WIFI_ROAMING_NORMALIZE input=1 output=0");
        roaming = 0;
    }
    payload.connected = connected; payload.type = type; payload.roaming = roaming;
    pthread_mutex_lock(&g_state_lock);
    g_agps_network_state_cache = payload;
    g_agps_network_state_valid = 1;
    if (g_agps_network_avail_synthetic) {
        g_agps_network_avail_cache.available = connected ? 1 : 0;
        avail = g_agps_network_avail_cache;
        send_synthetic = g_agps_ril_initialized;
    }
    pthread_mutex_unlock(&g_state_lock);
    rc = send_command(LT013G_CMD_AGPS_RIL_UPDATE_NETWORK_STATE, &payload, sizeof(payload));
    if (send_synthetic && rc == 0)
        avail_rc = send_command(LT013G_CMD_AGPS_RIL_UPDATE_NETWORK_AVAILABILITY, &avail, sizeof(avail));
    if (connected) {
        struct lt013g_cmd_agps_set_server dns_server;
        uint32_t dns_generation;
        int dns_needed;

        pthread_mutex_lock(&g_state_lock);

        dns_server = g_agps_server_cache;
        dns_generation = g_v379_dns_generation;

        dns_needed =
            g_agps_server_valid &&
            g_v379_dns_seeded_generation != dns_generation;

        pthread_mutex_unlock(&g_state_lock);

        if (dns_needed)
            (void)v381_seed_then_set_server(
                    &dns_server,
                    dns_generation,
                    "network_up");
    }
    ALOGI("V373_AGPS_RIL_NETWORK_STATE_COMPAT connected=%d type=%d roaming=%d synthetic_avail=%d avail_rc=%d",
          connected, type, roaming, send_synthetic, avail_rc);
    ALOGI("V367_AGPS_RIL_PROXY_NETWORK_STATE cached=1 connected=%d type=%d roaming=%d rc=%d", connected, type, roaming, rc);
    ALOGI("V341_AGPS_RIL_PROXY_NETWORK_STATE connected=%d type=%d roaming=%d rc=%d", connected, type, roaming, rc);
}
static void proxy_agps_ril_update_network_availability(int available, const char *apn) {
    struct lt013g_cmd_agps_ril_network_availability payload; int rc; memset(&payload, 0, sizeof(payload)); payload.available = available;
    if (apn) strncpy(payload.apn, apn, sizeof(payload.apn) - 1u);
    pthread_mutex_lock(&g_state_lock);
    g_agps_network_avail_cache = payload;
    g_agps_network_avail_valid = 1;
    g_agps_network_avail_synthetic = 0;
    pthread_mutex_unlock(&g_state_lock);
    rc = send_command(LT013G_CMD_AGPS_RIL_UPDATE_NETWORK_AVAILABILITY, &payload, sizeof(payload));
    ALOGI("V367_AGPS_RIL_PROXY_NETWORK_AVAIL cached=1 available=%d apn=%s rc=%d", available, payload.apn, rc);
    ALOGI("V341_AGPS_RIL_PROXY_NETWORK_AVAIL available=%d apn=%s rc=%d", available, payload.apn, rc);
}
static const AGpsRilInterface g_agps_ril_interface __attribute__((unused)) = {
    .size = sizeof(AGpsRilInterface), .init = proxy_agps_ril_init,
    .set_ref_location = proxy_agps_ril_set_ref_location, .set_set_id = proxy_agps_ril_set_set_id,
    .ni_message = proxy_agps_ril_ni_message, .update_network_state = proxy_agps_ril_update_network_state,
    .update_network_availability = proxy_agps_ril_update_network_availability,
};

static const GpsXtraInterface g_xtra_interface __attribute__((unused)) = {
    .size = sizeof(GpsXtraInterface),
    .init = proxy_xtra_init,
    .inject_xtra_data = proxy_xtra_inject,
};

static const void *proxy_get_extension(const char *name) {
    if (!name) return NULL;
    if (strcmp(name, GPS_XTRA_INTERFACE) == 0) {
        ALOGI("V363_EXTENSION_EXPOSE name=%s", name);
        return &g_xtra_interface;
    }
    if (strcmp(name, AGPS_INTERFACE) == 0) {
        ALOGI("V363_EXTENSION_EXPOSE name=%s", name);
        return &g_agps_interface;
    }
    if (strcmp(name, "agps_ril") == 0) {
        ALOGI("V363_EXTENSION_EXPOSE name=%s", name);
        return &g_agps_ril_interface;
    }
    ALOGI("V363_EXTENSION_MASK name=%s", name);
    return NULL;
}

static const GpsInterface g_interface = {
    .size = sizeof(GpsInterface),
    .init = proxy_init,
    .start = proxy_start,
    .stop = proxy_stop,
    .cleanup = proxy_cleanup,
    .inject_time = proxy_inject_time,
    .inject_location = proxy_inject_location,
    .delete_aiding_data = proxy_delete_aiding,
    .set_position_mode = proxy_set_position_mode,
    .get_extension = proxy_get_extension,
};

__attribute__((visibility("default")))
const GpsInterface *gps_get_hardware_interface(void) { return &g_interface; }

__attribute__((visibility("default")))
int lt013g_gps_bridge_get_stats(struct lt013g_bridge_stats *stats) {
    if (!stats || stats->size < sizeof(*stats)) return -EINVAL;
    pthread_mutex_lock(&g_state_lock);
    memcpy(stats, &g_stats, sizeof(*stats));
    pthread_mutex_unlock(&g_state_lock);
    return 0;
}

static const GpsInterface *device_get_interface(struct gps_device_t *device) {
    (void)device;
    return &g_interface;
}

static int device_close(struct hw_device_t *device) {
    free(device);
    return 0;
}

static int module_open(const struct hw_module_t *module, const char *name,
                       struct hw_device_t **device) {
    struct gps_device_t *gps_device;
    if (!device || !name || strcmp(name, GPS_HARDWARE_MODULE_ID) != 0) return -EINVAL;
    gps_device = (struct gps_device_t *)calloc(1, sizeof(*gps_device));
    if (!gps_device) return -ENOMEM;
    gps_device->common.tag = HARDWARE_DEVICE_TAG;
    gps_device->common.version = 0;
    gps_device->common.module = (struct hw_module_t *)module;
    gps_device->common.close = device_close;
    gps_device->get_gps_interface = device_get_interface;
    *device = &gps_device->common;
    return 0;
}

static struct hw_module_methods_t g_module_methods = {
    .open = module_open,
};

__attribute__((visibility("default")))
struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = 1,
    .hal_api_version = HARDWARE_HAL_API_VERSION,
    .id = GPS_HARDWARE_MODULE_ID,
    .name = "SM-T311 stock KitKat GNSS bridge V197",
    .author = "OpenAI / Pawel Jarczak",
    .methods = &g_module_methods,
};
