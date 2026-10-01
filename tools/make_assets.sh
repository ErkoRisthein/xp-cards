#!/bin/sh
# FreeCell HD and Solitaire HD — regenerate the generated art from its sources (run from anywhere).
#
#   tools/make_assets.sh [kings] [icon] [cursor] [cards] [backs] [solicon] [check]
#                                                              (default: kings icon cursor solicon check)
#
#   kings   res/common/cards-svg/KS.svg -> res/freecell/king/src/king_{right,left,smile}.svg (derive_kings.py)
#           -> res/freecell/king/king_{right,left,smile}.png (1024x1024 RGBA, rsvg-convert + oxipng)
#   icon    res/freecell/king/src/king_left.svg -> res/freecell/icon/icon_card.svg -> res/freecell/freecell.ico
#   cursor  res/freecell/icon/make_cursor.py -> res/freecell/downarrow.cur
#   cards   res/common/cards-svg/<R><S>.svg -> res/common/cards/<R><S>.png (400x560 RGBA; docs/card-art.md §2, slow:
#           zopfli). The SVGs themselves come from the RevK generator URL in res/common/cards-src/ and are
#           kept unmodified; tools/edit_card_svg.py edits a copy before rasterising (docs/DESIGN.md
#           "Crispness decisions"): the rank-index stroke goes from the generator's 80 to
#           $INDEX_STROKE (default 130, closer to XP's bold index and readable at small sizes), and the
#           court-art linework (stroke #44F) becomes $COURT_MULT x wider (default 1.6) and
#           $COURT_COLOUR (default #223, dark navy), the court picture frame $COURT_COLOUR at
#           $COURT_FRAME_W units (default 1.5). The blue fills stay.
#   backs   res/solitaire/backs-src/<id>_<name>.svg -> res/solitaire/backs/<id>_<name>.png (Solitaire HD's 12
#           backs, 400x560 RGB; slow: zopfli). The SVGs are the RevK generator's, kept unmodified;
#           make_backs.py edits a copy (the pattern block scaled to a 5-unit inset, the Diamond lattice
#           doubled) and makes the edge band and corners opaque.
#   solicon res/solitaire/icon/make_icon.py -> res/solitaire/icon/icon_box.svg, res/solitaire/solitaire.ico
#   check   structure checks of the .ico/.cur and PNG sizes/formats
#
# Needs: rsvg-convert (librsvg), Python 3 with Pillow ($PYTHON, default python3) and, for PNG
# optimisation, the pyoxipng module ($OXIPNG_PYTHON, default $PYTHON; without it the PNGs are
# written by ImageMagick `magick` at zlib level 9 instead).
set -eu
cd "$(dirname "$0")/.."
PYTHON=${PYTHON:-python3}
OXIPNG_PYTHON=${OXIPNG_PYTHON:-$PYTHON}
INDEX_STROKE=${INDEX_STROKE:-130}
COURT_MULT=${COURT_MULT:-1.6}
COURT_COLOUR=${COURT_COLOUR:-#223}
COURT_FRAME_W=${COURT_FRAME_W:-1.5}
# Card layout for stacked legibility (docs/ROADMAP.md 2e): a named variant from
# tools/crisplab/candidates/stack.json (default XPLIKE: XP-like index/pip positions); stacklab.py applies
# the art edit above (index 130, courts 1.6x #223, frame 1.5) and then this layout.
CARD_VARIANT=${CARD_VARIANT:-XPLIKE}
CARD_LAYOUT=$("$PYTHON" -c 'import json,sys; print(json.dumps(next(v["layout"] for v in json.load(open("tools/crisplab/candidates/stack.json")) if v["name"]==sys.argv[1])))' "$CARD_VARIANT")
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
    "$PYTHON" res/freecell/king/src/derive_kings.py res/common/cards-svg/KS.svg res/freecell/king/src
    for v in right left smile; do
        rsvg-convert -w 1024 -h 1024 "res/freecell/king/src/king_$v.svg" -o "$TMP/king_$v.png"
        optimise "$TMP/king_$v.png" "res/freecell/king/king_$v.png" 0
        echo "res/freecell/king/king_$v.png"
    done
}

icon()   { "$PYTHON" res/freecell/icon/make_icon.py res/freecell/king/src/king_left.svg res/freecell/icon res/freecell/freecell.ico; echo res/freecell/freecell.ico; }
cursor() { "$PYTHON" res/freecell/icon/make_cursor.py res/freecell/downarrow.cur >/dev/null; echo res/freecell/downarrow.cur; }
backs()  { "$OXIPNG_PYTHON" res/solitaire/backs-src/make_backs.py res/solitaire/backs-src res/solitaire/backs; }
solicon() { "$PYTHON" res/solitaire/icon/make_icon.py res/solitaire/icon res/solitaire/solitaire.ico; echo res/solitaire/solitaire.ico; }

cards() {
    for f in res/common/cards-svg/??.svg; do
        n=$(basename "$f" .svg)
        "$PYTHON" tools/crisplab/stacklab.py svg "$f" "$TMP/$n.svg" --layout "$CARD_LAYOUT"
        rsvg-convert -h 560 "$TMP/$n.svg" -o "$TMP/$n.png"
        optimise "$TMP/$n.png" "res/common/cards/$n.png" 15
        printf '%s ' "$n"
    done
    echo
}

check() {
    "$PYTHON" res/freecell/icon/check_icocur.py res/freecell/freecell.ico
    "$PYTHON" res/freecell/icon/check_icocur.py res/freecell/downarrow.cur
    "$PYTHON" res/freecell/icon/check_icocur.py res/solitaire/solitaire.ico
    "$PYTHON" - <<'EOF'
import glob, os
from PIL import Image
# the backs are opaque: oxipng may store them RGB or palette
for pat, size, modes in (('res/freecell/king/king_*.png', (1024, 1024), ('RGBA',)),
                         ('res/common/cards/??.png', (400, 560), ('RGBA',)),
                         ('res/solitaire/backs/[0-9][0-9]_*.png', (400, 560), ('RGB', 'P'))):
    files = sorted(glob.glob(pat))
    bad = [f for f in files if Image.open(f).size != size or Image.open(f).mode not in modes or
           ('RGB' in modes and Image.open(f).convert('RGBA').getextrema()[3][0] != 255)]
    print('%-20s %2d files, %8d bytes%s' % (pat, len(files), sum(os.path.getsize(f) for f in files),
          ', BAD: %s' % bad if bad else ', OK'))
    if bad: raise SystemExit(1)
EOF
}

[ $# -gt 0 ] || set -- kings icon cursor solicon check
for step in "$@"; do
    case $step in
        kings|icon|cursor|cards|backs|solicon|check) $step ;;
        *) echo "unknown step: $step" >&2; exit 2 ;;
    esac
done
