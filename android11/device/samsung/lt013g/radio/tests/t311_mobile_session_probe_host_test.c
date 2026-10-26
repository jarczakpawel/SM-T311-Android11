// Host and Android ARM32 tests for the actual production rmnet classifier.
#define _DEFAULT_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#define getifaddrs test_getifaddrs
#define freeifaddrs test_freeifaddrs
#define main probe_main
#include "../t311_mobile_session_probe.c"
#undef getifaddrs
#undef freeifaddrs
#undef main
#include <assert.h>
#include <stdlib.h>

static struct ifaddrs *mock_list;
static int mock_fail;
int test_getifaddrs(struct ifaddrs **result) {
    if (mock_fail) return -1;
    *result = mock_list;
    return 0;
}
void test_freeifaddrs(struct ifaddrs *ignored) { (void)ignored; }

static int run(void) {
    char arg0[] = "probe";
    char arg1[] = "rmnet1";
    char *argv[] = {arg0, arg1, NULL};
    return probe_main(2, argv);
}

static int ntests;
static void check(const char *label, int expected) {
    int actual = run();
    if (actual != expected) {
        fprintf(stderr, "G616_FAIL %s got=%d expected=%d\n", label, actual, expected);
        abort();
    }
    ++ntests;
}

int main(void) {
    struct ifaddrs primary = {0}, other = {0};
    struct sockaddr_in ipv4 = {.sin_family = AF_INET};
    struct sockaddr_in6 ipv6 = {.sin6_family = AF_INET6};
    primary.ifa_name = "rmnet1";
    mock_list = &primary;
    check("primary present without assigned address", 0);

    ipv4.sin_addr.s_addr = htonl(0x0a000001);
    primary.ifa_addr = (struct sockaddr *)&ipv4;
    check("primary IPv4", 1);
    ipv4.sin_addr.s_addr = INADDR_ANY;
    check("primary unspecified IPv4", 0);

    primary.ifa_addr = (struct sockaddr *)&ipv6;
    check("primary unspecified IPv6", 0);
    assert(inet_pton(AF_INET6, "fe80::1234", &ipv6.sin6_addr) == 1);
    check("primary IPv6 link-local", 0);
    assert(inet_pton(AF_INET6, "2001:db8::1234", &ipv6.sin6_addr) == 1);
    check("primary global IPv6", 1);

    primary.ifa_addr = NULL;
    primary.ifa_name = "rmnet2";
    check("primary missing", 2);
    primary.ifa_name = "rmnet1";
    mock_fail = 1;
    check("getifaddrs failure", 2);
    mock_fail = 0;

    primary.ifa_next = &other;
    other.ifa_name = "rmnet0";
    ipv4.sin_addr.s_addr = htonl(0x0a000002);
    other.ifa_addr = (struct sockaddr *)&ipv4;
    check("alternate rmnet0 IPv4 blocks restart", 1);
    other.ifa_name = "rmnet2";
    check("alternate rmnet2 IPv4 blocks restart", 1);
    other.ifa_name = "rmnet12";
    check("alternate multi-digit rmnet IPv4", 1);

    other.ifa_name = "wlan0";
    check("unrelated WLAN IPv4 ignored", 0);
    other.ifa_name = "rmnetX";
    check("non-numeric rmnet name ignored", 0);

    other.ifa_name = "rmnet0";
    other.ifa_addr = (struct sockaddr *)&ipv6;
    check("alternate rmnet global IPv6", 1);
    assert(inet_pton(AF_INET6, "fe80::5678", &ipv6.sin6_addr) == 1);
    check("alternate rmnet link-local IPv6", 0);

    other.ifa_addr = NULL;
    check("alternate rmnet no address", 0);
    primary.ifa_name = "rmnet3";
    check("primary missing despite alternate interface", 2);

    assert(ntests == 17);
    puts("G616_ALL_RMNET_SESSION_PROBE_17_CASES_PASS");
    return 0;
}
