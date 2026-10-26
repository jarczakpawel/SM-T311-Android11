#ifndef LT013G_GNSS_BRIDGE_PROTOCOL_H
#define LT013G_GNSS_BRIDGE_PROTOCOL_H

#include <stdint.h>

#define LT013G_BRIDGE_MAGIC 0x47505342u
#define LT013G_BRIDGE_VERSION 4u
#define LT013G_BRIDGE_SOCKET "lt013g_gnss_bridge_v290"
#define LT013G_BRIDGE_PAYLOAD_MAX 2048u
#define LT013G_BRIDGE_MAX_SVS 32u
#define LT013G_BRIDGE_MAX_GNSS_SVS 64u
#define LT013G_BRIDGE_NMEA_MAX 960u

enum lt013g_bridge_type {
    LT013G_MSG_RESPONSE = 1,
    LT013G_CMD_PING = 10,
    LT013G_CMD_INIT = 11,
    LT013G_CMD_START = 12,
    LT013G_CMD_STOP = 13,
    LT013G_CMD_CLEANUP = 14,
    LT013G_CMD_INJECT_TIME = 15,
    LT013G_CMD_INJECT_LOCATION = 16,
    LT013G_CMD_DELETE_AIDING = 17,
    LT013G_CMD_SET_POSITION_MODE = 18,
    LT013G_CMD_XTRA_INIT = 19,
    LT013G_CMD_XTRA_BEGIN = 20,
    LT013G_CMD_XTRA_CHUNK = 21,
    LT013G_CMD_XTRA_COMMIT = 22,
    LT013G_CMD_AGPS_INIT = 23,
    LT013G_CMD_AGPS_DATA_CONN_OPEN = 24,
    LT013G_CMD_AGPS_DATA_CONN_CLOSED = 25,
    LT013G_CMD_AGPS_DATA_CONN_FAILED = 26,
    LT013G_CMD_AGPS_SET_SERVER = 27,
    LT013G_CMD_AGPS_RIL_INIT = 28,
    LT013G_CMD_AGPS_RIL_SET_REF_LOCATION = 29,
    LT013G_CMD_AGPS_RIL_SET_SET_ID = 30,
    LT013G_CMD_AGPS_RIL_UPDATE_NETWORK_STATE = 31,
    LT013G_CMD_AGPS_RIL_UPDATE_NETWORK_AVAILABILITY = 32,
    LT013G_CMD_DNS_SEED = 33,
    LT013G_CB_LOCATION = 100,
    LT013G_CB_STATUS = 101,
    LT013G_CB_SV = 102,
    LT013G_CB_NMEA = 103,
    LT013G_CB_CAPABILITIES = 104,
    LT013G_CB_WAKELOCK_ACQUIRE = 105,
    LT013G_CB_WAKELOCK_RELEASE = 106,
    LT013G_CB_REQUEST_UTC = 107,
    LT013G_CB_GPSD_EVENT = 108,
    LT013G_CB_GNSS_SV = 109,
    LT013G_CB_SYSTEM_INFO = 110,
    LT013G_CB_LEGACY_EXTENSIONS = 111,
    LT013G_CB_XTRA_DOWNLOAD_REQUEST = 112,
    LT013G_CB_AGPS_STATUS = 113,
    LT013G_CB_AGPS_RIL_REQUEST_SETID = 114,
    LT013G_CB_AGPS_RIL_REQUEST_REFLOC = 115,
};

#pragma pack(push, 1)
struct lt013g_bridge_packet {
    uint32_t magic;
    uint32_t version;
    uint32_t type;
    uint32_t sequence;
    int32_t result;
    uint32_t payload_size;
    uint8_t payload[LT013G_BRIDGE_PAYLOAD_MAX];
};

struct lt013g_cmd_inject_time {
    int64_t time_ms;
    int64_t reference_ms;
    int32_t uncertainty_ms;
};

struct lt013g_cmd_inject_location {
    double latitude;
    double longitude;
    float accuracy_m;
};

struct lt013g_cmd_delete_aiding {
    uint16_t flags;
};

struct lt013g_cmd_position_mode {
    uint32_t mode;
    uint32_t recurrence;
    uint32_t min_interval_ms;
    uint32_t preferred_accuracy_m;
    uint32_t preferred_time_ms;
};

#define LT013G_XTRA_CHUNK_DATA_MAX 1900u
#define LT013G_XTRA_TOTAL_MAX (512u * 1024u)

