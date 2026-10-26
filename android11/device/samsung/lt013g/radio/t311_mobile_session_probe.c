// Read-only IPv4/IPv6 inventory for all SM-T311 rmnet cellular interfaces.
// Exit 0: primary interface present and no rmnet global address.
// Exit 1: any rmnet has IPv4 or non-link-local IPv6; do not restart modem.
// Exit 2: inventory failure or missing primary interface; fail closed.
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

static int is_rmnet_interface(const char *name) {
    if (name == NULL || strncmp(name, "rmnet", 5) != 0)
        return 0;
    const char *digit = name + 5;
    if (*digit < '0' || *digit > '9')
        return 0;
    for (; *digit != '\0'; ++digit) {
        if (*digit < '0' || *digit > '9')
            return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2 || strcmp(argv[1], "rmnet1") != 0) {
        return 2;
    }

    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0) {
        return 2;
    }

    int found = 0;
    int active = 0;
    for (const struct ifaddrs *entry = list; entry != NULL; entry = entry->ifa_next) {
        if (!is_rmnet_interface(entry->ifa_name)) {
            continue;
        }
        if (strcmp(entry->ifa_name, "rmnet1") == 0)
            found = 1;
        if (!entry->ifa_addr) {
            continue;
        }
        if (entry->ifa_addr->sa_family == AF_INET) {
            const struct sockaddr_in *addr = (const struct sockaddr_in *)entry->ifa_addr;
            if (addr->sin_addr.s_addr != INADDR_ANY) {
                active = 1;
                break;
            }
        } else if (entry->ifa_addr->sa_family == AF_INET6) {
            const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)entry->ifa_addr;
            if (!IN6_IS_ADDR_UNSPECIFIED(&addr6->sin6_addr) &&
                !IN6_IS_ADDR_LOOPBACK(&addr6->sin6_addr) &&
                !IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr)) {
                active = 1;
                break;
            }
        }
    }
    freeifaddrs(list);
    return !found ? 2 : (active ? 1 : 0);
}
