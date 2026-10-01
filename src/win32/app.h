/*
 * FreeCell HD — Win32 front end: shared state and the interfaces between its files.
 *
 *   main.c     WinMain, window class, placement, message loop, window procedure (input -> session)
 *   view.c     back buffer, incremental rendering, painting, card animation, cursors, "Cards Left"
 *   ui.c       the session's UI callbacks and every dialog (GameNum, MoveCol, YouWin, YouLose,
 *              Stats, Options)
 *   storage.c  registry: statistics/options store (XP's key and format) and the window placement
 *   help.c     Help > Contents / Search / How to Use Help (HtmlHelp, built-in fallback) and About
 *
 * One window, one thread: everything lives in the global g_app.
 */
#ifndef FC_WIN32_APP_H
#define FC_WIN32_APP_H

#include <windows.h>
#include <stdint.h>

#include "core/session.h"
#include "core/stats.h"
#include "gfx/cardset.h"
#include "gfx/layout.h"
#include "gfx/render.h"
#include "../../res/resource.h"

#define FC_CLASS_NAME   L"FreeCellHD"
#define WM_APP_SYNC     (WM_APP + 1)     /* deferred "session state changed": render the difference */

typedef struct App {
    HINSTANCE  inst;
    HWND       hwnd;
    HMENU      menu;
    HACCEL     accel;
    FcSession  s;
    FcCardSet *cs;

    /* layout and back buffer (32-bpp top-down DIB section, possibly larger than the client) */
    FcLayout   L;
    int        have_layout;
    unsigned   layout_gen;      /* bumped on every layout change (aborts a card flight) */
    HDC        memdc;
    HBITMAP    dib, old_bmp;
    uint32_t  *bits;
    int        buf_w, buf_h;    /* allocated DIB size */
    FcImage    fb;              /* client-sized view of the DIB */
    int        quality;         /* sprite quality of the current layout (0 while live-resizing) */
    int        in_sizemove, sized_in_loop;

    /* scratch DIB for animation frames (back buffer region + flying card) */
    HDC        sdc;
    HBITMAP    sdib, sold_bmp;
    uint32_t  *sbits;
    int        s_w, s_h;

    /* what the back buffer currently shows (for incremental re-rendering) */
    int        drawn_valid;
    FcBoard    drawn_board;
    FcView     drawn_view;
    int        dirty, sync_posted;

    /* animation: the card in flight is hidden (or, for a buried source, removed from a copy) */
    int        hide_col, hide_pos;
    int        use_anim_board;
    FcBoard    anim_board;

    /* cursors */
    HCURSOR    cur_arrow, cur_down, cur_up, cur_wait, cursor;
    int        rcapture;        /* right button captured for a peek */

    /* "Cards Left: N" in the menu bar */
    HFONT      menu_font;
    int        cards_left;
    int        cl_prev_left;    /* left edge of the last drawn text (to erase a longer old one) */
    int        menu_undo, menu_redo, menu_restart;   /* -1 = unknown */

    int        in_modal;        /* > 0 while one of our modal dialogs / message boxes is up */

    /* help */
    HMODULE    hh;
    int        hh_tried;
    void      *html_help;       /* HtmlHelpW */

    /* optional timing log (environment variable FCHD_TIMING_LOG = file name) */
    HANDLE     tlog;
    LARGE_INTEGER qpf;
} App;

extern App g_app;

/* view.c */
int    view_init(App *a);
void   view_free(App *a);
void   view_resize(App *a, int w, int h);            /* WM_SIZE (w, h > 0) */
void   view_exit_sizemove(App *a);
void   view_cardset_ready(App *a);
void   view_invalidate_all(App *a);                  /* layout unchanged, but repaint everything */
void   view_sync(App *a);                            /* render what changed, invalidate it */
void   view_sync_now(App *a);                        /* view_sync + UpdateWindow */
void   view_paint(App *a);                           /* WM_PAINT */
void   view_animate_step(App *a, const FcStep *st, int forward);
void   view_mouse_move(App *a, int x, int y);
void   view_refresh_cursor(App *a);
int    view_hit(App *a, int x, int y, int mode, int *col, int *pos);
void   menubar_font_update(App *a);
void   menubar_draw(App *a);
void   menubar_reset(App *a);                        /* the menu bar was repainted from scratch */
double now_ms(App *a);
void   tlog(App *a, const char *fmt, ...);

/* ui.c */
void   ui_make(App *a, FcSessionUI *ui);
void   ui_menu_init(App *a);
void   dlg_statistics(App *a);
void   dlg_options(App *a);
void   center_dialog(HWND dlg, HWND owner);
void   clamp_to_work_area(HWND near_wnd, RECT *r);
int    to_wide(const char *s, WCHAR *out, int n);
int    load_wstr(App *a, UINT id, WCHAR *out, int n, const WCHAR *fallback);

/* storage.c */
FcStore storage_store(void);
int    placement_load(WINDOWPLACEMENT *wp);
void   placement_save(HWND hwnd);

/* help.c */
void   help_contents(App *a);
void   help_search(App *a);
void   help_howto(App *a);
void   help_about(App *a);
void   help_shutdown(App *a);

#endif
