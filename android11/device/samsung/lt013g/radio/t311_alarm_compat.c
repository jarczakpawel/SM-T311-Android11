typedef __builtin_va_list t311_va_list;

extern int openat(int, const char *, int, ...);

static int t311_is_alarm_path(const char *p)
{
    static const char s[] = "/dev/alarm";
    int i = 0;

    if (!p)
        return 0;
    while (s[i]) {
        if (p[i] != s[i])
            return 0;
        i++;
    }
    return p[i] == 0;
}

int open(const char *path, int flags, ...)
{
    unsigned int mode = 0;

    if ((flags & 0100) || (flags & 020000000)) {
        t311_va_list ap;
        __builtin_va_start(ap, flags);
        mode = __builtin_va_arg(ap, unsigned int);
        __builtin_va_end(ap);
    }

    if (t311_is_alarm_path(path))
        return openat(-100, "/dev/null", 0, 0);

    return openat(-100, path, flags, mode);
}
