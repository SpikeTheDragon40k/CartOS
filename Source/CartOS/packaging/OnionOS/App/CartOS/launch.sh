#!/bin/sh
APPDIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
if [ -f "$APPDIR/.skip-startup-animation-once" ] && rm -f "$APPDIR/.skip-startup-animation-once"; then
    export CARTOS_SKIP_STARTUP_ANIMATION=1
fi
export CARTOS_BASE_LD_LIBRARY_PATH="${LD_LIBRARY_PATH-}"
export CARTOS_BASE_HOME="${HOME-__CARTOS_UNSET__}"
export CARTOS_BASE_PATH="${PATH-__CARTOS_UNSET__}"
export CARTOS_BASE_SDL_VIDEODRIVER="${SDL_VIDEODRIVER-__CARTOS_UNSET__}"
export CARTOS_BASE_SDL_AUDIODRIVER="${SDL_AUDIODRIVER-__CARTOS_UNSET__}"
export CARTOS_BASE_EGL_VIDEODRIVER="${EGL_VIDEODRIVER-__CARTOS_UNSET__}"
export HOME="$APPDIR"
export PATH="$APPDIR/bin:/mnt/SDCARD/.tmp_update/bin:/bin:/usr/bin:$PATH"
# Preserve the app-picker environment so OnionOS apps and games do not inherit
# CartOS's bundled SDL libraries, drivers, home, or executable path.
# libpng16 in Onion's parasyte directory needs its matching newer libz.
# Keep that directory ahead of miyoo/lib, which contains an older zlib.
export LD_LIBRARY_PATH="$APPDIR/lib:/mnt/SDCARD/.tmp_update/lib/parasyte:/mnt/SDCARD/.tmp_update/lib:/mnt/SDCARD/miyoo/lib:/lib:/config/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_VIDEODRIVER=mmiyoo
export SDL_AUDIODRIVER=mmiyoo
export EGL_VIDEODRIVER=mmiyoo
export CARTOS_FONT="$APPDIR/res/font_atlas.png"
# Optional overrides. CartOS detects these itself, like OnionOS and spruceOS do:
#   CARTOS_PLATFORM   miyoo-mini | miyoo-mini-plus | any other name
#   CARTOS_SD_ROOT    SD card mount point (default /mnt/SDCARD)
#   CARTOS_SCREEN_W / CARTOS_SCREEN_H   panel size (default: read from /dev/fb0)
export CARTOS_BG_IMAGE="$APPDIR/BG.png"
cd "$APPDIR"

exec "$APPDIR/bin/cartos" "$@"
