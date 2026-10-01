#!/bin/sh
# xvfb_wm.sh CMD [ARGS...] - run CMD on the current X display (e.g. inside xvfb-run) with a window manager.
# Wine's X11 driver leaves maximize/restore to the window manager; without one, a window that starts
# maximized cannot be restored (fchd_smoke.txt checks that). WM defaults to openbox.
set -eu
WM=${WM:-openbox}
"$WM" >/dev/null 2>&1 &
wm=$!
trap 'kill "$wm" 2>/dev/null || true' EXIT
sleep 1
"$@"
