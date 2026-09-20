#!/system/bin/sh
# Restore the stock executable for this boot; module disable takes effect next boot.
if [ "${1:-}" != --global ]; then
    exec nsenter -t 1 -m -- /system/bin/sh "$0" --global
fi
RUNTIME=/dev/gsid-fix17
TARGET=/system/bin/gsid
[ -f "$RUNTIME/gsid" ] || exit 0
if [ "$(stat -c '%d:%i' "$TARGET")" = "$(stat -c '%d:%i' "$RUNTIME/gsid")" ]; then
    setprop ctl.stop gsid
    sleep 1
    umount "$TARGET" || exit 1
fi
umount "$RUNTIME" || exit 1
rmdir "$RUNTIME"
echo 'Stock GSID restored for this boot.'
