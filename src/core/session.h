/*
 * FreeCell HD — the XP FreeCell controller (state machine), platform independent.
 *
 * Owns the board, game number, selection, king state, undo history, cheat, timers' state and every
 * flow of docs/xp-reference/rules.md §2.4–§10. The Win32 layer is a thin adapter:
 *
 *   WM_LBUTTONDOWN   fc_layout_hit(l, &s->board, x, y, fcs_has_selection(s) ? FC_HIT_DEST : FC_HIT_SOURCE,
 *                    &col, &pos) -> fcs_click(s, hit ? col : FCS_MISS, pos)
 *   WM_LBUTTONDBLCLK same hit test -> fcs_dblclick(s, col, pos)
 *   WM_RBUTTONDOWN/UP  FC_HIT_SOURCE hit -> fcs_rbutton_down(s, col, pos) / fcs_rbutton_up(s)
 *   WM_MOUSEMOVE     dest = FC_HIT_DEST hit (col/pos or FCS_MISS), on_card = FC_HIT_SOURCE hit result
 *                    -> SetCursor(fcs_mouse_move(s, col, pos, on_card))
 *   WM_MOUSEACTIVATE (HTCLIENT) -> fcs_mouse_activate(s)   (next left click / dblclick is swallowed)
 *   WM_CHAR          fcs_char(s, ch)
 *   WM_TIMER         fcs_timer(s, id)
 *   WM_COMMAND       fcs_command(s, FCS_CMD_NEW / SELECT / RESTART / UNDO / REDO / CHEAT); Statistics and
 *                    Options dialogs work on s->stats / s->opts directly (stats.h)
 *   WM_CLOSE         if (fcs_close(s)) DestroyWindow   (asks to resign, records the loss, saves options)
 *   WM_PAINT         draw s->board with fcs_view_state(s, ...) mapped onto FcView
 *
 * Deviations from XP (docs/DESIGN.md): unlimited undo (history of FcAction; a selecting click keeps it;
 * a pure deselect adds nothing; free cell -> free cell / home moves are undoable; cleared on every
 * deal, win and lose); redo (an extra: Undo moves the undone action onto a redo stack, Redo replays it
 * forwards with the same animation, including its autoplay steps; any newly committed action, deal,
 * win or lose clears it); the bugs of rules.md §11 are fixed (stale undo after a new deal, Select Game
 * cancel now leaves the current game completely untouched and records no loss, no y < 9 miss quirk —
 * a miss is FCS_MISS, no DBLCLK repost, cheat win keeps the home piles consistent); the two texts with
 * missing spaces are fixed; two New Games in the same second give different numbers.
 *
 * v1.1 extras (s->extras, all off by default = XP): a move counter and game clock for the menu bar
 * (moves = committed user actions: a supermove is 1, autoplay is not counted, Undo -1, Redo +1; the
 * clock starts at the first counted move after a deal and stops on win, loss or a new deal), the
 * standard supermove rule (f+1)*2^e, New Game from all 1..1000000 games, and the set of won deals
 * (always on; wondeals.h).
 */
#ifndef FC_SESSION_H
#define FC_SESSION_H

#include "game.h"
#include "stats.h"
#include "wondeals.h"

/* ---- Constants -------------------------------------------------------------------------------- */
#define FCS_MISS (-1)                    /* "col" of a click/move that hit nothing */

enum { FCS_KING_RIGHT = 0, FCS_KING_LEFT = 1, FCS_KING_BLANK = 2 };   /* == FC_KINGVIEW_* (render.h) */
enum { FCS_CURSOR_ARROW = 0,             /* IDC_ARROW */
       FCS_CURSOR_DOWNARROW = 1,         /* custom "DownArrow" cursor: legal tableau destination */
       FCS_CURSOR_UPARROW = 2,           /* IDC_UPARROW: legal free/home cell, any empty column */
       FCS_CURSOR_WAIT = 3 };            /* IDC_WAIT: keyboard column peek / replay in progress */
/* WM_COMMAND ids of the XP menu (resources.md) handled by fcs_command / posted via post_command. */
enum { FCS_CMD_NEW = 102, FCS_CMD_SELECT = 103, FCS_CMD_RESTART = 107, FCS_CMD_CHEAT = 114,
       FCS_CMD_UNDO = 115, FCS_CMD_REDO = 116 /* extra, not in XP */ };
enum { FCS_TIMER_FLASH = 2, FCS_TIMER_PEEK = 3 };          /* XP timer ids; 400 ms and 300 ms */
enum { FCS_MOVECOL_CANCEL = -1, FCS_MOVECOL_SINGLE = 0, FCS_MOVECOL_COLUMN = 1 };
enum { FCS_CHEAT_NONE = 0, FCS_CHEAT_LOSE = 1, FCS_CHEAT_WIN = 2 };   /* Ignore / Retry / Abort */
/* Extra text ids for fcs_string (XP dialog template / inline texts, not string-table entries). */
enum { FCS_STR_YOULOSE = 1001, FCS_STR_YOUWIN = 1002, FCS_STR_CHEAT_CAPTION = 1003,
       FCS_STR_CHEAT_TEXT = 1004 };

