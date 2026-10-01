#!/bin/sh
# Run the Solitaire HD end-to-end scenarios (tests/e2e/solhd_*.txt) under Wine. Used by
# `make e2e-solitaire`.
#   tests/e2e/run_solhd_e2e.sh [OUTDIR] [SCRIPT...]     (default: build/e2e/solhd, all solhd_*.txt)
#
# The scenarios share FreeCell HD's Wine prefix (FCHD_WINEPREFIX, see run_fchd_e2e.sh: the Mac
# driver in Retina mode, so a full-HD client fits). The app writes timings to OUTDIR/solhd_timing.log
# (SOLHD_TIMING_LOG). The scripts fix the deal with SOLHD_TIME (time(NULL) for the game: deal 64 first);
# SOLHD_NO_WARP keeps the keyboard interface from moving the host's pointer. For XP's own sol.exe under
# Wine, set WINEDLLOVERRIDES=cards=n: Wine's builtin cards.dll is used otherwise
# (docs/xp-reference/solitaire/layout.md).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
SCRATCH=/private/tmp/claude-502/-Users-erko-IdeaProjects-xp-cards/4b6e3af5-e647-41ff-a37b-eccdd0578cdc/scratchpad
: "${WINE:=$SCRATCH/tools/Wine Devel.app/Contents/Resources/wine/bin/wine}"
: "${FCHD_WINEPREFIX:=$SCRATCH/wineprefix-fchd-retina}"
OUT=${1:-$REPO/build/e2e/solhd}
[ $# -gt 0 ] && shift
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
WINEPREFIX=$FCHD_WINEPREFIX
export WINE WINEPREFIX

"$REPO/tools/wine/run.sh" --init
rm -f "$OUT/solhd_timing.log"
SOLHD_TIMING_LOG=$OUT/solhd_timing.log
# the keyboard interface must not move the host's real pointer (Wine passes SetCursorPos through)
SOLHD_NO_WARP=1
export SOLHD_TIMING_LOG SOLHD_NO_WARP
if [ $# -eq 0 ]; then set -- "$HERE"/solhd_*.txt; fi
rc=0
"$REPO/tools/wine/run.sh" -o "$OUT" "$@" || rc=$?
[ -f "$OUT/solhd_timing.log" ] && echo "timings: $OUT/solhd_timing.log"
exit $rc
