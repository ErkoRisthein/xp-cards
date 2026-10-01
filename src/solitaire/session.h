/*
 * Solitaire HD — the XP Solitaire controller (state machine), platform independent.
 *
 * Owns the board, the deal (seed and rand state), the drag, the keyboard cursor, the score, the clock,
 * the undo/redo history, the options and the win flow of docs/xp-reference/solitaire/rules.md. The
 * Win32 layer is a thin adapter; hit-testing and geometry are the layout's (XP: the topmost card whose
 * rectangle holds the point, and the drop target = the first pile in index order whose top card (or
 * empty slot) overlaps the dragged card and accepts it):
 *
 *   WM_LBUTTONDOWN     SetCapture; r = sol_press(s, pile or SOL_MISS, card index or -1, mods)
 *                      (mods: SOL_MOD_*, from GetKeyState; Ctrl+Alt+Shift on the stock draws one card)
 *                      r == SOL_PRESS_DRAG: a drag began (s->drag_*), remember the grab offset,
 *                      sol_drag_over(s, -1) (no target until the cards move)
 *   WM_MOUSEMOVE       while dragging: move the image, then sol_drag_over(s, first pile in index
 *                      order that overlaps and sol_can_drop_on(s, pile), else -1)
 *   WM_LBUTTONUP       ReleaseCapture; if dragging: sol_drop(s, s->target); 0 = refused: zip the cards
 *                      back (read s->drag_* before the call)
 *   WM_LBUTTONDBLCLK   sol_dblclick(s, pile, index, mods) (same results as sol_press)
 *   WM_RBUTTONDOWN     sol_autoplay(s) (no mouse capture; refused while dragging — an XP bug fixed)
 *   WM_KEYDOWN         sol_key(s, wParam, mods); move the pointer when ui.kbd_cursor is called (and
 *                      the dragged cards with it: view_drag_to -> sol_drag_over). Enter / Space while
 *                      dragging drops on sol_key_drop_target (XP: MouseUp, the highlighted target);
 *                      zip the cards back first when it is -1
 *   WM_KILLFOCUS, Esc  sol_cancel_drag(s)
 *   WM_TIMER           sol_timer(s, id)  (ui.set_timer starts/stops SOL_TIMER_CLOCK, 250 ms)
 *   WM_SIZE            sol_set_minimized(s, wParam == SIZE_MINIMIZED)  (the clock pauses)
 *   WM_INITMENU        Undo: sol_undo_enabled; Redo: sol_redo_enabled; Deal, Deck, About: sol_idle
 *   WM_COMMAND         sol_command(s, SOL_CMD_DEAL / UNDO / REDO / FORCEWIN); the Options dialog:
 *                      sol_apply_options(s, &edited); the Deck dialog: sol_set_back(s, index)
 *   WM_PAINT           draw s->board when sol_board_visible(s): the waste fan from s->waste_fan,
 *                      the empty stock from sol_stock_symbol, the dragged cards s->drag_* (normal
 *                      dragging: drawn at the pointer, not in their pile; outline dragging: in their
 *                      pile, plus the inverted s->target), the back s->back; status bar:
 *                      sol_score_text / sol_seconds, s->opts.status_bar / timed / scoring
 *
 * Deviations from XP (docs/DESIGN.md):
 *   - Unlimited undo + redo (extras). Every committed action is one history entry: a drop, a
 *     double-click home, a stock draw, a recycle, turning a card over (XP: kills Undo) and a whole
 *     right-click / Ctrl+A autoplay (XP: one record per card). Undo restores the position, score and
 *     recycle count from before the action and then charges XP's Undo event (Standard -2, floored at
 *     0) — for every action undone. Redo performs the undone action again and scores it from the
 *     current score (so the Undo charge stays). The first Undo after an action gives back the clock's
 *     Standard penalties since it, as XP's single Undo does; Undoing further keeps the penalties the
 *     clock took after the action being undone (else Undo all + Redo all would erase the time
 *     penalty of the whole game). A new action, a deal and a win clear the redo stack;
 *     a deal and a win clear the history. Undoing a Vegas recycle gives the pass back, as in XP.
 *   - XP bugs fixed (rules.md §11): Esc / focus loss during a drag clears the drop target and records
 *     nothing, and every drop is checked again; a foundation gives only its top card (no run picked up
 *     from the pile's 3-D edge, also not with the keyboard cursor); autoplay is refused while a card is
 *     being dragged.
 *   - Two deals in the same second (time(NULL) & 0x7FFF) would be identical in XP: the next seed is
 *     taken instead. A missing Back value picks a uniform random back (XP: 54 twice as likely, 65
 *     never). sCurrency is never read ("$"; XP crashed on short values).
 */
