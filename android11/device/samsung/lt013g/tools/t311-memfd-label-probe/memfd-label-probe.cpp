#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/memfd.h>
#include <selinux/selinux.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

int main() {
    int fd = static_cast<int>(syscall(__NR_memfd_create, "t311-ephemeral-label-test", MFD_ALLOW_SEALING | MFD_CLOEXEC));
    if (fd < 0) {
        printf("CREATE=FAIL errno=%d %s\n", errno, strerror(errno));
        return 1;
    }
    printf("CREATE=PASS fd=%d\n", fd);
    char* label = nullptr;
    int r = fgetfilecon(fd, &label);
    printf("BEFORE rc=%d errno=%d label=%s\n", r, r < 0 ? errno : 0, r >= 0 ? label : "(none)");
    if (label != nullptr) freecon(label);
    const char *target = "u:object_r:appdomain_tmpfs:s0";
    errno = 0;
    r = fsetfilecon(fd, target);
    printf("RELABEL rc=%d errno=%d detail=%s\n", r, r < 0 ? errno : 0, r < 0 ? strerror(errno) : "OK");
    label = nullptr;
    int t = fgetfilecon(fd, &label);
    printf("AFTER rc=%d errno=%d label=%s\n", t, t < 0 ? errno : 0, t >= 0 ? label : "(none)");
    if (label != nullptr) freecon(label);
    close(fd);
    printf("CLOSED=1\n");
    return 0;
}
