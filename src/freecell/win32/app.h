/*
 * FreeCell HD — Win32 front end: shared state and the interfaces between its files. The generic
 * pieces (back buffer, animation clock, menu-bar text, placement, full screen, registry, dialogs,
 * help, worker thread) are the engine's (engine/win32/shell.h, docs/ENGINE.md).
 *
 *   main.c     WinMain, window class, placement, message loop, window procedure (input -> session)
 *   view.c     incremental rendering, painting, card animation, cursors, "Cards Left"
 *   ui.c       the session's UI callbacks and every dialog (GameNum, MoveCol, YouWin, YouLose,
 *              Stats, Options)
 *   storage.c  where things are kept: statistics/options (XP's key and format), the extras and the
 *              window placement (our own key), the won-deals file (%APPDATA%)
 *   help.c     Help > Contents / Search / How to Use Help (HtmlHelp, built-in fallback) and About
 *   solve.c    the background solver for Hint and the unwinnable warning (v1.2): one worker thread
 *
 * One window, one UI thread: everything lives in the global g_app. The solver's worker thread only
 * sees its own copies of boards (solve.c).
 */
#ifndef FC_WIN32_APP_H
#define FC_WIN32_APP_H

#include <windows.h>
#include <stdint.h>

#include "engine/win32/shell.h"
#include "freecell/layout.h"
#include "freecell/render.h"
#include "freecell/session.h"
#include "freecell/sprites.h"
#include "freecell/stats.h"
#include "../../../res/freecell/resource.h"

#define FC_CLASS_NAME   L"FreeCellHD"
#define WM_APP_SYNC     (WM_APP + 1)     /* deferred "session state changed": render the difference */
#define WM_APP_SOLVED   (WM_APP + 2)     /* a solver job finished (lParam = the job; 0 = retry delivery) */

struct SolveJob;
#define FC_TIMER_CLOCK  10               /* extra: once-a-second refresh of "Time: m:ss" in the menu bar */
#define FC_TIMER_PULSE  11               /* 2d, Enhanced animations: frames of the hint's soft pulse */

typedef struct App {
    HINSTANCE  inst;
    HWND       hwnd;
    HMENU      menu;
    HACCEL     accel;
    FcSession  s;
    FcCardSet *cs;

    /* layout and back buffer (bb.fb: the client-sized view of the DIB; bb.bits NULL until the first
     * WM_SIZE; bb also holds the live-resize state and the scratch DIB for animation frames) */
    FcLayout   L;
    int        have_layout;
    unsigned   layout_gen;      /* bumped on every layout change (aborts a card flight) */
    CeBackBuf  bb;
    int        quality;         /* sprite quality of the current layout (0 while live-resizing) */

    /* what the back buffer currently shows (for incremental re-rendering) */
    int        drawn_valid;
    FcBoard    drawn_board;
    FcView     drawn_view;
    int        dirty, sync_posted;

    /* animation (2d): the cards in the air (engine/win32/anim.h). The back buffer shows the board
     * without them: a card not yet moved in the session's board is hidden at its source (a buried one
     * removed from the copy anim_board), one already moved is hidden at its destination (FcView hide_*) */
    CeFlights  fl;
    unsigned   fl_gen;          /* layout_gen when the flights began (a new layout drops them) */
    FcBoard    anim_board;
    unsigned   drawn_deals;     /* s.as.deals when the back buffer was last synced (a new deal: its animation) */
    int        anim_slow;       /* test hook FCHD_ANIM_SLOW: every animation that many times slower (1) */

    /* 2d: the Finish button on the table (shown while Game > Finish is enabled), its look as drawn */
    CeTableButton fbtn;
    int        finish_avail;
    int        btn_drawn;       /* 0: not in the back buffer; 1 + hot + 2 pressed */

    /* 2d, Enhanced animations: the hint's pulse (the cell / cards it fades in or out on) */
    int        pulse_col, pulse_pos, pulse_from, pulse_to, pulse_timer;
    DWORD      pulse_t0;

    /* cursors */
    HCURSOR    cur_arrow, cur_down, cur_up, cur_wait, cur_busy, cursor;   /* busy: IDC_APPSTARTING */
    int        rcapture;        /* right button captured for a peek */

    /* "Cards Left: N" in the menu bar */
    CeMenuBarText mbt;
    int        cards_left;
    int        menu_undo, menu_redo, menu_restart;   /* -1 = unknown */
    int        menu_hint, menu_finish;
    int        menu_undoall;    /* v1.4 */

    int        in_modal;        /* > 0 while one of our modal dialogs / message boxes is up */
    CeAnimClock anim;           /* timeBeginPeriod(1) is in effect while cards are flying */

    /* v1.1 extras */
    CeStore    app_store;       /* HKCU\Software\xp-cards\FreeCell HD: the extras (REG_DWORD) */
    int        clock_timer;     /* FC_TIMER_CLOCK is set */
    CeFullScreen fs;            /* borderless, covering the monitor (the menu bar stays) */

    /* v1.2: a finished solver job waiting until no session call or dialog is in progress */
    struct SolveJob *solve_ready;

    /* v1.4: drag and drop (extras.drag_drop) */
    int        press_armed;     /* the button is down after a press that may become a drag */
    int        press_on_sel;    /* ... on the pile already selected (released unmoved: XP's click on it) */
    int        press_x, press_y, press_col, press_pos;   /* where (the FC_HIT_SOURCE hit) */
    int        lcapture;        /* SetCapture for the press / drag */
    int        drag_on;         /* cards are lifted and follow the pointer */
    int        drag_col, drag_first;   /* which: the top-row slot, or a column from index drag_first */
    CeDrag     drag;            /* the lifted cards' sprite and rect (engine/win32/drag.h) */
} App;

