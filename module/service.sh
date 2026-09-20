#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Intentionally no post-fs-data script or system/ overlay.
MODDIR=${0%/*}
LOG="$MODDIR/activation.log"
if [ "${1:-}" != --global ]; then
    exec nsenter -t 1 -m -- /system/bin/sh "$0" --global
fi
exec >>"$LOG" 2>&1
date
RUNTIME=/dev/gsid-fix17
TARGET=/system/bin/gsid
mounted=0
staged=0
rollback_failed_activation() {
    if [ "$mounted" = 1 ]; then
        setprop ctl.stop gsid
        sleep 1
        umount "$TARGET" || return 1
    fi
    if [ "$staged" = 1 ]; then
        umount "$RUNTIME" || return 1
        rmdir "$RUNTIME"
    fi
}
fail() {
    echo "Activation stopped: $*"
    rollback_failed_activation
    exit 1
}
count=0
while [ "$(getprop sys.boot_completed)" != 1 ]; do
    if [ -e "$MODDIR/disable" ] || [ -e "$MODDIR/remove" ]; then exit 0; fi
    count=$((count + 1))
    [ "$count" -le 180 ] || { echo 'Boot not complete; leaving stock GSID in place'; exit 0; }
    sleep 1
done
[ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ] || exit 0
[ "$(getprop ro.gsid.image_running)" != 1 ] || { echo 'Inside DSU; skipped'; exit 0; }
[ ! -e "$RUNTIME" ] || { echo 'Runtime directory already exists; skipped'; exit 0; }
# Do not interrupt an installation or a late native startup task.
[ -z "$(pidof gsid)" ] || { echo 'GSID is busy; run service.sh again when idle'; exit 0; }
mkdir "$RUNTIME" || fail mkdir
mount -t tmpfs -o mode=0700 tmpfs "$RUNTIME" || { rmdir "$RUNTIME"; fail tmpfs; }
staged=1
cp "$MODDIR/bin/gsid" "$RUNTIME/gsid" || fail copy
chmod 0755 "$RUNTIME/gsid" || fail chmod
chcon u:object_r:gsid_exec:s0 "$RUNTIME/gsid" || fail chcon
# tmpfs avoids /data's nosuid flag and the denied init->gsid transition.
mount --bind "$RUNTIME/gsid" "$TARGET" || fail bind
mounted=1
timeout 15 gsi_tool status || fail 'native Binder health check'
echo 'Active: native GSID with loop fallback. Stock early-boot GSID was preserved.'
