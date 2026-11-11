#!/usr/bin/env bash
# One-line installer for flameshot-fast (Debian/Ubuntu, amd64, GNOME on Wayland).
#
#   curl -fsSL https://raw.githubusercontent.com/harshp2008/flameshot-fast/screencast-capture/fast/packaging/install.sh | bash
#
# What it does:
#   1. downloads the newest release .deb from GitHub and checks its SHA-256
#   2. installs it with apt (asks for your sudo password; this replaces the
#      distribution's `flameshot`)
#   3. runs `flameshot-fast-setup` as YOUR user: config keys, the shortcuts
#      Super+Shift+S / Super+Shift+T / Super+Print, and spawn-at
#
# Options (after `bash -s --` when piped):
#   --uninstall       undo the per-user setup and remove the package
#   --no-setup        install the package only
#   --download-only   download and verify, do not install (prints the path)
#   --tag TAG         install a specific release tag instead of the newest
set -euo pipefail

REPO="${FLAMESHOT_FAST_REPO:-harshp2008/flameshot-fast}"
TAG=""
DO_SETUP=1
DOWNLOAD_ONLY=0
UNINSTALL=0

while [ $# -gt 0 ]; do
    case "$1" in
        --uninstall) UNINSTALL=1 ;;
        --no-setup) DO_SETUP=0 ;;
        --download-only) DOWNLOAD_ONLY=1 ;;
        --tag) TAG="${2:?--tag needs a value}"; shift ;;
        -h|--help) sed -n '2,20p' "$0" 2>/dev/null || true; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die() { echo "error: $*" >&2; exit 1; }

if [ "$(id -u)" -eq 0 ]; then
    die "run this as your normal user (it uses sudo only for the package step); the shortcuts are per user"
fi
command -v curl >/dev/null || die "curl is required"

if [ "$UNINSTALL" -eq 1 ]; then
    if command -v flameshot-fast-setup >/dev/null; then
        say "Undoing the per-user setup"
        flameshot-fast-setup --undo || true
    fi
    say "Removing the package (sudo)"
    sudo apt-get remove -y flameshot-fast
    echo "Stock Flameshot is not reinstalled automatically: sudo apt install flameshot"
    exit 0
fi

if [ "$DOWNLOAD_ONLY" -eq 0 ]; then
    command -v apt-get >/dev/null || die "this installer supports Debian/Ubuntu (apt) only; see fast/docs/INSTALL.md to build from source"
    [ "$(dpkg --print-architecture)" = amd64 ] || die "only amd64 packages are published"
    # shellcheck disable=SC1091
    . /etc/os-release 2>/dev/null || true
    if [ "${ID:-}" != ubuntu ] || [ "${VERSION_ID:-}" != "24.04" ]; then
        echo "note: built on Ubuntu 24.04; on ${PRETTY_NAME:-this system} apt may not find matching Qt 6.4 libraries" >&2
    fi
fi

SB="$(mktemp -d /tmp/w11c-sandbox.XXXXXX)"
cleanup() {
    if [ "$DOWNLOAD_ONLY" -eq 1 ]; then return; fi
    if [[ "$SB" == /tmp/w11c-sandbox.* ]] && [ -d "$SB" ]; then
        if findmnt -M "$SB" >/dev/null 2>&1; then
            echo "not removing $SB: it is a mount point" >&2
        else
            rm -rf "$SB"
        fi
    fi
}
trap cleanup EXIT
chmod 755 "$SB" # apt's sandbox user must be able to read the file

if [ -n "$TAG" ]; then
    API="https://api.github.com/repos/$REPO/releases/tags/$TAG"
else
    API="https://api.github.com/repos/$REPO/releases/latest"
fi
say "Looking up the release ($REPO${TAG:+ $TAG})"
curl -fsSL -H 'Accept: application/vnd.github+json' "$API" -o "$SB/release.json" ||
    die "could not read the release information from $API"

urls="$(grep -o '"browser_download_url": *"[^"]*"' "$SB/release.json" | sed 's/.*: *"\(.*\)"/\1/')"
DEB_URL="$(printf '%s\n' "$urls" | grep '\.deb$' | head -1 || true)"
SUM_URL="$(printf '%s\n' "$urls" | grep 'SHA256SUMS$' | head -1 || true)"
[ -n "$DEB_URL" ] || die "the release has no .deb file"
[ -n "$SUM_URL" ] || die "the release has no SHA256SUMS file; refusing to install an unchecked package"

DEB_NAME="$(basename "$DEB_URL")"
DEB_NAME="${DEB_NAME//%2B/+}" # GitHub URL-encodes the plus sign
DEB="$SB/$DEB_NAME"
say "Downloading $DEB_NAME"
curl -fL --progress-bar "$DEB_URL" -o "$DEB"
curl -fsSL "$SUM_URL" -o "$SB/SHA256SUMS"
say "Checking SHA-256"
(cd "$SB" && grep -F " $DEB_NAME" SHA256SUMS | sha256sum -c -) ||
    die "checksum mismatch; not installing"

if [ "$DOWNLOAD_ONLY" -eq 1 ]; then
    echo "$DEB"
    exit 0
fi

say "Installing the package (sudo; this replaces the distribution's flameshot)"
sudo apt-get install -y "$DEB"

if [ "$DO_SETUP" -eq 1 ]; then
    say "Setting up config and shortcuts for $USER"
    flameshot-fast-setup
fi

echo
say "Done"
echo "  Super+Shift+S  capture image      Super+Shift+T  capture + copy text (OCR)      Super+Print  capture image"
echo "  First capture: tick ALL screens in the Share dialog and press Share."
echo "  Log out and in once so the pointer helper (GNOME extension) loads."
if pgrep -x flameshot >/dev/null; then
    echo "  A Flameshot background process from before is still running; it keeps the"
    echo "  old code until it restarts (log out and in, or: pkill -u \"\$USER\" -x flameshot)."
fi
echo "  Remove everything again: curl -fsSL https://raw.githubusercontent.com/$REPO/screencast-capture/fast/packaging/install.sh | bash -s -- --uninstall"
