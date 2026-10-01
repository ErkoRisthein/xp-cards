/*
 * Card engine, Win32 — the main window's geometry: sizes, the first-run rect, the remembered
 * placement, borderless full screen.
 *
 * Every game window is WS_OVERLAPPEDWINDOW with a menu bar. The placement (an extra over XP, whose
 * games never remember it) is a REG_BINARY WINDOWPLACEMENT value in the game's own key
 * (HKCU\Software\xp-cards\<game> HD, value "WindowPlacement"), validated against the monitors on load.
 * Full screen (F11 / Alt+Enter in FreeCell HD) drops the caption and the sizing frame, keeps the menu
 * bar, and covers the monitor; leaving it restores the exact old placement and style.
 */
#ifndef CE_WINDOW_H
#define CE_WINDOW_H

#include <windows.h>

#define CE_PLACEMENT_VALUE L"WindowPlacement"

/* Outer size of a WS_OVERLAPPEDWINDOW with a menu for a cw x ch client. */
void ce_window_outer_size(int cw, int ch, int *w, int *h);

/* The work area of the primary monitor (the screen if unknown). */
void ce_window_primary_work_area(RECT *w);

/* First run: centred on the primary work area, the client at the largest scale s (8.0 down to 0.5 in
 * steps of 0.01) whose window fits 85% of the work area height and 95% of its width; client_for_scale
 * gives the client size for a scale (the board's natural proportions). */
typedef void (*CeClientForScale)(double s, int *w, int *h);
void ce_window_default_rect(RECT *out, CeClientForScale client_for_scale);

/* Validate a saved placement: its normal rect must be at least min_w x min_h (outer size) and lie on a
 * monitor (it is shrunk to and moved into that monitor's work area). WINDOWPLACEMENT uses workspace
 * coordinates (relative to the primary monitor's work area). show is WinMain's show command: a
 * minimized start is honoured, otherwise the window opens normal or maximized as saved. Fills *screen
 * with the normal rect in screen coordinates. Returns 0 if the placement is unusable. */
int  ce_placement_fix(WINDOWPLACEMENT *wp, int show, int min_w, int min_h, RECT *screen);

/* HKCU\<key>\WindowPlacement. */
int  ce_placement_load(const WCHAR *key, WINDOWPLACEMENT *wp);       /* 1 if a valid-looking one is stored */
void ce_placement_save_wp(const WCHAR *key, const WINDOWPLACEMENT *wp);
void ce_placement_save(const WCHAR *key, HWND hwnd);                  /* the window's current placement */

/* ---- full screen ------------------------------------------------------------------------------- */

typedef struct CeFullScreen {
    int             on;
    WINDOWPLACEMENT prev;       /* placement before full screen: restored on leaving, saved on exit */
    LONG            style;      /* window style before full screen */
} CeFullScreen;

int  ce_monitor_rect(HWND h, RECT *r);                 /* the monitor the window is (mostly) on */

/* Enter: remember the placement (prev, or the current one) and the style, drop the caption and the
 * sizing frame and cover the monitor (on is set before the style changes, so the window procedure
 * sees it during the resulting WM_SIZE). Returns 1 if it entered (not if already on, minimized or no
 * monitor). */
int  ce_fullscreen_enter(CeFullScreen *fs, HWND h, const WINDOWPLACEMENT *prev);
/* Leave: the old style and exactly the old placement (normal or maximized). Returns 1 if it left. */
int  ce_fullscreen_leave(CeFullScreen *fs, HWND h);
/* Cover the whole monitor again (WM_DISPLAYCHANGE); nothing when not in full screen. */
void ce_fullscreen_fit(const CeFullScreen *fs, HWND h);
/* Save the placement to come back to: in full screen the one before it, else the current one. */
void ce_fullscreen_save_placement(const CeFullScreen *fs, HWND h, const WCHAR *key);

#endif
