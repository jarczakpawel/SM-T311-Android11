#!/system/bin/sh
set -eu
[ "$(id -u)" = 0 ] || exit 60
printf 'G742 permissions audit Android11\n'
printf 'SELinux: '; getenforce
printf 'BOOT: '; getprop sys.boot_completed
printf 'SYS_PARTITIONS\n'
for base in /system /vendor /system_ext /product /odm; do
    [ -d "$base" ] || continue
    printf 'BASE %s\n' "$base"
    find "$base" -xdev -type f -perm /002 -printf '%m %U:%G %Z %p\n' 2>/dev/null | head -n 20
    find "$base" -xdev -type d -perm /002 -printf '%m %U:%G %Z %p\n' 2>/dev/null | head -n 20
    find "$base" -xdev -type f -perm /6000 -printf 'setuid-or-setgid %m %U:%G %p\n' 2>/dev/null | head -n 35
done
printf 'SENSITIVE_PATHS\n'
for base in /data/system /data/misc /data/vendor /data/user/0 /data/app /cache; do
    [ -d "$base" ] || continue
    printf 'BASE %s\n' "$base"
    find "$base" -xdev -type f -perm /002 -printf 'public-writable-file %m %U:%G %Z %p\n' 2>/dev/null | head -n 16
    find "$base" -xdev -type d -perm /002 -printf 'public-writable-dir %m %U:%G %Z %p\n' 2>/dev/null | head -n 16
done
printf 'SHARED_MEDIA\n'
[ -d /data/media/0 ] && {
    find /data/media/0 -xdev -type d -perm 0700 -printf 'private-dir %m %U:%G %Z %p\n' 2>/dev/null | head -n 120
    find /data/media/0 -xdev -type d -printf '%U:%G\n' 2>/dev/null | sort | uniq -c | sort -rn | head -n 16
    find /data/media/0 -xdev -type f -printf '%U:%G\n' 2>/dev/null | sort | uniq -c | sort -rn | head -n 16
    find /data/media/0 -xdev -type d -printf '%Z\n' 2>/dev/null | sort | uniq -c | sort -rn | head -n 12
}
printf 'KERNEL_SECURITY\n'
for path in /proc/sys/kernel/randomize_va_space /proc/sys/kernel/kptr_restrict /proc/sys/kernel/dmesg_restrict /sys/fs/selinux/enforce; do
    [ -e "$path" ] && printf '%s=%s\n' "$path" "$(cat "$path" 2>/dev/null)"
done
