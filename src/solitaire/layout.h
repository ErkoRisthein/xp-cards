/*
 * Solitaire HD — scalable board geometry and hit testing (platform independent).
 *
 * XP Solitaire's geometry (docs/xp-reference/solitaire/layout.md §1-§3, §10) generalised by a scale
 * factor s: every XP constant is an XP pixel at s = 1 (card 71 x 96), and a 585 x 384 client with an
 * 18-px status bar reproduces XP's coordinates exactly (tests/solitaire/test_sol_layout.c).
 *
 *   s       = min(W / 585, (H - status bar) / 367), at least 0.5. 367 = XP's 384-px client minus the
 *             17 px of the status bar that overlap it: the board XP shows by default, with room for a
 *             column of 6 face-down and 10 face-up cards. 1904 x 996 (1080p maximised) gives s = 2.67,
 *             cards 189 x 256.
 *   cw x ch = round(71 s) x round(96 s): XP's card cell (the 5:7 art is stretched to it).
 *   gap g   = (W - 7 cw) / 8: XP's horizontal spread (its WM_SIZE), so the columns spread over the
 *             whole width as XP's do when the window is wider than the board.
 *   y       = XP's fixed rows: top MulDiv(ch, 5, 100), tableau ch + round(11 s).
 *   steps   = face-down round(3 s), face-up round(15 s) (XP: ch/25, 4ch/25), waste fan cw/5 x round(s),
 *             3-D pile edges (round(2 s), round(s)) per 10 cards (stock, waste) or per 4 (foundations).
 *   Tall tableau columns (extra; XP lets them run under the status bar) compress their face-up step to
 *   end above the status bar, never below 0.10 ch (the top of the rank index stays readable).
 *
 * Piles are XP's (game.h): 0 stock, 1 waste, 2..5 foundations, 6..12 tableau columns. Card positions
 * follow XP's ComputeCardPositions (layout.md §2.2) from the board alone, plus the waste's fan: the
 * number of top waste cards spread by the last draw (sol_waste_fan(session); rules.md §3.2), passed as
 * `fan` below.
 */
#ifndef SOL_LAYOUT_H
#define SOL_LAYOUT_H

#include "engine/geom.h"
#include "game.h"

typedef struct SolLayout {
    int    client_w, client_h; /* the whole client; the status bar overlaps its bottom (as XP's) */
    int    status_h;           /* status bar window height (XP: system font height + 2; 0 = hidden) */
    int    board_h;            /* client_h minus the status bar's visible part (status_h - 1) */
    double s;                  /* scale; 1.0 = XP */
    int    cw, ch;             /* card cell: round(71 s) x round(96 s) */
    int    gap;                /* XP's g: (W - 7 cw) / 8 */
    int    top;                /* top row y: MulDiv(ch, 5, 100) */
    int    tab_y;              /* tableau top: ch + round(11 s) */
    CeRect pile[SOL_NPILES];   /* XP's pile rects: hit and drop zones (an empty column's drop zone is
                                  its whole rect: 6 face-down + 12 face-up steps + ch tall) */
    int    step_dn, step_up;   /* tableau steps: round(3 s), round(15 s) */
    int    step_up_min;        /* compressed face-up step floor: round(0.10 ch) */
    int    fan_dx, fan_dy;     /* waste fan step: cw / 5, round(s) */
    int    edge_dx, edge_dy;   /* 3-D pile edge per layer: round(2 s), round(s) */
    int    bottom_limit;       /* tall columns are compressed to end at or above this y */
    CeRect status;             /* the status bar window rect, XP's (-1, H - h + 1, W + 2, h); 0 x 0 if hidden */
    int    line;               /* "Outline dragging" line width: max(1, round(s)) */
    int    zip_px_per_frame;   /* zip-back slide of a refused drop: round(36 s) (layout.md §6.1) */
} SolLayout;

/* XP's default client (585 x 384) and status bar height (18) at 96 DPI. */
#define SOL_XP_CLIENT_W 585
#define SOL_XP_CLIENT_H 384
#define SOL_XP_STATUS_H 18

/* Compute the layout for a client area; status_h = the status bar window height, 0 if it is off. */
void sol_layout_compute(SolLayout *l, int client_w, int client_h, int status_h);

/* Smallest client the window should allow (s = 0.5), and the client giving scale s with XP's default
 * proportions (the first-run window size). */
void sol_layout_min_client(int status_h, int *w, int *h);
void sol_layout_client_for_scale(double s, int status_h, int *w, int *h);

/* Face-up step of tableau column pile (6..12): step_up, or less when the column is compressed. */
int sol_layout_col_step(const SolLayout *l, const SolBoard *b, int pile);

/* Top-left of card i of pile (XP's ComputeCardPositions, scaled); the card rect is cw x ch. */
void   sol_layout_card_pos(const SolLayout *l, const SolBoard *b, int fan, int pile, int i, int *x, int *y);
CeRect sol_layout_card_rect(const SolLayout *l, const SolBoard *b, int fan, int pile, int i);

/* The stack of cards first..n-1 of pile as laid out (the dragged image; XP: cw x (n-1) * step + ch):
 * its size, relative to card first's top-left. */
void sol_layout_stack_size(const SolLayout *l, const SolBoard *b, int fan, int pile, int first, int *w, int *h);

/* Press hit test (XP's MouseDown; rules.md §2.2, §3.1, layout.md §6). Returns 1 and the pile / card:
 *   stock:      its top card (card = n - 1); an empty stock over the card-sized rect at its origin
 *               (card = -1: recycle);
 *   waste:      its top card only;
 *   foundation: its top card only (XP also hit the visible 3-D edge and picked up the cards above it:
 *               rules.md §11, fixed);
 *   tableau:    the topmost card whose card rect holds the point (face up or not: the top face-down
 *               card is turned over by a click, a buried face-down card picks up nothing: the session
 *               decides).
 * Returns 0 on a miss (*pile = SOL_MISS = -1, *card = -1). Rects are half-open, as Win32's PtInRect. */
int sol_layout_hit(const SolLayout *l, const SolBoard *b, int fan, int x, int y, int *pile, int *card);

/* Extra (click to select, v1.2): the empty pile (waste, foundation or column; not the stock, which
 * sol_layout_hit already hits) whose drop zone, its whole pile rect, holds the point; -1 if none. A
 * click there is the selection's destination. */
int sol_layout_hit_empty(const SolLayout *l, const SolBoard *b, int x, int y);

/* XP's drop zone of a pile (ValidMovePt, rules.md §2.3): the rect of its top card, or the whole pile
 * rect when it is empty. The dragged first card's rect (cw x ch at the drag position) must overlap it
 * by any amount; the target is the first pile in index order that overlaps and accepts the cards. */
CeRect sol_layout_drop_zone(const SolLayout *l, const SolBoard *b, int fan, int pile);

/* The first pile (0..12, skipping 'from'; -1 skips none) whose drop zone overlaps the card rect at
 * (x, y) and for which accept(ctx, pile) is nonzero (NULL: any); -1 if none. With the session:
 * accept = sol_can_drop_on, then sol_drag_over(s, result). */
int sol_layout_drop_target(const SolLayout *l, const SolBoard *b, int fan, int x, int y, int from,
                           int (*accept)(void *ctx, int pile), void *ctx);

#endif