/* ---- UI callbacks ---------------------------------------------------------------------------------
 * Any callback may be NULL (then: no-op; prompts take the default noted). All modal prompts are
 * called synchronously from inside the session call that needs them. */
typedef struct FcSessionUI {
    void *ctx;
    /* Information box: MessageBeep(MB_ICONINFORMATION) + MessageBox(text, "FreeCell", MB_OK |
     * MB_ICONINFORMATION). id = 306 (illegal move) or 307 (too many cards; text already formatted). */
    void (*message)(void *ctx, int id, const char *text);
    /* String 305 "Do you want to resign this game?": MessageBeep(MB_ICONQUESTION) + MB_YESNO |
     * MB_ICONQUESTION. Return 1 = Yes. NULL = Yes. */
    int  (*confirm_resign)(void *ctx);
    /* "MoveCol" dialog: FCS_MOVECOL_COLUMN / _SINGLE / _CANCEL. NULL = column (default button). */
    int  (*ask_move_column)(void *ctx);
    /* "GameNum" dialog with the edit pre-filled with `initial` (SetDlgItemInt). Return 0 = Cancel;
     * 1 = OK with *value = GetDlgItemInt(.., bSigned = TRUE) (0 if not a number). The session
     * validates -2..1000000 (not 0) and re-shows the dialog with initial 0 when invalid. NULL = Cancel. */
    int  (*ask_game_number)(void *ctx, int initial, int *value);
    /* "YouWin" dialog; *select_game holds the checkbox (in: initial state, out: final state).
     * Return 1 = Yes. The big king is already requested (invalidate) — repaint before showing. */
    int  (*you_win)(void *ctx, int *select_game);
    /* "YouLose" dialog; *same_game = the "Same game" checkbox (in: 1, out: final). Return 1 = Yes. */
    int  (*you_lose)(void *ctx, int *same_game);
    /* Ctrl+Shift+F10 box (FCS_STR_CHEAT_*, MB_ABORTRETRYIGNORE | MB_ICONQUESTION):
     * return FCS_CHEAT_WIN (Abort) / FCS_CHEAT_LOSE (Retry) / FCS_CHEAT_NONE (Ignore). */
    int  (*cheat_prompt)(void *ctx);
    /* Animate one single-card step, synchronously (return when the card has landed). On entry the
     * session board still shows the state BEFORE the step: forward = 1 -> the card flies from
     * (src_col,src_pos) to (dst_col,dst_pos); forward = 0 (undo) -> from dst back to src. After it
     * returns the session applies the step, updates the king and calls invalidate() and, if it
     * changed, cards_left_changed(). Quick play: the UI just returns (s->opts.quick). */
    void (*animate_step)(void *ctx, const FcStep *st, int forward);
    void (*invalidate)(void *ctx);                     /* board/view changed: repaint everything */
    void (*set_title)(void *ctx, const char *title);   /* "FreeCell Game #%d" after a deal */
    void (*cards_left_changed)(void *ctx, int n);      /* "Cards Left: %u" in the menu bar */
    /* Game menu: Undo (115), Restart (107) and the extra Redo (116) enabled (1) or grayed (0). */
    void (*menu_state)(void *ctx, int undo_enabled, int restart_enabled, int redo_enabled);
    /* Start (ms > 0) or kill (ms == 0) Win32 timer `id`; on WM_TIMER call fcs_timer(s, id). */
    void (*set_timer)(void *ctx, int id, int ms);
    void (*flash)(void *ctx, int invert);              /* FlashWindow(hwnd, invert) */
    /* Follow-up command XP posts after YouWin/YouLose "Yes": PostMessage(WM_COMMAND, cmd) and later
     * call fcs_command(s, cmd). If NULL the session runs the command directly before returning. */
    void (*post_command)(void *ctx, int cmd);
    uint32_t (*now_seed)(void *ctx);                    /* time(NULL), seeds RandomGameNumber */
    /* Extras. Monotonic millisecond clock for the game timer (GetTickCount; wraps). NULL = 0. */
    uint32_t (*now_ms)(void *ctx);
    /* The move counter or the game clock changed (moved, reset, started, stopped): redraw "Moves" /
     * "Time" and start (fcs_clock_running) or stop the UI's once-a-second display refresh. */
    void (*status_changed)(void *ctx);
} FcSessionUI;