struct lt013g_cmd_xtra_begin { uint32_t total_length; };
struct lt013g_cmd_xtra_chunk {
    uint32_t offset;
    uint32_t length;
    uint8_t data[LT013G_XTRA_CHUNK_DATA_MAX];
};

#define LT013G_AGPS_STRING_MAX 256u
struct lt013g_cmd_agps_data_conn_open {
    uint16_t apn_ip_type;
    char apn[LT013G_AGPS_STRING_MAX];
};
struct lt013g_cmd_agps_set_server {
    uint16_t type;
    int32_t port;
    char hostname[LT013G_AGPS_STRING_MAX];
};
struct lt013g_cmd_agps_ril_ref_location {
    uint16_t type;
    uint16_t cell_type;
    uint16_t mcc;
    uint16_t mnc;
    uint16_t lac;
    uint16_t tac;
    uint16_t pcid;
    uint32_t cid;
    uint8_t mac[6];
};
struct lt013g_cmd_agps_ril_set_id {
    uint16_t type;
    char setid[LT013G_AGPS_STRING_MAX];
};
struct lt013g_cmd_agps_ril_network_state {
    int32_t connected;
    int32_t type;
    int32_t roaming;
};
struct lt013g_cmd_agps_ril_network_availability {
    int32_t available;
    char apn[LT013G_AGPS_STRING_MAX];
};
#define LT013G_DNS_MAX_ADDRS 4u
#define LT013G_DNS_ADDR_MAX 16u
struct lt013g_cmd_dns_seed {
    int32_t port;
    uint32_t address_count;
    char hostname[LT013G_AGPS_STRING_MAX];
    char address[LT013G_DNS_MAX_ADDRS][LT013G_DNS_ADDR_MAX];
};
struct lt013g_cb_agps_status {
    uint16_t type;
    uint16_t status;
    uint32_t ipaddr;
    uint16_t family;
    uint8_t address[16];
};
struct lt013g_cb_agps_ril_request { uint32_t flags; };

struct lt013g_cb_location {
    uint16_t flags;
    double latitude;
    double longitude;
    double altitude;
    float speed;
    float bearing;
    float accuracy;
    int64_t timestamp_ms;
};

struct lt013g_cb_status {
    uint16_t status;
};

struct lt013g_cb_sv_item {
    int32_t prn;
    float snr;
    float elevation;
    float azimuth;
};

struct lt013g_cb_sv {
    int32_t num_svs;
    uint32_t ephemeris_mask;
    uint32_t almanac_mask;
    uint32_t used_in_fix_mask;
    struct lt013g_cb_sv_item sv[LT013G_BRIDGE_MAX_SVS];
};

struct lt013g_cb_gnss_sv_item {
    int16_t svid;
    uint8_t constellation;
    uint8_t flags;
    float c_n0_dbhz;
    float elevation;
    float azimuth;
};

struct lt013g_cb_gnss_sv {
    int32_t num_svs;
    struct lt013g_cb_gnss_sv_item sv[LT013G_BRIDGE_MAX_GNSS_SVS];
};

struct lt013g_cb_system_info {
    uint16_t year_of_hw;
};

struct lt013g_cb_legacy_extensions {
    uint32_t mask;
};

struct lt013g_cb_nmea {
    int64_t timestamp_ms;
    uint32_t length;
    char data[LT013G_BRIDGE_NMEA_MAX];
};

struct lt013g_cb_capabilities {
    uint32_t capabilities;
};

enum lt013g_gpsd_event_type {
    LT013G_GPSD_EVENT_SPAWN = 1,
    LT013G_GPSD_EVENT_EXIT = 2,
    LT013G_GPSD_EVENT_HAL_RECOVERY = 3,
};

struct lt013g_cb_gpsd_event {
    uint32_t event;
    uint32_t generation;
    int32_t status;
    uint32_t expected;
};

struct lt013g_bridge_stats {
    uint32_t size;
    uint32_t gpsd_spawn_count;
    uint32_t gpsd_unexpected_exit_count;
    uint32_t gpsd_expected_exit_count;
    uint32_t gpsd_last_generation;
    uint32_t gpsd_hal_recovery_count;
    uint32_t nmea_count;
    uint32_t nmea_after_restart;
    uint32_t last_nmea_generation;
};
#pragma pack(pop)

#endif
