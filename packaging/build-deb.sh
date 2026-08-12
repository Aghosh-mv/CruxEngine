#!/usr/bin/env bash
# Builds a .deb package for FrostEngine.
# Prerequisites: cmake, g++, dpkg-deb, convert (imagemagick).
# Output: dist/frostengine_<version>_amd64.deb
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
VERSION="${1:-0.1.0}"
ARCH="amd64"
PKG="frostengine"
DIST="$ROOT/dist"
STAGE="$DIST/stage"

echo "== Building FrostEngine (Release) =="
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$ROOT/build" -j"$(nproc)" >/dev/null

rm -rf "$STAGE"
mkdir -p "$STAGE/usr/bin"
mkdir -p "$STAGE/usr/share/applications"
mkdir -p "$STAGE/usr/share/icons/hicolor/256x256/apps"
mkdir -p "$STAGE/DEBIAN"

install -m 755 "$ROOT/build/FrostGame" "$STAGE/usr/bin/frostgame"
install -m 644 "$HERE/frostengine.desktop" "$STAGE/usr/share/applications/frostengine.desktop"
install -m 644 "$HERE/frostengine.png" "$STAGE/usr/share/icons/hicolor/256x256/apps/frostengine.png"

SIZE_KIB=$(du -sk "$STAGE" | awk '{print $1}')

cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Section: games
Priority: optional
Architecture: $ARCH
Installed-Size: $SIZE_KIB
Depends: libc6, libx11-6, libgl1
Maintainer: FrostEngine <frostengine@localhost>
Homepage: https://github.com/Aghosh-mv/CruxEngine
Description: FrostEngine wind-glider demo
 A lightweight 3D wind-glider flight demo with PBR rendering,
 terrain streaming, water, and post-processing. Ships with a
 native OpenGL renderer and zero runtime dependencies beyond
 libGL and libX11.
EOF

echo "== Packing .deb =="
mkdir -p "$DIST"
dpkg-deb --build --root-owner-group "$STAGE" "$DIST/${PKG}_${VERSION}_${ARCH}.deb" >/dev/null

echo "Done: $DIST/${PKG}_${VERSION}_${ARCH}.deb"
