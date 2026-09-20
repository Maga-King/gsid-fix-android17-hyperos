#!/system/bin/sh
set -eu
[ ! -e /dev/gsid-fix17 ] && [ ! -e /dev/gsid17-check ] || exit 1
[ ! -e /data/gsi/gsid17check ] && [ ! -e /metadata/gsi/gsid17check ] || exit 1
STAGE=/data/local/tmp/gsid17-module-check
if [ -d "$STAGE" ]; then
    [ "$(readlink -f "$STAGE")" = /data/local/tmp/gsid17-module-check ] || exit 1
    rm -rf -- "$STAGE"
fi
rm -f /data/local/tmp/gsid-fix-upstream-check \
      /data/local/tmp/gsid-android17-loopfix-check \
      /data/local/tmp/gsid17-image-probe \
      /data/local/tmp/gsid17-probe-device.sh \
      /data/local/tmp/GSID-Fix-Android17-v17.1-local.zip
sha256sum /system/bin/gsid
timeout 15 gsi_tool status
getenforce
