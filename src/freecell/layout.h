/*
 * FreeCell HD — scalable board geometry and hit testing (platform independent).
 *
 * Generalises XP FreeCell's layout (docs/xp-reference/layout.md §2, §10) by a scale factor s; at
 * s = 1 with a 632-wide client every coordinate equals XP's. See docs/DESIGN.md "Layout".
 */
#ifndef FC_LAYOUT_H
#define FC_LAYOUT_H

#include "engine/geom.h"
#include "game.h"

typedef struct FcLayout {
    int    client_w, client_h;
    double s;                 /* scale; 1.0 = XP */
    int    board_x, board_w;  /* board is centred horizontally */
    int    cw, ch;            /* card cell size: round(71 s) x round(96 s) */
    CeRect top[8];            /* top row cells: 0..3 free cells, 4..7 home cells */
    int    col_x[9];          /* col_x[1..8]: tableau column left edges (index 0 unused) */
    int    col_y0;            /* top of first card in every column: ch + round(10 s) */
    int    large_print;       /* laid out for the Large Print faces (extras.large_print) */
    int    step;              /* normal vertical step: floor(9 ch / 46); Large Print: ce_large_print_step(ch),
                                 round(21 ch / 96), so a stacked card shows its whole index */
    int    step_min;          /* smallest compressed step (keeps the rank glyph readable) */
    int    bottom_limit;      /* columns are compressed to end at or above this y */
    CeRect king;              /* small king sprite rect: K = round(32 s) */
    CeRect king_frame;        /* raised frame drawn around it (3 s outside the sprite) */
    int    bevel;             /* bevel line width: max(1, round(s)) */
    CeRect big_king;          /* win king: 320 s square at (board_x + 10 s, ch + 10 s), fitted to client */
    int    anim_px_per_frame; /* round(37 s) */
} FcLayout;

/* Compute the layout for a client area (fc_layout_compute_ex: large_print = the Large Print faces' step,
 * everything else the same; column compression as usual). */
void fc_layout_compute(FcLayout *l, int client_w, int client_h);
void fc_layout_compute_ex(FcLayout *l, int client_w, int client_h, int large_print);

/* Smallest client size the window should allow (s = 0.5). */
void fc_layout_min_client(int *w, int *h);

/* Client size giving scale s with the board's natural aspect (used for the first-run window size). */
void fc_layout_client_for_scale(double s, int *w, int *h);

/* Vertical step for column col (1..8) of board b (compressed if the column would overflow). */
int fc_layout_col_step(const FcLayout *l, const FcBoard *b, int col);

/* Rect of the card at (col, pos): col 0 = top row slot pos (0..7); col 1..8 = tableau position. */
CeRect fc_layout_card_rect(const FcLayout *l, const FcBoard *b, int col, int pos);

/* Hit testing, XP semantics (layout.md §2 "Hit testing", rules.md §2.4).
 * Returns 1 on a hit and fills *col / *pos:
 *   top row: col = 0, pos = 0..7 (cell under the point, occupied or not);
 *   tableau: col = 1..8, pos = index of the card under the point (buried cards over their visible
 *            strip, the exposed card over its full height); pos = -1 for an empty column.
 * Returns 0 on a miss. For FC_HIT_DEST, a point anywhere in a tableau column's x band (from its left
 * edge to the next column's left edge, at y >= col_y0) hits that column even below its last card or
 * over an empty column (pos is then the last index, or -1 if empty) — XP's wider destination zones.
 * Details: FC_HIT_SOURCE over an empty column hits (pos = -1) anywhere in the card's x range at
 * y >= col_y0; column 8's band is as wide as the others (XP: x = 631 at s = 1 is a miss); the king
 * gap, the band between the rows and the margins are misses in both modes; on a miss *col / *pos are
 * still set to the column under the point (or -1) and -1. Card rects are half-open (XP's HitTest
 * also accepted the pixel just right of / below a card). Compressed columns use their own step. */
enum { FC_HIT_SOURCE = 0, FC_HIT_DEST = 1 };
int fc_layout_hit(const FcLayout *l, const FcBoard *b, int x, int y, int mode, int *col, int *pos);

#endif