extern App g_app;

/* view.c */
int    view_init(App *a);
void   view_free(App *a);
void   view_resize(App *a, int w, int h);            /* WM_SIZE (w, h > 0) */
void   view_exit_sizemove(App *a);
void   view_cardset_ready(App *a);
/* Options OK (2c): "Large print cards" changed: switch the card set's faces and lay out again with their
 * column step (a full re-render); nothing when it did not change. */
void   view_large_print(App *a);
void   view_invalidate_all(App *a);                  /* layout unchanged, but repaint everything */
void   view_sync(App *a);                            /* render what changed, invalidate it */
void   view_sync_now(App *a);                        /* view_sync + UpdateWindow */
void   view_paint(App *a);                           /* WM_PAINT */
void   view_animate_step(App *a, const FcStep *st, int forward);
void   view_anim_idle(App *a);                      /* the cards in the air land; then the timer back to normal */
void   view_anim_drop(App *a);                      /* forget the cards in the air (the window is going) */
void   view_pulse_tick(App *a);                     /* FC_TIMER_PULSE */
int    view_button_mouse(App *a, UINT m, LPARAM lp); /* the Finish button's mouse; 1 = it took the message */
void   view_finish_avail(App *a, int on);           /* Game > Finish enabled or not (the button) */
void   view_mouse_move(App *a, int x, int y);
int    view_drag_begin(App *a, int col, int first, int px, int py);   /* lift the cards (v1.4) */
void   view_drag_move(App *a, int x, int y);         /* the lifted cards follow the pointer */
void   view_drag_zip_back(App *a);                   /* a refused drop: they slide back */
void   view_drag_end(App *a);                        /* put them down (the board shows them again) */
void   view_refresh_cursor(App *a);
int    view_hit(App *a, int x, int y, int mode, int *col, int *pos);
void   menubar_draw(App *a);                         /* ce_menubar_draw with "Cards Left" etc. */
void   clock_update(App *a);                         /* start / re-arm / stop FC_TIMER_CLOCK */

/* ui.c */
void   ui_make(App *a, FcSessionUI *ui);
void   ui_menu_init(App *a);
void   dlg_statistics(App *a);
void   dlg_options(App *a);
void   modal_begin(App *a);                          /* a modal dialog / message box opens ... */
void   modal_end(App *a);                            /* ... and closes (then a held solver job is retried) */
int    load_wstr(App *a, UINT id, WCHAR *out, int n, const WCHAR *fallback);   /* ce_load_wstr */

/* storage.c */
extern const WCHAR FC_APP_KEY[];                     /* HKCU\Software\xp-cards\FreeCell HD: extras, placement */
CeStore storage_store(void);                         /* XP's key and format: statistics, options */
CeStore storage_app_store(void);                     /* our key, REG_DWORD values (the extras) */
CeBlobIO storage_won_io(void);                       /* %APPDATA%\xp-cards\FreeCell HD\won-deals.bin */
void   storage_won_set_aside(void);                  /* a damaged file -> won-deals.bad */

/* main.c */
void   fullscreen_set(App *a, int on);

/* solve.c */
void   solver_request(App *a, uint32_t id, const FcBoard *b, int standard);
void   solver_cancel(App *a);
void   solver_received(App *a, LPARAM lp);           /* WM_APP_SOLVED */
void   solver_deliver(App *a);                       /* hand a held job to the session when it can take it */
void   solver_shutdown(App *a);                      /* WM_DESTROY: stop and join the worker */

/* help.c */
void   help_contents(App *a);
void   help_search(App *a);
void   help_howto(App *a);
void   help_about(App *a);
void   help_shutdown(App *a);

#endif
