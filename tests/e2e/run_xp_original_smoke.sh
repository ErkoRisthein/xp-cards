#!/bin/sh
# Run tests/e2e/xp_original_smoke.txt against the original XP freecell.exe in a scratch work dir.
#   tests/e2e/run_xp_original_smoke.sh [OUTDIR]     (captures default to build/e2e/xp_original)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
SCRATCH=/private/tmp/claude-502/-Users-erko-IdeaProjects-xp-cards/4b6e3af5-e647-41ff-a37b-eccdd0578cdc/scratchpad
FCWORK=${FCWORK:-$SCRATCH/e2e-work/xp-original}
mkdir -p "$FCWORK"
cp "$REPO/freecell.exe" "$REPO/cards.dll" "$FCWORK/"
export FCWORK
exec "$REPO/tools/wine/run.sh" -o "${1:-$REPO/build/e2e/xp_original}" "$HERE/xp_original_smoke.txt"
