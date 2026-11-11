#!/usr/bin/env bash
# Launcher for a GNOME shortcut:  launch.sh [gui|ocr]
# The ScreenCast permission is remembered per launching application and per
# monitor layout; this launcher asks for it (Share dialog) only when missing.
SUB="${1:-gui}"
D="${FLAMESHOT_FAST_DIR:-$HOME/.local/opt/flameshot-fast}"
export XDG_CONFIG_HOME="${FLAMESHOT_FAST_CONFIG:-$D/cfg}"
LOG="$D/last.log"
# "auto": the Share dialog is shown only when this launcher and monitor layout
# have no saved permission (first run, or after plugging/unplugging a monitor).
FLAMESHOT_SCREENCAST_SETUP=auto "$D/bin/flameshot" "$SUB" 2>"$LOG"
rc=$?
cat "$LOG" >&2
exit $rc
