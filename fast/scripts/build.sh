#!/usr/bin/env bash
# Reproducible local build of flameshot-fast. Installs into ./dist only.
# Usage: fast/scripts/build.sh [extra cmake args...]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
PREFIX="${PREFIX:-$ROOT/dist}"

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_WAYLAND_CLIPBOARD=OFF \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    "$@"
cmake --build "$BUILD_DIR" -j"$(nproc)"
cmake --install "$BUILD_DIR" >/dev/null

echo "binary: $PREFIX/bin/flameshot"