/* ---- Session state (read-only for the UI except opts, which the Options dialog edits) ---------- */
typedef struct FcSession {
    FcBoard   board;
    int       game_number;   /* current game; 0 = none / finished: board input is ignored */
    int       in_progress;   /* set at the deal; cleared on win/lose/resign (asks 305 when set) */
    int       dealt;         /* a game has been dealt since startup (Restart enabled) */
    int       select_flag;   /* last deal was via Select Game (YouWin checkbox default), §6.3 */
    int       cheat;         /* FCS_CHEAT_*, acts on the next committed click; reset on win/lose */
    int       sel, sel_col, sel_pos;   /* selection (sel = 0: none; then col/pos are -1) */
    int       king;          /* FCS_KING_* */
    int       big_king;      /* win: draw the big smiling king until the next deal */
    int       peek_col, peek_pos;      /* right-button or keyboard peek, -1 = none */
    int       kbd_peek;      /* keyboard column peek running (input blocked, wait cursor) */
    int       flash_left;
    int       swallow_click;
    int       busy;          /* inside a replay: input ignored */
    int       quiet;         /* > 0: illegal-move messages suppressed (key '0'), opts left alone */
    uint32_t  last_seed;
    int       seeded;
    FcAction **hist;         /* undo history, oldest first */
    int       nhist, hist_cap;
    FcAction **redo;         /* redo stack: undone actions, the next one to redo last */
    int       nredo, redo_cap;
    FcStats   stats;
    FcOptions opts;
    FcExtras  extras;        /* v1.1 extras; loaded and saved by the UI (its own store) */
    int       moves;         /* move counter (extra) */
    int       clock_running; /* game clock (extra): running since clock_start, else stopped at clock_ms */
    uint32_t  clock_start, clock_ms;
    FcWonDeals won;          /* won deals (extra), persisted through won_io */
    FcBlobIO  won_io;
    FcStore   store;
    FcSessionUI ui;
    FcAction  work;          /* scratch action being built */
} FcSession;

/* What the renderer needs (maps 1:1 onto FcView in render.h; hide_* is the UI's animation business). */
typedef struct FcsViewState {
    int sel_col, sel_pos;    /* -1 = none */
    int peek_col, peek_pos;  /* -1 = none */
    int king;                /* FCS_KING_* == FC_KINGVIEW_* */
    int big_king;
    int no_game;             /* nothing dealt yet (startup) */
} FcsViewState;

/* Initialise: empty board, no game (title "FreeCell", "Cards Left: 0", Restart and Undo grayed — the
 * UI sets those initial states itself), loads options and runs the entpack.ini migration from store.
 * ui/store are copied; either may be NULL. */
void fcs_init(FcSession *s, const FcSessionUI *ui, const FcStore *store);
void fcs_free(FcSession *s);                     /* frees the undo history and the redo stack */

/* ---- Input. (col, pos) targets: top row col 0, pos 0..7 (0..3 free cells, 4..7 home cells);
 * tableau col 1..8, pos = card index (ignored for destinations; -1 for an empty column);
 * col = FCS_MISS for nothing. Ignored while no game is active, during a replay or a keyboard peek. */
int  fcs_has_selection(const FcSession *s);      /* 1 -> hit-test with FC_HIT_DEST, else FC_HIT_SOURCE */
void fcs_click(FcSession *s, int col, int pos);
void fcs_dblclick(FcSession *s, int col, int pos);
void fcs_char(FcSession *s, int ch);             /* '0'..'9' (rules.md §4.7); others ignored */
void fcs_rbutton_down(FcSession *s, int col, int pos);
void fcs_rbutton_up(FcSession *s);
void fcs_mouse_activate(FcSession *s);
/* Mouse move: turns the king (over a free cell: left, over a home cell: right) and returns the
 * FCS_CURSOR_* to show. (col,pos) = FC_HIT_DEST hit or FCS_MISS; on_card = FC_HIT_SOURCE hit. */
int  fcs_mouse_move(FcSession *s, int col, int pos, int on_card);
int  fcs_cursor(const FcSession *s, int col, int pos, int on_card);   /* same, without the king */
void fcs_timer(FcSession *s, int id);
void fcs_command(FcSession *s, int cmd);
int  fcs_close(FcSession *s);                    /* 1 = OK to destroy the window */

/* ---- Queries ------------------------------------------------------------------------------------ */
int  fcs_undo_enabled(const FcSession *s);       /* history non-empty and the game active */
int  fcs_redo_enabled(const FcSession *s);       /* redo stack non-empty and the game active */
int  fcs_restart_enabled(const FcSession *s);
void fcs_view_state(const FcSession *s, FcsViewState *v);
/* Fixed texts: string-table ids 301..313 (307 with the missing space fixed), FCS_STR_* (YouLose text
 * with "lose. There" fixed). Returns "" for unknown ids. */
const char *fcs_string(int id);

/* ---- Extras ------------------------------------------------------------------------------------- */
int      fcs_moves(const FcSession *s);
uint32_t fcs_elapsed_ms(const FcSession *s);    /* game clock */
int      fcs_clock_running(const FcSession *s);
/* "m:ss" below an hour, "h:mm:ss" from an hour on (whole seconds, rounded down). */
void     fcs_format_time(uint32_t ms, char *buf, size_t n);
/* Load the won-deals set through io (kept for saving after each new won deal). Returns
 * fc_won_load's result: 1 loaded, 0 no file, -1 damaged (then the set starts empty). */
int      fcs_attach_won_deals(FcSession *s, const FcBlobIO *io);
int      fcs_won_before(const FcSession *s, int game);
uint32_t fcs_won_count(const FcSession *s);

#endif
