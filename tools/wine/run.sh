#!/bin/sh
# run.sh - run fcdrive.exe end-to-end scripts under Wine in a dedicated prefix.
#
#   tools/wine/run.sh [-o OUTDIR] [-k] [-v] SCRIPT [SCRIPT...]   run scripts (captures -> OUTDIR, BMP -> PNG)
#   tools/wine/run.sh --build                                    only (re)build fcdrive.exe + fchook.dll
#   tools/wine/run.sh --init                                     only create/upgrade the Wine prefix
#   tools/wine/run.sh --exec ARGS...                             run fcdrive.exe ARGS... (e.g. -e "cmd")
#
# Environment: WINE (wine binary), WINEPREFIX (default: the e2e prefix below), CC32 (i686 gcc),
#              MAGICK (ImageMagick), FCDRIVE_KEEP_BMP=1 (keep BMPs next to the PNGs).
# Scripts see ${OUT} (the output dir, as a Unix path) and ${REPO} (the repository root).
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
SCRATCH=/private/tmp/claude-502/-Users-erko-IdeaProjects-xp-cards/4b6e3af5-e647-41ff-a37b-eccdd0578cdc/scratchpad
: "${WINE:=$SCRATCH/tools/Wine Devel.app/Contents/Resources/wine/bin/wine}"
: "${WINEPREFIX:=$SCRATCH/wineprefix-e2e}"
: "${CC32:=i686-w64-mingw32-gcc}"
: "${MAGICK:=magick}"
WINEDEBUG=-all
MVK_CONFIG_LOG_LEVEL=0  # silence MoltenVK start-up chatter on macOS
export MVK_CONFIG_LOG_LEVEL
export WINEPREFIX WINEDEBUG REPO
BIN="$REPO/build/wine"
WINESERVER="$(dirname "$WINE")/wineserver"

die() { echo "run.sh: $*" >&2; exit 2; }

build() {
    mkdir -p "$BIN"
    if [ ! -f "$BIN/fchook.dll" ] || [ "$HERE/fchook.c" -nt "$BIN/fchook.dll" ] || [ "$HERE/fcipc.h" -nt "$BIN/fchook.dll" ]; then
        echo "run.sh: building fchook.dll"
        $CC32 -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -mcrtdll=msvcrt-os -shared \
            -static-libgcc -Wl,--kill-at -o "$BIN/fchook.dll" "$HERE/fchook.c" -lgdi32 -luser32
    fi
    if [ ! -f "$BIN/fcdrive.exe" ] || [ "$HERE/fcdrive.c" -nt "$BIN/fcdrive.exe" ] || [ "$HERE/fcipc.h" -nt "$BIN/fcdrive.exe" ]; then
        echo "run.sh: building fcdrive.exe"
        $CC32 -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -mcrtdll=msvcrt-os -municode \
            -static -o "$BIN/fcdrive.exe" "$HERE/fcdrive.c" -luser32 -lgdi32 -ladvapi32 -lshell32
    fi
}

init_prefix() {
    [ -x "$WINE" ] || die "wine not found at $WINE (set WINE=...)"
    if [ ! -f "$WINEPREFIX/system.reg" ]; then
        echo "run.sh: creating Wine prefix $WINEPREFIX (first boot takes ~20 s)"
        mkdir -p "$WINEPREFIX"
        "$WINE" wineboot -i >/dev/null 2>&1 || true
        "$WINESERVER" -w
    fi
    # Windows version XP (GetVersionEx -> 5.1.2600 SP3); idempotent.
    if ! grep -q '"Version"="winxp"' "$WINEPREFIX/user.reg" 2>/dev/null; then
        echo "run.sh: setting Windows version to winxp"
        "$WINE" reg add 'HKEY_CURRENT_USER\Software\Wine' /v Version /t REG_SZ /d winxp /f >/dev/null 2>&1
        "$WINESERVER" -w
    fi
}

OUT=$REPO/build/e2e
VERBOSE=
KEEP=${FCDRIVE_KEEP_BMP:-}
case "${1:-}" in
    --build) build; exit 0 ;;
    --init) init_prefix; exit 0 ;;
    --exec) shift; build; init_prefix; exec "$WINE" "$BIN/fcdrive.exe" "$@" ;;
    ""|-h|--help) sed -n '2,12p' "$0"; exit 2 ;;
esac
while [ $# -gt 0 ]; do
    case "$1" in
        -o) OUT=$2; shift 2 ;;
        -k) KEEP=1; shift ;;
        -v) VERBOSE=-v; shift ;;
        -*) die "unknown option $1" ;;
        *) break ;;
    esac
done
[ $# -gt 0 ] || die "no script given"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
export OUT
build
init_prefix

rc=0
for script in "$@"; do
    [ -f "$script" ] || die "no such script: $script"
    script=$(cd "$(dirname "$script")" && pwd)/$(basename "$script")
    stamp="$OUT/.stamp.$$"
    : > "$stamp"
    echo "=== $script"
    "$WINE" "$BIN/fcdrive.exe" $VERBOSE -o "$OUT" "$script" </dev/null || rc=$?
    # convert captures written during this run to PNG
    find "$OUT" -maxdepth 2 -name '*.bmp' -newer "$stamp" | while read -r bmp; do
        png=${bmp%.bmp}.png
        if "$MAGICK" "$bmp" "$png" 2>/dev/null; then
            [ -n "$KEEP" ] || rm -f "$bmp"
            echo "png: $png"
        else
            echo "run.sh: could not convert $bmp" >&2
        fi
    done
    rm -f "$stamp"
done
exit $rc