#ifndef SOL_SESSION_H
#define SOL_SESSION_H

#include "engine/store.h"
#include "game.h"

#define SOL_MISS (-1)                    /* "pile" of a press that hit no pile */

/* WM_COMMAND ids (XP's: resources.md §2.1), handled by sol_command. */
enum { SOL_CMD_DEAL = 1000, SOL_CMD_UNDO = 1001, SOL_CMD_FORCEWIN = 1010 /* Alt+Shift+2 */,
       SOL_CMD_REDO = 1101 /* extra (Ctrl+Y), not in XP */ };
#define SOL_TIMER_CLOCK 666              /* XP's timer id */
#define SOL_TICK_MS     250              /* XP's tick; the clock counts ticks, shows ticks >> 2 seconds */
#define SOL_TICKS_MAX   0x7FFE

/* sol_press / sol_dblclick results */
enum { SOL_PRESS_NONE = 0,               /* nothing happened (the clock may have started) */
       SOL_PRESS_DRAG = 1,               /* a drag began */
       SOL_PRESS_DONE = 2 };             /* a draw, recycle, turn-over or double-click move happened */
/* Modifier keys held (GetKeyState < 0) */
enum { SOL_MOD_SHIFT = 1, SOL_MOD_CTRL = 2, SOL_MOD_ALT = 4,
       SOL_MOD_CHEAT = SOL_MOD_SHIFT | SOL_MOD_CTRL | SOL_MOD_ALT };   /* stock: draw one card */
/* sol_key codes = the Windows virtual-key codes, so wParam can be passed as is. */
enum { SOL_KEY_TAB = 0x09, SOL_KEY_RETURN = 0x0D, SOL_KEY_ESCAPE = 0x1B, SOL_KEY_SPACE = 0x20,
       SOL_KEY_END = 0x23, SOL_KEY_HOME = 0x24, SOL_KEY_LEFT = 0x25, SOL_KEY_UP = 0x26,
       SOL_KEY_RIGHT = 0x27, SOL_KEY_DOWN = 0x28, SOL_KEY_A = 0x41 };
/* What the stock pile shows (layout.md §3) */
enum { SOL_STOCK_CARDS = 0,              /* its top card (face down) */
       SOL_STOCK_O = 1,                  /* empty: cards.dll's green O, click to turn the waste over */
       SOL_STOCK_X = 2 };                /* empty, Vegas, no passes left: the red X */

/* ---- History ------------------------------------------------------------------------------------- */
enum { SOL_ACT_MOVE = 1,                 /* n cards src -> dst (drop, double-click home) */
       SOL_ACT_DRAW = 2,                 /* n cards stock -> waste */
       SOL_ACT_RECYCLE = 3,              /* the waste turned back into the stock */
       SOL_ACT_TURN = 4,                 /* the top card of tableau column src turned over */
       SOL_ACT_AUTOPLAY = 5 };           /* nsteps single cards steps[i][0] -> steps[i][1] */
typedef struct SolAction {
    uint8_t type, src, dst, n;
    uint8_t nsteps;
    uint8_t steps[52][2];
    /* the state before the action (Undo restores it) */
    uint8_t board[SOL_PACKED_SIZE];
    uint8_t waste_fan;
    int32_t score, recycles;
    int32_t clock_pen;                   /* SolSession.clock_pen then */
} SolAction;

/* ---- UI callbacks ---------------------------------------------------------------------------------
 * Any callback may be NULL (a no-op; prompts take the default noted). Called synchronously from inside
 * the session call that needs them. */
