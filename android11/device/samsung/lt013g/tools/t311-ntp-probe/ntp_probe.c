/*
 * Diagnostic-only, zero-setting-write, zero-location-data SNTP transport probe
 * for Samsung SM-T311 Android11. Sends exactly one 48-byte SNTP client request
 * per reachable address, at most 2 names supplied by caller separately.
 * Never changes the system clock, GPS/CP/EFS, GNSS service, network settings
 * or requests a location fix. No IP addresses/APN/device IDs are logged.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define NTP_BYTES 48u
#define NTP_UNIX_OFFSET 2208988800u

enum verdict {
    NTP_OK = 0, NTP_BAD_LENGTH, NTP_BAD_MODE, NTP_BAD_LEAP,
    NTP_BAD_STRATUM, NTP_BAD_ORIGIN, NTP_BAD_CLOCK
};

static enum verdict validate_response(const uint8_t *reply, size_t len,
                                      const uint8_t request[NTP_BYTES]) {
    uint32_t secs;
    if (!reply || len < NTP_BYTES) return NTP_BAD_LENGTH;
    if ((reply[0] & 0x07u) != 4u || ((reply[0] >> 3) & 0x07u) < 3u)
        return NTP_BAD_MODE;
    if ((reply[0] >> 6) == 3u) return NTP_BAD_LEAP;
    if (reply[1] == 0u || reply[1] > 15u) return NTP_BAD_STRATUM;
    if (memcmp(reply + 24, request + 40, 8) != 0) return NTP_BAD_ORIGIN;
    secs = (uint32_t)reply[40] << 24 | (uint32_t)reply[41] << 16 |
           (uint32_t)reply[42] << 8 | (uint32_t)reply[43];
    if (secs < NTP_UNIX_OFFSET + 1577836800u) return NTP_BAD_CLOCK;
    return NTP_OK;
}

static void make_request(uint8_t out[NTP_BYTES]) {
    struct timespec realtime;
    uint64_t ntpsecs, fraction;
    unsigned i;
    memset(out, 0, NTP_BYTES);
    out[0] = 0x23u; /* leap=0, version 4, client mode 3 */
    clock_gettime(CLOCK_REALTIME, &realtime);
    ntpsecs = (uint64_t)realtime.tv_sec + (uint64_t)NTP_UNIX_OFFSET;
    fraction = ((uint64_t)realtime.tv_nsec << 32) / 1000000000ULL;
    for (i = 0; i < 4; ++i) {
        out[40u + i] = (uint8_t)(ntpsecs >> (24u - i * 8u));
        out[44u + i] = (uint8_t)(fraction >> (24u - i * 8u));
    }
}

static int selftest(void) {
    uint8_t query[NTP_BYTES], reply[NTP_BYTES];
    make_request(query);
    memset(reply, 0, sizeof(reply));
    reply[0] = 0x24u;
    reply[1] = 2u;
    memcpy(reply + 24, query + 40, 8);
    memcpy(reply + 40, query + 40, 8);
    if (validate_response(reply, sizeof(reply), query) != NTP_OK) return 1;
    if (validate_response(reply, 47, query) != NTP_BAD_LENGTH) return 2;
    reply[0] = 0x23u;
    if (validate_response(reply, 48, query) != NTP_BAD_MODE) return 3;
    reply[0] = 0xe4u;
    if (validate_response(reply, 48, query) != NTP_BAD_LEAP) return 4;
    reply[0] = 0x24u;
    reply[1] = 0u;
    if (validate_response(reply, 48, query) != NTP_BAD_STRATUM) return 5;
    reply[1] = 2u;
    reply[24] ^= 1u;
    if (validate_response(reply, 48, query) != NTP_BAD_ORIGIN) return 6;
    reply[24] ^= 1u;
    memset(reply + 40, 0, 4);
    if (validate_response(reply, 48, query) != NTP_BAD_CLOCK) return 7;
    puts("SNTP_SELFTEST_PASS 7_cases no_network no_device_change");
    return 0;
}

static int probe(const char *host) {
    struct addrinfo hints, *res = NULL, *cur;
    int gai, attempts = 0, transport_errors = 0, invalid = 0;
    uint8_t query[NTP_BYTES], reply[256];
    make_request(query);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    gai = getaddrinfo(host, "123", &hints, &res);
    if (gai != 0) {
        puts("NTP_DNS_FAILED");
        return 2;
    }
    for (cur = res; cur && attempts < 4; cur = cur->ai_next) {
        int fd;
        ssize_t n;
        struct timeval tv = {.tv_sec = 3, .tv_usec = 0};
        struct timespec t0, t1;
        enum verdict v;
        if (cur->ai_family != AF_INET && cur->ai_family != AF_INET6) continue;
        attempts++;
        fd = socket(cur->ai_family, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
        if (fd < 0) { transport_errors++; continue; }
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        if (connect(fd, cur->ai_addr, cur->ai_addrlen) < 0) {
            transport_errors++; close(fd); continue;
        }
        clock_gettime(CLOCK_MONOTONIC, &t0);
        if (send(fd, query, sizeof(query), 0) != (ssize_t)sizeof(query)) {
            transport_errors++; close(fd); continue;
        }
        n = recv(fd, reply, sizeof(reply), 0);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        close(fd);
        if (n < 0) { transport_errors++; continue; }
        v = validate_response(reply, (size_t)n, query);
        if (v != NTP_OK) { invalid++; continue; }
        printf("NTP_VALID_REPLY family=%s rtt_ms=%lld\n",
               cur->ai_family == AF_INET ? "ipv4" : "ipv6",
               (long long)(t1.tv_sec - t0.tv_sec) * 1000LL +
               (long long)(t1.tv_nsec - t0.tv_nsec) / 1000000LL);
        freeaddrinfo(res);
        return 0;
    }
    freeaddrinfo(res);
    printf("NTP_NO_VALID_REPLY attempts=%d transport_errors=%d invalid=%d\n",
           attempts, transport_errors, invalid);
    return 3;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--selftest")) return selftest();
    if (argc != 2 || strlen(argv[1]) > 120u ||
        strchr(argv[1], '/') || strchr(argv[1], ':')) {
        fputs("usage: T311NtpTransportProbe HOST|--selftest\n", stderr);
        return 64;
    }
    return probe(argv[1]);
}
