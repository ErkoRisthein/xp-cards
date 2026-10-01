#!/bin/sh
# run.sh - run fcdrive.exe end-to-end scripts under Wine in a dedicated prefix.
#
#   tools/wine/run.sh [-o OUTDIR] [-k] [-v] SCRIPT [SCRIPT...]   run scripts (captures -> OUTDIR, BMP -> PNG)
#   tools/wine/run.sh --build                                    only (re)build fcdrive.exe + fchook.dll
#   tools/wine/run.sh --init                                     only create/upgrade the Wine prefix
#   tools/wine/run.sh --exec ARGS...                             run fcdrive.exe ARGS... (e.g. -e "cmd")
#
# Environment: WINE (wine binary), WINESERVER, WINEPREFIX (default: the e2e prefix below), CC32 (i686 gcc),
#              MAGICK (ImageMagick 7 `magick` or 6 `convert`), FCDRIVE_KEEP_BMP=1 (keep BMPs next to the PNGs).
# Scripts see ${OUT} (the output dir, as a Unix path) and ${REPO} (the repository root).
# Defaults: on the dev Mac, the Wine Devel.app and the prefix in the scratchpad below; elsewhere (Linux, CI)
# `wine`/`wineserver` from PATH and the prefix build/wineprefix-e2e.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
SCRATCH=/private/tmp/claude-502/-Users-erko-IdeaProjects-xp-cards/4b6e3af5-e647-41ff-a37b-eccdd0578cdc/scratchpad
MAC_WINE="$SCRATCH/tools/Wine Devel.app/Contents/Resources/wine/bin/wine"
if [ -z "${WINE:-}" ]; then
    if [ -x "$MAC_WINE" ]; then WINE=$MAC_WINE; else WINE=$(command -v wine || echo wine); fi
fi
if [ -z "${WINEPREFIX:-}" ]; then
    if [ -d "$SCRATCH" ]; then WINEPREFIX=$SCRATCH/wineprefix-e2e; else WINEPREFIX=$REPO/build/wineprefix-e2e; fi
fi
: "${CC32:=i686-w64-mingw32-gcc}"
if [ -z "${MAGICK:-}" ]; then
    if command -v magick >/dev/null 2>&1; then MAGICK=magick; else MAGICK=convert; fi
fi
WINEDEBUG=-all
MVK_CONFIG_LOG_LEVEL=0  # silence MoltenVK start-up chatter on macOS
export MVK_CONFIG_LOG_LEVEL
export WINEPREFIX WINEDEBUG REPO
BIN="$REPO/build/wine"
if [ -z "${WINESERVER:-}" ]; then
    if [ -x "$(dirname "$WINE")/wineserver" ]; then WINESERVER="$(dirname "$WINE")/wineserver"
    else WINESERVER=$(command -v wineserver || echo wineserver); fi
fi
# -mcrtdll needs GCC 14+; older mingw-w64 GCCs (Ubuntu 24.04) link msvcrt.dll by default anyway.
CRT32=
if $CC32 -mcrtdll=msvcrt-os -E -x c /dev/null >/dev/null 2>&1; then CRT32=-mcrtdll=msvcrt-os; fi

die() { echo "run.sh: $*" >&2; exit 2; }

build() {
    mkdir -p "$BIN"
    if [ ! -f "$BIN/fchook.dll" ] || [ "$HERE/fchook.c" -nt "$BIN/fchook.dll" ] || [ "$HERE/fcipc.h" -nt "$BIN/fchook.dll" ]; then
        echo "run.sh: building fchook.dll"
        $CC32 -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type $CRT32 -shared \
            -static-libgcc -Wl,--kill-at -o "$BIN/fchook.dll" "$HERE/fchook.c" -lgdi32 -luser32
    fi
    if [ ! -f "$BIN/fcdrive.exe" ] || [ "$HERE/fcdrive.c" -nt "$BIN/fcdrive.exe" ] || [ "$HERE/fcipc.h" -nt "$BIN/fcdrive.exe" ]; then
        echo "run.sh: building fcdrive.exe"
        $CC32 -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type $CRT32 -municode \
            -static -o "$BIN/fcdrive.exe" "$HERE/fcdrive.c" -luser32 -lgdi32 -ladvapi32 -lshell32
    fi
}

init_prefix() {
    [ -x "$WINE" ] || die "wine not found at $WINE (set WINE=...)"
    if [ ! -f "$WINEPREFIX/system.reg" ]; then
        echo "run.sh: creating Wine prefix $WINEPREFIX (first boot takes ~20 s)"
        mkdir -p "$WINEPREFIX"
        # no Mono/Gecko install prompts (they would block a headless run)
        WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-mscoree,mshtml=}" "$WINE" wineboot -i >/dev/null 2>&1 || true
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
    ""|-h|--help) sed -n '2,13p' "$0"; exit 2 ;;
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