typedef struct SolSessionUI {
    void *ctx;
    /* The board, the drag, the drop target, the back or the visibility changed: re-render. Render the
     * back buffer before returning (the win flow runs right after the last move's invalidate). */
    void (*invalidate)(void *ctx);
    /* The score, the time, or which of them (or the status bar) is shown changed: redraw the status. */
    void (*status_changed)(void *ctx);
    /* Start (ms > 0) or kill (ms == 0) Win32 timer id; on WM_TIMER call sol_timer(s, id). */
    void (*set_timer)(void *ctx, int id, int ms);
    /* The game is won (rules.md §8; the session has scored the bonus, stopped the clock and cleared
     * the history). Show sol_win_text in the status bar's left part, play the cascade (cascade.h, from
     * s->rng; after a forced win from the foundations' origins, over the board as it was shown — the
     * session did not invalidate), stop on WM_KEYDOWN / WM_SYSKEYDOWN / button downs / WM_MENUSELECT
     * (left in the queue), then clear the status text and erase the table. Return when done. */
    void (*win_cascade)(void *ctx);
    /* MessageBox "Deal Again?", caption "Solitaire", MB_YESNO | MB_ICONEXCLAMATION. 1 = Yes.
     * NULL = No (the finished table stays frozen until Deal). */
    int  (*deal_again)(void *ctx);
    /* "Deal Again? -> Yes": PostMessage(WM_COMMAND, cmd), later sol_command(s, cmd). NULL = deal now. */
    void (*post_command)(void *ctx, int cmd);
    uint32_t (*now_seed)(void *ctx);     /* time(NULL): deal seeds, the random back. NULL = 0 */
    /* The keyboard moved its cursor (XP: SetCursorPos onto the card's top centre, (x + cw/2, y), while
     * dragging over a non-empty pile lower by the pile's dyUp: a face-up step on the tableau, 1 XP px
     * on a foundation; the drag then follows the pointer). */
    void (*kbd_cursor)(void *ctx, int pile, int card, int dragging);
} SolSessionUI;

/* ---- Session state (read-only for the UI) ------------------------------------------------------ */
typedef struct SolSession {
    SolBoard board;
    int      waste_fan;      /* top waste cards fanned out by the last draw (0..3), see sol_waste_fan */
    int      dealt;          /* XP fDealt: a game is on; board input is ignored while 0 */
    int      visible;        /* the piles are drawn (0 before the first deal and after the win) */
    int      input;          /* XP fInput: a press since the deal; the clock runs only after it */
    int      won;            /* the last game was won (cascade, then the frozen table) */
    int      forced_win;     /* ... through Alt+Shift+2 */
    int      bonus;          /* the Standard win bonus (sol_win_text) */
    int      score;
    int      ticks;          /* the clock in 250 ms ticks, 0..SOL_TICKS_MAX */
    int      recycles;       /* times the waste was turned into the stock this game */
    int      clock_pen;      /* Standard points the clock took (-2 per 10 s) this game, less refunds */
    int      undo_fresh;     /* no Undo since the last action or Redo (the next Undo is XP's) */
    int      draw;           /* the draw count of this game (1 or 3) */
    unsigned seed;           /* the deal's seed, 0..32767 (XP keeps it but never shows it) */
    int      seeded;
    uint32_t rng;            /* rand state after the deal: the cascade continues from it */
    int      minimized;
    int      timer_on;
    int      busy;           /* inside the win flow: commands and input are ignored */
    /* drag: cards drag_index..top of pile drag_pile (-1 = none); they stay in the pile until dropped */
    int      drag_pile, drag_index, drag_count;
    int      drag_kbd;       /* begun by the keyboard (grab point = the card's top centre) */
    int      target;         /* drop target from sol_drag_over, -1 = none */
    int      target_ui;      /* sol_drag_over was called during this drag (the UI tracks the target) */
    int      kbd_pile, kbd_card;   /* keyboard cursor */
    SolAction *hist;         /* undo history, oldest first */
    int      nhist, hist_cap;
    SolAction *redo;         /* undone actions, the next to redo last */
    int      nredo, redo_cap;
    SolOptions opts;
    int      back;           /* card back 0..11 (XP ids 54..65) */
    int      currency;       /* iCurrency 0..3 */
    CeStore  store;
    SolSessionUI ui;
    SolAction work;          /* the action being built */
} SolSession;

/* Initialise: no game yet (green table, "Score: 0"), options, back and iCurrency loaded from store
 * (XP's HKCU\Software\Microsoft\Solitaire). XP then deals at once (unless started minimized):
 * sol_command(s, SOL_CMD_DEAL). ui and store are copied; either may be NULL. */
