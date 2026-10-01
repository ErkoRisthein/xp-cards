#!/bin/sh
# readme_screenshot.sh - regenerate the README screenshot docs/screenshots/game1-1904x996.png from the
# native snapshot renderer (make snapshots): game #1 in the client area of a maximized 1920x1080 window.
# Lossless: alpha dropped (the board is opaque) and recompressed with ImageMagick (MAGICK, default magick).
set -eu
REPO=$(cd "$(dirname "$0")/../.." && pwd)
if [ -z "${MAGICK:-}" ]; then
    if command -v magick >/dev/null 2>&1; then MAGICK=magick; else MAGICK=convert; fi
fi
cd "$REPO"
make snapshots >/dev/null
mkdir -p docs/screenshots
"$MAGICK" build/snapshots/04_game1_1904x996.png -strip -alpha off -define png:color-type=2 \
    -define png:compression-level=9 -define png:compression-filter=5 -define png:compression-strategy=1 \
    docs/screenshots/game1-1904x996.png
ls -l docs/screenshots/game1-1904x996.png
