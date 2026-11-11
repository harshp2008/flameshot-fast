#!/usr/bin/env bash
# Launcher for a GNOME shortcut:  launch.sh [gui|ocr]
# The ScreenCast permission is remembered per launching application, so the
# one-time grant has to happen from this very launcher. First run: setup mode
# (the Share dialog waits for you). Afterwards normal fast captures. If
# Flameshot reports a missing/foreign permission the marker is removed and the
# next press runs the setup again.
SUB="${1:-gui}"
D="${FLAMESHOT_FAST_DIR:-$HOME/.local/opt/flameshot-fast}"
export XDG_CONFIG_HOME="${FLAMESHOT_FAST_CONFIG:-$D/cfg}"
MARK="$D/setup-done"
LOG="$D/last.log"
if [ -e "$MARK" ]; then
    "$D/bin/flameshot" "$SUB" 2>"$LOG"
else
    FLAMESHOT_SCREENCAST_SETUP=1 "$D/bin/flameshot" "$SUB" 2>"$LOG"
    grep -q 'screencast-capture-ms=' "$LOG" && touch "$MARK"
fi
rc=$?
cat "$LOG" >&2
grep -q 'FLAMESHOT_SCREENCAST_SETUP=1' "$LOG" && rm -f "$MARK"
exit $rc
