#!/system/bin/sh
SKIPMOUNT=true
[ "$ARCH" = arm64 ] || abort "! This package contains an arm64 executable."
ui_print "- GSID Fix - Android 17 HyperOS (安卓17澎湃)"
ui_print "- Native GSID loop fallback for unencrypted /data"
ui_print "- No Android version or firmware whitelist"
ui_print "- Activation waits for sys.boot_completed=1"
ui_print "- No early-boot system/bin/gsid overlay"
touch "$MODPATH/skip_mount"
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/bin/gsid" 0 0 0755 u:object_r:gsid_exec:s0
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/rollback.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
ui_print "- Internal single-file image storage supported"
