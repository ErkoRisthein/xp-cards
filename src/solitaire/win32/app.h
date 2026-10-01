/*
 * Solitaire HD — Win32 front end: shared state and the interfaces between its files. The generic
 * pieces (back buffer, animation clock, placement, full screen, registry, dialogs, help) are the
 * engine's (engine/win32/shell.h, docs/ENGINE.md); the game is src/solitaire (session, layout, render,
 * winanim).
 *
 *   main.c     WinMain, window class, placement, full screen, the window procedure (input -> session)
 *   view.c     layout, incremental rendering, painting, drag and drop (lifted stack / outline, the
 *              zip-back of a refused drop), the keyboard pointer, the win cascade
 *   status.c   XP's status bar: a child window at the bottom (menu help left, Score / Time right)
 *   ui.c       the session's UI callbacks, the dialogs (Options, Select Card Back, Deal Again?), menu
 *              graying, help and About, where settings are kept
 *
 * One window, one UI thread: everything lives in the global g_app.
 */
#ifndef SOL_WIN32_APP_H
#define SOL_WIN32_APP_H

#include <windows.h>
#include <stdint.h>

#include "engine/win32/shell.h"
#include "solitaire/layout.h"
#include "solitaire/render.h"
#include "solitaire/session.h"
#include "solitaire/winanim.h"
#include "../../../res/solitaire/resource.h"

#define SOL_CLASS_NAME   L"SolitaireHD"
#define SOL_STATUS_CLASS L"Stat"                  /* XP's status bar class name */
#define WM_APP_SYNC      (WM_APP + 1)             /* deferred "session state changed": render the difference */
#define SOL_ZIP_FRAME_MS 10                       /* the zip-back slide of a refused drop (layout.md §6.1) */

typedef struct App {
    HINSTANCE  inst;
    HWND       hwnd;
    HWND       status;          /* the status bar (NULL while the option is off) */
    HMENU      menu;
    HACCEL     accel;
    SolSession s;
    SolGfx    *gfx;             /* NULL until the card set is decoded (input waits for it) */

    /* layout and back buffer */
    SolLayout  L;
    int        have_layout;
    unsigned   layout_gen;      /* bumped on every layout change (stops animations) */
    CeBackBuf  bb;
    int        quality;         /* sprite quality of the current layout (0 while live-resizing) */
    int        status_h;        /* status bar window height: system font height + 2 (XP: 18) */

    /* what the back buffer shows (incremental rendering) */
    int        drawn_valid;
    SolBoard   drawn_board;
    SolView    drawn_view;
    int        sync_posted;
    int        freeze;          /* the win cascade owns the back buffer: no re-rendering */

    /* drag and drop (the session's drag, as the view shows it) */
    int        drag_on;
    int        drag_pile, drag_card;
    int        drag_outline;    /* "Outline dragging" for this drag */
    int        grab_dx, grab_dy;/* pointer minus the first dragged card's top-left */
    int        drag_x, drag_y;  /* top-left of the first dragged card */
    int        drag_w, drag_h;  /* the stack's size */
    CeImage   *drag_sprite;     /* normal dragging: the lifted stack, floated over the back buffer */
    int        press_valid, press_x, press_y;   /* a mouse press is being handled (the grab point) */
    int        lcapture;        /* SetCapture for a mouse drag */

    int        in_modal;        /* > 0 while one of our dialogs / message boxes is up */
    int        cascade_abort;   /* stop the win cascade (layout change, close) */
    CeAnimClock anim;
    CeFullScreen fs;
    CeStore    app_store;       /* HKCU\Software\xp-cards\Solitaire HD (extras) */

    /* the status bar's texts */
    WCHAR      status_left[128];
    HFONT      status_font;     /* MS Shell Dlg 9 pt bold (the right part) */

    int        menu_undo, menu_redo, menu_idle;   /* menu item states as last set (-1 = unknown) */

    /* test hooks (environment, read once): SOLHD_TIME replaces time(NULL), so deals and the random
     * back are reproducible; SOLHD_NO_WARP keeps the keyboard from moving the real pointer (the e2e
     * runs under Wine, where SetCursorPos moves the host's cursor) */
    int        have_fake_time;
    uint32_t   fake_time;
    int        no_warp;
} App;

extern App g_app;

/* view.c */
void   view_init(App *a);
void   view_free(App *a);
void   view_resize(App *a, int w, int h);            /* WM_SIZE (w, h > 0) */
void   view_relayout(App *a);                        /* same size, e.g. the status bar came or went */
void   view_exit_sizemove(App *a);
void   view_gfx_ready(App *a);
void   view_invalidate_all(App *a);
void   view_sync(App *a);                            /* render what changed, invalidate it */
void   view_sync_now(App *a);                        /* view_sync + UpdateWindow */
void   view_paint(App *a);                           /* WM_PAINT */
int    view_hit(App *a, int x, int y, int *pile, int *card);
void   view_drag_to(App *a, int x, int y);           /* the pointer moved while dragging */
void   view_zip_back(App *a);                        /* animate the dragged cards back (refused drop) */
void   view_kbd_cursor(App *a, int pile, int card, int dragging);
void   view_cascade(App *a);                         /* the win animation, until done or input */
void   view_anim_idle(App *a);

/* status.c */
int    status_register(HINSTANCE inst);
int    status_height(void);                          /* system font height + 2 */
void   status_show(App *a, int on);                  /* create / destroy, then lay out again */
void   status_place(App *a);                         /* XP: MoveWindow(-1, H - h + 1, W + 2, h) */
void   status_set_left(App *a, const WCHAR *text);   /* menu help, the win text ("" clears) */
void   status_update(App *a);                        /* score / time changed */
void   status_free(App *a);

/* ui.c */
void   ui_make(App *a, SolSessionUI *ui);
CeStore storage_xp_store(void);                      /* HKCU\Software\Microsoft\Solitaire (REG_DWORD) */
CeStore storage_app_store(void);                     /* HKCU\Software\xp-cards\Solitaire HD */
extern const WCHAR SOL_APP_KEY[];
void   menu_update(App *a);                          /* Undo / Redo / Deal / Deck / About graying */
void   dlg_options(App *a);
void   dlg_deck(App *a);
void   help_contents(App *a);
void   help_search(App *a);
void   help_howto(App *a);
void   help_about(App *a);
int    load_wstr(App *a, UINT id, WCHAR *out, int n, const WCHAR *fallback);
void   modal_begin(App *a);
void   modal_end(App *a);
uint32_t app_time(App *a);                           /* time(NULL), or SOLHD_TIME */

/* main.c */
void   fullscreen_set(App *a, int on);
void   after_input(App *a);                          /* menu states, capture, view sync */

#endif