void sol_init(SolSession *s, const SolSessionUI *ui, const CeStore *store);
void sol_free(SolSession *s);

/* Deal XP's deal number `seed` (& 0x7FFF). from_options: the Options dialog forced it (a Vegas
 * cumulative score is then reset). */
void sol_deal(SolSession *s, unsigned seed, int from_options);
/* A new deal with a fresh seed: time(NULL) & 0x7FFF, the next one if that repeats the last deal. */
void sol_new_deal(SolSession *s, int from_options);

/* ---- Input. pile = 0..12 (SOL_STOCK, SOL_WASTE, SOL_FOUND0.., SOL_TAB0..) or SOL_MISS; index = the card
 * hit (the topmost card whose rectangle holds the point), -1 for an empty pile. For the stock any hit
 * on its top card, or on the card-sized slot at its origin when it is empty, is a press on the stock. */
int  sol_press(SolSession *s, int pile, int index, int mods);
int  sol_dblclick(SolSession *s, int pile, int index, int mods);
int  sol_begin_drag(SolSession *s, int pile, int index);   /* the pick-up rules alone; 1 = dragging */
int  sol_can_drop_on(const SolSession *s, int pile);       /* the current drag may land on pile */
int  sol_drag_over(SolSession *s, int pile);               /* set the drop target; returns it or -1 */
/* Where Enter / Space drops the cards being dragged (-1: refused, they go back). XP's KeyHit sends
 * MouseUp: the target the dragged cards overlap (s->target), which the UI keeps up to date through
 * sol_drag_over as the cards follow the pointer or the keyboard cursor; without a UI (no
 * sol_drag_over during the drag) the keyboard cursor's pile. */
int  sol_key_drop_target(const SolSession *s);
int  sol_drop(SolSession *s, int pile);                    /* 1 = moved; 0 = refused (cards go back) */
void sol_cancel_drag(SolSession *s);                       /* Esc, focus lost: nothing recorded */
int  sol_autoplay(SolSession *s);                          /* right button / Ctrl+A: cards moved */
int  sol_key(SolSession *s, int key, int mods);            /* 1 = handled */
void sol_timer(SolSession *s, int id);
void sol_set_minimized(SolSession *s, int minimized);
void sol_command(SolSession *s, int cmd);
int  sol_undo(SolSession *s);                              /* 1 = an action was undone */
int  sol_redo(SolSession *s);
/* Options dialog OK: stores and writes "Options"; a change of Draw, Timed game or Scoring deals a new
 * game (score reset). Returns 1 if it dealt. */
int  sol_apply_options(SolSession *s, const SolOptions *o);
void sol_set_back(SolSession *s, int back);                /* Deck dialog OK: writes "Back" */

/* ---- Queries ------------------------------------------------------------------------------------ */
static inline int sol_dragging(const SolSession *s) { return s->drag_pile >= 0; }
int  sol_idle(const SolSession *s);                  /* Deal / Deck / About enabled: not dragging */
int  sol_undo_enabled(const SolSession *s);
int  sol_redo_enabled(const SolSession *s);
int  sol_board_visible(const SolSession *s);
int  sol_stock_symbol(const SolSession *s);          /* SOL_STOCK_* */
int  sol_seconds(const SolSession *s);               /* ticks >> 2 */
int  sol_clock_running(const SolSession *s);
/* The waste display (rules.md §3.2): the top `fan` cards are spread from the position of the card
 * below them (or the pile origin), each next one (cw/5, 1) further; the rest lie collapsed in the
 * 3-D pile. Returns s->waste_fan clamped to the waste's size. */
int  sol_waste_fan(const SolSession *s);
/* The same as the index of the fan's first card (layout.h's SolPiles.waste_fan): waste size - fan. */
int  sol_waste_fan_start(const SolSession *s);
/* The keyboard cursor, the card clamped to the pile (0 for an empty pile). */
void sol_kbd_cursor(const SolSession *s, int *pile, int *card);
/* The score value as the status bar shows it ("-$52", "15"); "" with scoring None. */
void sol_score_text(const SolSession *s, char *buf, size_t n);
/* The status text during the cascade: Standard "Bonus: <n>  Press Esc or a mouse button to stop...",
 * else "Press Esc or a mouse button to stop...". */
void sol_win_text(const SolSession *s, char *buf, size_t n);

#endif
