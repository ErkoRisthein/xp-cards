#!/bin/sh
# FreeCell HD — regenerate the generated art from its sources (run from anywhere).
#
#   tools/make_assets.sh [kings] [icon] [cursor] [cards] [check]     (default: kings icon cursor check)
#
#   kings   res/cards-svg/KS.svg -> res/king/src/king_{right,left,smile}.svg (derive_kings.py)
#           -> res/king/king_{right,left,smile}.png (1024x1024 RGBA, rsvg-convert + oxipng)
#   icon    res/king/src/king_left.svg -> res/icon/icon_card.svg -> res/freecell.ico
#   cursor  res/icon/make_cursor.py -> res/downarrow.cur
#   cards   res/cards-svg/<R><S>.svg -> res/cards/<R><S>.png (400x560 RGBA; docs/card-art.md §2, slow:
#           zopfli). The SVGs themselves come from the RevK generator URL in res/cards-src/.
#   check   structure checks of the .ico/.cur and PNG sizes/formats
#
# Needs: rsvg-convert (librsvg), Python 3 with Pillow ($PYTHON, default python3) and, for PNG
# optimisation, the pyoxipng module ($OXIPNG_PYTHON, default $PYTHON; without it the PNGs are
# written by ImageMagick `magick` at zlib level 9 instead).
set -eu
cd "$(dirname "$0")/.."
PYTHON=${PYTHON:-python3}
OXIPNG_PYTHON=${OXIPNG_PYTHON:-$PYTHON}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/fcassets.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

# optimise <src.png> <dst.png> <zopfli iterations, 0 = libdeflate level 12>
optimise() {
    if "$OXIPNG_PYTHON" -c 'import oxipng' 2>/dev/null; then
        "$OXIPNG_PYTHON" - "$1" "$2" "$3" <<'EOF'
import sys, oxipng
src, dst, it = sys.argv[1], sys.argv[2], int(sys.argv[3])
kw = dict(deflate=oxipng.Deflaters.zopfli(it)) if it else {}
oxipng.optimize(src, dst, level=6, strip=oxipng.StripChunks.all(), color_type_reduction=False,
                bit_depth_reduction=False, palette_reduction=False, grayscale_reduction=False, **kw)
EOF
    else
        magick "$1" -strip -define png:compression-level=9 -define png:compression-filter=5 "PNG32:$2"
    fi
}

kings() {
    "$PYTHON" res/king/src/derive_kings.py res/cards-svg/KS.svg res/king/src
    for v in right left smile; do
        rsvg-convert -w 1024 -h 1024 "res/king/src/king_$v.svg" -o "$TMP/king_$v.png"
        optimise "$TMP/king_$v.png" "res/king/king_$v.png" 0
        echo "res/king/king_$v.png"
    done
}

icon()   { "$PYTHON" res/icon/make_icon.py res/king/src/king_left.svg res/icon res/freecell.ico; echo res/freecell.ico; }
cursor() { "$PYTHON" res/icon/make_cursor.py res/downarrow.cur >/dev/null; echo res/downarrow.cur; }

cards() {
    for f in res/cards-svg/??.svg; do
        n=$(basename "$f" .svg)
        rsvg-convert -h 560 "$f" -o "$TMP/$n.png"
        optimise "$TMP/$n.png" "res/cards/$n.png" 15
        printf '%s ' "$n"
    done
    echo
}

check() {
    "$PYTHON" res/icon/check_icocur.py res/freecell.ico
    "$PYTHON" res/icon/check_icocur.py res/downarrow.cur
    "$PYTHON" - <<'EOF'
import glob, os
from PIL import Image
for pat, size in (('res/king/king_*.png', (1024, 1024)), ('res/cards/??.png', (400, 560))):
    files = sorted(glob.glob(pat))
    bad = [f for f in files if Image.open(f).size != size or Image.open(f).mode != 'RGBA']
    print('%-20s %2d files, %8d bytes%s' % (pat, len(files), sum(os.path.getsize(f) for f in files),
          ', BAD: %s' % bad if bad else ', OK'))
    if bad: raise SystemExit(1)
EOF
}

[ $# -gt 0 ] || set -- kings icon cursor check
for step in "$@"; do
    case $step in
        kings|icon|cursor|cards|check) $step ;;
        *) echo "unknown step: $step" >&2; exit 2 ;;
    esac
done
