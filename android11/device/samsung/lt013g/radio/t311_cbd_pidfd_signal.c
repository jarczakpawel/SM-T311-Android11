// Exact-process Samsung CBD termination: pidfd reference prevents PID reuse.
// Do NOT allow a fallback to the non-atomic kill(pid,...) interface.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef T311_CBD_EXEC_A
#define T311_CBD_EXEC_A "/vendor/bin/t311-cbd"
#endif
#ifndef T311_CBD_EXEC_B
#define T311_CBD_EXEC_B "/system/vendor/bin/t311-cbd"
#endif
#ifndef T311_CBD_COMM
#define T311_CBD_COMM "t311-cbd"
#endif

static bool decimal_u64(const char *text, unsigned long long *value) {
    if (!text || !*text) return false;
    for (const char *p = text; *p; ++p)
        if (*p < '0' || *p > '9') return false;
    errno = 0;
    char *end;
    unsigned long long x = strtoull(text, &end, 10);
    if (errno || *end || x == 0) return false;
    *value = x;
    return true;
}

static bool process_starttime(pid_t pid, unsigned long long *out) {
    char path[64], data[4096];
    int size = snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    if (size < 0 || (size_t)size >= sizeof(path)) return false;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    ssize_t n = read(fd, data, sizeof(data) - 1);
    close(fd);
    if (n <= 0 || (size_t)n == sizeof(data) - 1) return false;
    data[n] = '\0';
    char prefix[64];
    size = snprintf(prefix, sizeof(prefix), "%ld (" T311_CBD_COMM ") ", (long)pid);
    if (size < 0 || (size_t)size >= sizeof(prefix)
        || strncmp(data, prefix, (size_t)size) != 0) return false;
    char *token = data + size;
    // /proc/PID/stat field 3 is token 1 after PID and comm.
    for (int field = 3; field <= 22; ++field) {
        while (*token == ' ') ++token;
        if (*token == '\0' || *token == '\n') return false;
        char *end = token;
        while (*end && *end != ' ' && *end != '\n') ++end;
        if (field == 22) {
            char saved = *end;
            *end = '\0';
            bool ok = decimal_u64(token, out);
            *end = saved;
            return ok;
        }
        token = end;
    }
    return false;
}

static bool verified_identity(pid_t pid, const char *expected) {
    char path[64], actual_exe[PATH_MAX], expected_identity[PATH_MAX + 48];
    int size = snprintf(path, sizeof(path), "/proc/%ld/exe", (long)pid);
    if (size < 0 || (size_t)size >= sizeof(path)) return false;
    ssize_t n = readlink(path, actual_exe, sizeof(actual_exe) - 1);
    if (n <= 0 || (size_t)n >= sizeof(actual_exe) - 1) return false;
    actual_exe[n] = '\0';
    if (strcmp(actual_exe, T311_CBD_EXEC_A) != 0 &&
        strcmp(actual_exe, T311_CBD_EXEC_B) != 0) return false;
    unsigned long long start;
    if (!process_starttime(pid, &start)) return false;
    size = snprintf(expected_identity, sizeof(expected_identity),
                    "%s|%llu", actual_exe, start);
    return size > 0 && (size_t)size < sizeof(expected_identity)
        && strcmp(expected_identity, expected) == 0;
}

int main(int argc, char **argv) {
    bool check_only = argc == 4 && strcmp(argv[1], "--check") == 0;
    if ((!check_only && argc != 3) || (check_only && argc != 4)) return 2;
    const char *pidstr = argv[1 + check_only], *identity = argv[2 + check_only];
    unsigned long long parsed;
    if (!decimal_u64(pidstr, &parsed) || parsed > INT_MAX) return 2;
    pid_t pid = (pid_t)parsed;
    // Acquire kernel handle BEFORE reading /proc so later PID reuse is harmless.
    int pidfd = (int)syscall(SYS_pidfd_open, pid, 0);
    if (pidfd < 0) return 3;
    bool valid = verified_identity(pid, identity);
    if (!valid) { close(pidfd); return 4; }
    // A 0 signal in validation mode checks permission without affecting the process.
    int signal_no = check_only ? 0 : SIGKILL;
    int result = (int)syscall(SYS_pidfd_send_signal, pidfd, signal_no, NULL, 0);
    int saved = errno;
    close(pidfd);
    if (result != 0) { errno = saved; return 5; }
    return 0;
}
