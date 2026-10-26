#!/system/bin/sh
set -eu
[ "$(id -u)" = 0 ] || exit 60
[ "$#" = 1 ] || exit 61
case "$1" in
    check|apply) ;;
    *) exit 62 ;;
esac
[ -d /data/media/0 ] || exit 63
[ "$(stat -c %u /data/media/0)" = 1023 ] || exit 64
for dir in \
    "/data/media/0/Pictures/Screenshots" \
    "/data/media/0/Pictures/.thumbnails" \
    "/data/media/0/Music/.thumbnails" \
    "/data/media/0/Movies/.thumbnails" \
    "/data/media/0/DCIM/Camera" \
    "/data/media/0/Music/Sound records" \
    "/data/media/0/Documents" \
    "/data/media/0/Audiobooks"
do
    [ -d "$dir" ] || continue
    [ ! -L "$dir" ] || exit 65
    uid="$(stat -c %u "$dir")"
    [ "$uid" = 10145 ] || [ "$uid" = 1023 ] || exit 66
    stale_files="$(find "$dir" -xdev -type f -user 10145 -printf '.' | wc -c)"
    stale_dirs="$(find "$dir" -xdev -type d -user 10145 -printf '.' | wc -c)"
    printf '%s: old_files=%s old_dirs=%s\n' "$dir" "$stale_files" "$stale_dirs"
    [ "$1" = apply ] || continue
    [ "$(find "$dir" -xdev -type l -printf '.' | wc -c)" -eq 0 ] || exit 67
    find "$dir" -xdev -type f -user 10145 -exec sh -c '
        for f do
            chcon u:object_r:media_rw_data_file:s0 "$f" || exit 70
            chown 1023:1023 "$f" || exit 71
            chmod 0664 "$f" || exit 72
        done
    ' sh {} +
    find "$dir" -xdev -depth -type d -user 10145 -exec sh -c '
        for d do
            chcon u:object_r:media_rw_data_file:s0 "$d" || exit 73
            chown 1023:1023 "$d" || exit 74
            chmod 0775 "$d" || exit 75
        done
    ' sh {} +
done
for file in /data/media/0/t311-*.xml /data/media/0/window-camera.xml \
    /data/media/0/Download/SM-T311-AUDIO-SYSTEM-TEST-30s.wav
do
    [ -f "$file" ] || continue
    [ ! -L "$file" ] || exit 76
    uid="$(stat -c %u "$file")"
    [ "$uid" = 10145 ] || [ "$uid" = 1023 ] || exit 77
    printf '%s: old_uid=%s\n' "$file" "$uid"
    [ "$1" = apply ] || continue
    [ "$uid" = 10145 ] || continue
    chcon u:object_r:media_rw_data_file:s0 "$file" || exit 78
    chown 1023:1023 "$file" || exit 79
    chmod 0664 "$file" || exit 80
done
