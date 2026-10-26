struct selabel_handle;

int is_selinux_enabled(void) {
    return 0;
}

struct selabel_handle* selinux_android_service_context_handle(void) {
    return (struct selabel_handle*)1;
}

int selinux_status_open(int fallback) {
    (void)fallback;
    return 0;
}

int selinux_status_updated(void) {
    return 0;
}
