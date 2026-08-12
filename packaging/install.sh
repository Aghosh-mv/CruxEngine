#!/usr/bin/env bash
# FrostEngine installer — installs the built game binary + icon + desktop entry.
# Usage: ./packaging/install.sh [--user]
#   --user  install to ~/.local (no root required)   [default when run as non-root]
#   (no flag) install to /usr/local (requires sudo)   [default when run as root]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BIN="$ROOT/build/FrostGame"

if [[ ! -x "$BIN" ]]; then
    echo "error: $BIN not found. Run: cmake -B build && cmake --build build -j" >&2
    exit 1
fi

if [[ "${1:-}" == "--user" || "$(id -u)" != "0" ]]; then
    PREFIX="$HOME/.local"
else
    PREFIX="/usr/local"
fi

BINDIR="$PREFIX/bin"
ICONDIR="$PREFIX/share/icons/hicolor/256x256/apps"
APPDIR="$PREFIX/share/applications"

mkdir -p "$BINDIR" "$ICONDIR" "$APPDIR"

install -m 755 "$BIN" "$BINDIR/frostgame"
install -m 644 "$HERE/frostengine.png" "$ICONDIR/frostengine.png"
install -m 644 "$HERE/frostengine.desktop" "$APPDIR/frostengine.desktop"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPDIR" || true
fi

echo "Installed FrostEngine to $PREFIX"
echo "Run it with: frostgame"
