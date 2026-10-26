// Execute the production pidfd implementation on an owned harmless child only.
#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <sys/wait.h>
#ifdef __ANDROID__
#define T311_CBD_EXEC_A "/data/local/tmp/G619-pidfd-test"
#else
#define T311_CBD_EXEC_A "/tmp/G619-pidfd-test"
#endif
#define T311_CBD_EXEC_B T311_CBD_EXEC_A
#define T311_CBD_COMM "G619-pidfd-test"
#define main cbd_pidfd_main
#include "../t311_cbd_pidfd_signal.c"
#undef main

static char pid_arg[32];
static char identity[PATH_MAX + 48];
static pid_t spawn_child(void) {
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        char *args[] = { (char *)T311_CBD_EXEC_A, "--child", NULL };
        execv(T311_CBD_EXEC_A, args);
        _exit(91);
    }
    snprintf(pid_arg, sizeof(pid_arg), "%ld", (long)child);
    bool ready = false;
    for (int i = 0; i < 100; ++i) {
        unsigned long long start;
        char exe[PATH_MAX], path[64];
        snprintf(path, sizeof(path), "/proc/%ld/exe", (long)child);
        ssize_t n = readlink(path, exe, sizeof(exe) - 1);
        if (n > 0 && (size_t)n < sizeof(exe) && process_starttime(child, &start)) {
            exe[n] = 0;
            snprintf(identity, sizeof(identity), "%s|%llu", exe, start);
            if (strcmp(exe, T311_CBD_EXEC_A) == 0) {
                ready = true; break;
            }
        }
        usleep(10000);
    }
    assert(ready);
    return child;
}
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--child") == 0) {
        for (;;) pause();
    }
    assert(argc == 1);
    assert(cbd_pidfd_main(1, argv) == 2);
    assert(!decimal_u64("abc", &(unsigned long long){0}));
    assert(!decimal_u64("0", &(unsigned long long){0}));
    pid_t child = spawn_child();
    char *positive[] = {(char *)"test", (char *)"--check", pid_arg, identity, NULL};
    assert(cbd_pidfd_main(4, positive) == 0);
    // If starttime changes or executable is forged, do not signal.
    char wrong[PATH_MAX + 48];
    snprintf(wrong, sizeof(wrong), "%s", identity);
    char *sep = strrchr(wrong, '|');
    assert(sep); strcpy(sep + 1, "99999999");
    positive[3] = wrong;
    assert(cbd_pidfd_main(4, positive) == 4);
    char *invalid[] = {(char *)"test", (char *)"bogus", identity, NULL};
    assert(cbd_pidfd_main(3, invalid) == 2);
    positive[3] = identity;
    char *terminate[] = {(char *)"test", pid_arg, identity, NULL};
    assert(cbd_pidfd_main(3, terminate) == 0);
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    assert(cbd_pidfd_main(4, positive) != 0);
    puts("G619_PIDFD_OWN_CHILD_SIGNAL_AND_IDENTITY_PASS");
    return 0;
}
