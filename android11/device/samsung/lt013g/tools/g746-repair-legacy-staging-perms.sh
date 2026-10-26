#!/system/bin/sh
# G746, optional one-time remediation for historical test artifacts only.
# Do not chmod /data, package directories, media descendants, cache/recovery.
set -eu
[ "$(id -u)" = 0 ] || exit 60
[ "$#" = 1 ] || exit 61
case "$1" in check|apply) ;; *) exit 62;; esac

media_dir=/data/media/0/TWRP
if [ -d "$media_dir" ]; then
    [ ! -L "$media_dir" ] || exit 63
    uid="$(stat -c %u "$media_dir")"
    case "$uid" in 0|1023) ;; *) exit 64;; esac
    printf 'TWRP media folder: %s\n' "$(stat -c '%a %u:%g' "$media_dir")"
    if [ "$1" = apply ]; then
        chown 1023:1023 "$media_dir"
        chmod 0775 "$media_dir"
    fi
fi

for dir in /cache/SM-T311-G*; do
    [ -d "$dir" ] || continue
    [ ! -L "$dir" ] || exit 65
    [ "$(stat -c %u "$dir")" = 0 ] || exit 66
    printf 'Historical cache folder: %s\n' "$dir"
    [ "$1" = apply ] || continue
    # Strip group/other write only from historical test paths. Leave mode of
    # active recovery/cache and everything under /data/user untouched.
    find "$dir" -xdev -type f -perm /022 -exec chmod go-w {} +
    find "$dir" -xdev -type d -perm /022 -exec chmod go-w {} +
done
