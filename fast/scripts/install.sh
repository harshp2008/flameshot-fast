#!/usr/bin/env bash
# User-level install of flameshot-fast (no sudo, nothing in /usr).
#
#   fast/scripts/install.sh [--build] [--shortcuts]
#
#   --build      run fast/scripts/build.sh first
#   --shortcuts  (re)create two GNOME test shortcuts: Shift+Super+F (gui) and
#                Shift+Super+O (ocr text copy). Never touches other shortcuts.
#
# Installs to ~/.local/opt/flameshot-fast (binary, launch.sh, cfg/) and checks
# the runtime dependencies of the OCR feature. Missing system packages are
# printed as one apt command for you to run; they are not installed here.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
D="${FLAMESHOT_FAST_DIR:-$HOME/.local/opt/flameshot-fast}"
BUILD=0; SHORTCUTS=0
for a in "$@"; do
    case "$a" in --build) BUILD=1 ;; --shortcuts) SHORTCUTS=1 ;; *) echo "unknown option $a" >&2; exit 2 ;; esac
done
[ "$BUILD" = 1 ] && "$ROOT/fast/scripts/build.sh"
[ -x "$ROOT/dist/bin/flameshot" ] || { echo "no dist/bin/flameshot; run with --build" >&2; exit 1; }

mkdir -p "$D/bin" "$D/cfg/flameshot"
cp "$ROOT/dist/bin/flameshot" "$D/bin/flameshot.new" && mv -f "$D/bin/flameshot.new" "$D/bin/flameshot" # atomic; safe while running
cp "$ROOT/fast/scripts/launch.sh" "$D/launch.sh"
INI="$D/cfg/flameshot/flameshot.ini"
[ -f "$INI" ] || printf '[General]\n' >"$INI"
setkey() { grep -q "^$1=" "$INI" || sed -i "/^\[General\]/a $1=$2" "$INI"; }
setkey useScreenCastCapture true
setkey showDesktopNotification true
command -v spawn-at >/dev/null && setkey screenCastPointerCommand "$(command -v spawn-at) query pointer"
echo "installed: $D/bin/flameshot ($("$D/bin/flameshot" --version | head -1))"

echo; echo "== dependencies =="
MISSING=()
need_pkg() { dpkg -s "$1" >/dev/null 2>&1 && printf '  ok  %-20s %s\n' "$1" "$(dpkg-query -W -f='${Version}' "$1")" || { printf '  MISSING %s\n' "$1"; MISSING+=("$1"); }; }
need_pkg tesseract-ocr        # OCR engine (`flameshot ocr`)
need_pkg tesseract-ocr-eng    # English language data
need_pkg libpipewire-0.3-0t64 || true
if command -v spawn-at >/dev/null; then
    echo "  ok  spawn-at            $(spawn-at --version 2>&1 | head -1)"
    spawn-at update --check 2>&1 | grep -iE "up to date|available|new version" | sed 's/^/      /' || true
else
    echo "  MISSING spawn-at (pointer-based monitor choice falls back to the selector)"
fi
if [ "${#MISSING[@]}" -gt 0 ]; then
    echo; echo "Run (needs sudo, not done here):  sudo apt install ${MISSING[*]}"
fi
echo "Note: Ubuntu 24.04 ships tesseract 5.3.4; newer upstream releases need a PPA or a source build (not done)."

if [ "$SHORTCUTS" = 1 ]; then
    python3 - "$D" <<'PY'
import subprocess, ast, sys
d = sys.argv[1]
S = "org.gnome.settings-daemon.plugins.media-keys"
base = "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/"
items = [("flameshot-fast-test", "Flameshot FAST (test)", f"{d}/launch.sh gui", "<Shift><Super>f"),
         ("flameshot-fast-ocr-test", "Flameshot FAST OCR (test)", f"{d}/launch.sh ocr", "<Shift><Super>o")]
cur = ast.literal_eval(subprocess.check_output(["gsettings", "get", S, "custom-keybindings"], text=True).strip())
for key, name, cmd, binding in items:
    p = base + key + "/"
    if p not in cur:
        cur.append(p)
    k = f"{S}.custom-keybinding:{p}"
    for field, val in (("name", name), ("command", cmd), ("binding", binding)):
        subprocess.check_call(["gsettings", "set", k, field, val])
subprocess.check_call(["gsettings", "set", S, "custom-keybindings", str(cur)])
print("shortcuts set: Shift+Super+F (gui), Shift+Super+O (ocr)")
PY
fi
