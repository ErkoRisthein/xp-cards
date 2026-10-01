/*
 * FreeCell HD — board renderer (platform independent). Draws into an FcImage framebuffer; the Win32
 * layer wraps its DIB section bits, native tests write the result to PNG.
 */
#ifndef FC_RENDER_H
#define FC_RENDER_H

#include "image.h"
#include "layout.h"
#include "cardset.h"
#include "../core/game.h"

#define FC_TABLE_GREEN  FC_RGB(0, 127, 0)
#define FC_BEVEL_LIGHT  FC_RGB(0, 255, 0)
#define FC_BEVEL_DARK   FC_RGB(0, 0, 0)

enum { FC_KINGVIEW_RIGHT = 0, FC_KINGVIEW_LEFT = 1, FC_KINGVIEW_BLANK = 2 };

typedef struct FcView {
    int sel_col, sel_pos;     /* selected card, drawn inverted; sel_col = -1 for none */
    int peek_col, peek_pos;   /* buried card drawn fully on top of its column (right-button / keyboard
                                 peek); peek_col = -1 for none */
    int hide_col, hide_pos;   /* card not drawn (it is in flight during an animation); -1 for none.
                                 Cards below it in the same column are hidden too. */
    int king;                 /* FC_KINGVIEW_* for the small king box */
    int big_king;             /* 1 = draw the big smiling win king */
    int no_game;              /* 1 = nothing dealt yet (startup): draw empty cells only */
    int hint_col, hint_pos;   /* hint flash (extra), drawn inverted like a selection: hint_col 0 = the
                                 top-row cell hint_pos (an empty cell inverts a card-shaped area);
                                 1..8 = the cards from hint_pos to the end of that column, or, when the
                                 column is empty, its first card slot; hint_col = -1 for none */
} FcView;

void fc_view_init(FcView *v);  /* nothing selected/peeked/hidden, king right, no big king */

/* Size the card set's sprites for layout l (call after fc_layout_compute, before rendering):
 * fc_cardset_set_size(cs, l->cw, l->ch, l->king.w, l->big_king.w, quality). quality 0 while the
 * window is being live-resized, 1 otherwise. */
void fc_render_prepare(FcCardSet *cs, const FcLayout *l, int quality);

/* Draw the whole board (background, top-row cells/cards, king + frame, columns, peek, big king). */
void fc_render_board(FcImage *fb, const FcLayout *l, const FcBoard *b, const FcView *v, FcCardSet *cs);

/* Draw only the region r of the board (clipped render, used for partial repaints/animation). */
void fc_render_board_rect(FcImage *fb, const FcLayout *l, const FcBoard *b, const FcView *v,
                          FcCardSet *cs, FcRect r);

/* Draw one card sprite at (x, y), optionally inverted (selection look). */
void fc_render_card(FcImage *fb, FcCardSet *cs, Card c, int x, int y, int inverted);

#endif
