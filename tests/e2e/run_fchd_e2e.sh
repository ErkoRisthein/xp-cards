#!/bin/sh
# Run the FreeCell HD end-to-end scenarios (tests/e2e/fchd_*.txt) under Wine and build the review
# images. Used by `make e2e`.
#   tests/e2e/run_fchd_e2e.sh [OUTDIR] [SCRIPT...]     (default: build/e2e/fchd, all fchd_*.txt)
#
# The scenarios run in their own Wine prefix (FCHD_WINEPREFIX, default: wineprefix-fchd-retina in the
# scratchpad) with the Mac driver in Retina mode, so the virtual screen (3600 x 2338 on this Mac) is
# large enough for a full-HD client; the prefix is created on first use. The app writes timings to
# OUTDIR/fchd_timing.log (FCHD_TIMING_LOG).
# Review images: OUTDIR/compare_632x427_xp_vs_hd.png (the XP capture next to ours at 632 x 427) and
# OUTDIR/30_win_composite.png (the win screen with the YouWin dialog at its real position).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
SCRATCH=/private/tmp/claude-502/-Users-erko-IdeaProjects-xp-cards/4b6e3af5-e647-41ff-a37b-eccdd0578cdc/scratchpad
: "${WINE:=$SCRATCH/tools/Wine Devel.app/Contents/Resources/wine/bin/wine}"
: "${FCHD_WINEPREFIX:=$SCRATCH/wineprefix-fchd-retina}"
: "${MAGICK:=magick}"
XP_SHOT=${XP_SHOT:-$SCRATCH/research/layout/shots/s02_game1.png}
OUT=${1:-$REPO/build/e2e/fchd}
[ $# -gt 0 ] && shift
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
WINEPREFIX=$FCHD_WINEPREFIX
export WINE WINEPREFIX

"$REPO/tools/wine/run.sh" --init
if ! grep -q '"RetinaMode"="y"' "$WINEPREFIX/user.reg" 2>/dev/null; then
    echo "run_fchd_e2e: enabling the Mac driver's Retina mode in $WINEPREFIX"
    WINEDEBUG=-all "$WINE" reg add 'HKCU\Software\Wine\Mac Driver' /v RetinaMode /t REG_SZ /d y /f >/dev/null 2>&1
    "$(dirname "$WINE")/wineserver" -w
fi

rm -f "$OUT/fchd_timing.log"
# a copy of the exe whose first card face (a 400 x 560 PNG) cannot be decoded, for fchd_badart.txt
python3 - "$REPO/build/FreeCellHD.exe" "$OUT/FreeCellHD_badart.exe" <<'PY'
import struct, sys
d = bytearray(open(sys.argv[1], 'rb').read())
i = d.find(b'IHDR' + struct.pack('>II', 400, 560))
assert i > 0, 'no card PNG found'
d[i:i + 4] = b'IHDX'
open(sys.argv[2], 'wb').write(d)
PY
FCHD_TIMING_LOG=$OUT/fchd_timing.log
export FCHD_TIMING_LOG
if [ $# -eq 0 ]; then set -- "$HERE"/fchd_*.txt; fi
rc=0
"$REPO/tools/wine/run.sh" -o "$OUT" "$@" || rc=$?

# review images
if [ -f "$OUT/01_game1_632x427.png" ] && [ -f "$XP_SHOT" ]; then
    "$MAGICK" "$XP_SHOT" "$OUT/01_game1_632x427.png" -background '#808080' -splice 8x0 +append \
        "$OUT/compare_632x427_xp_vs_hd.png" && echo "png: $OUT/compare_632x427_xp_vs_hd.png"
fi
if [ -f "$OUT/30_win_window.png" ] && [ -f "$OUT/30_win_dialog.png" ] && [ -f "$OUT/fchd_timing.log" ]; then
    # last YouWin placement: "youwin dialog at X,Y size WxH (client origin CX,CY)"; the window capture
    # starts at the window's top-left, which is the client origin minus the frame / caption / menu
    line=$(grep 'youwin dialog at' "$OUT/fchd_timing.log" | tail -n 1 || true)
    if [ -n "$line" ]; then
        set -- $(echo "$line" | sed 's/.*dialog at \(-*[0-9]*\),\(-*[0-9]*\) size .*client origin \(-*[0-9]*\),\(-*[0-9]*\)).*/\1 \2 \3 \4/')
        ww=$("$MAGICK" identify -format '%w' "$OUT/30_win_window.png")
        wh=$("$MAGICK" identify -format '%h' "$OUT/30_win_window.png")
        cw=$("$MAGICK" identify -format '%w' "$OUT/30_win_client.png")
        ch=$("$MAGICK" identify -format '%h' "$OUT/30_win_client.png")
        fx=$(( (ww - cw) / 2 ))                     # side frame
        fy=$(( wh - ch - fx ))                      # caption + menu + top frame
        dx=$(( $1 - $3 + fx )); dy=$(( $2 - $4 + fy ))
        "$MAGICK" "$OUT/30_win_window.png" -background black -extent "$(( dx + 400 > ww ? dx + 400 : ww ))x$wh" \
            "$OUT/30_win_dialog.png" -geometry "+$dx+$dy" -composite "$OUT/30_win_composite.png" &&
            echo "png: $OUT/30_win_composite.png (dialog at +$dx+$dy in the window)"
    fi
fi
[ -f "$OUT/fchd_timing.log" ] && echo "timings: $OUT/fchd_timing.log"
exit $rc
