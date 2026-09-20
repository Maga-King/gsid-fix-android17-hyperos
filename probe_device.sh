#!/system/bin/sh
set -eu
# Run in init's mount namespace. The only overlay is removed by the EXIT trap.
PATCH=/data/local/tmp/gsid-android17-loopfix-check
PROBE=/data/local/tmp/gsid17-image-probe
DATA=/data/gsi/gsid17check
META=/metadata/gsi/gsid17check
mounted=0
staged=0
cleanup() {
    if [ "$mounted" = 1 ]; then
        setprop ctl.stop gsid
        sleep 1
        umount /system/bin/gsid || echo 'ERROR: transient overlay still mounted'
    fi
    if [ "$staged" = 1 ]; then
        umount /dev/gsid17-check
        rmdir /dev/gsid17-check
    fi
    # The image service deletes the owned image. Remove empty test directories only.
    rmdir "$DATA" "$META" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
[ ! -e "$DATA" ] && [ ! -e "$META" ] || { echo 'Test directory already exists'; exit 1; }
[ "$(getprop ro.gsid.image_running)" != 1 ] || exit 1
mkdir "$DATA" "$META"
restorecon -RF "$DATA" "$META"
chmod 0755 "$PROBE" "$PATCH"
chcon u:object_r:gsid_exec:s0 "$PATCH"
echo '=== Native GSID negative control ==='
timeout 15 gsi_tool status
"$PROBE" --expect-map-failure
echo '=== Transient patched GSID test ==='
setprop ctl.stop gsid
sleep 1
mkdir /dev/gsid17-check
mount -t tmpfs -o mode=0700 tmpfs /dev/gsid17-check
staged=1
cp "$PATCH" /dev/gsid17-check/gsid
chmod 0755 /dev/gsid17-check/gsid
chcon u:object_r:gsid_exec:s0 /dev/gsid17-check/gsid
mount --bind /dev/gsid17-check/gsid /system/bin/gsid
mounted=1
timeout 15 gsi_tool status
"$PROBE"
ps -A -Z | grep gsid
echo '=== Probe complete; restoring stock GSID ==='
