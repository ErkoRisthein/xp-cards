/*
 * Solitaire HD — board renderer (platform independent). Draws into a CeImage framebuffer (the Win32
 * back buffer's DIB bits, or a test image written to PNG), region by region through the engine's
 * CeDraw, so a clipped render gives exactly the pixels of a full one.
 *
 * What it draws is XP Solitaire's board (layout.md §2.4, §3, §6, §7) at any scale: the table
 * (#008000), the piles back to front with their 3-D edges, the empty stock's green O or red X and
 * the empty foundations' dotted outline (cards.dll bitmaps 68, 67, 53, redrawn crisply at the card
 * size), the dragged stack, "Outline dragging" (an inverted outline and an inverted target), and a
 * keyboard selection (inverted cards). Card faces and the 12 backs come from the engine's card set.
 */
#ifndef SOL_RENDER_H
#define SOL_RENDER_H

#include "engine/cardset.h"
#include "engine/image.h"
#include "layout.h"

#define SOL_TABLE_GREEN CE_RGB(0, 128, 0)      /* XP Solitaire's table, RGB(0,128,0) */
#define SOL_MARK_O      CE_RGB(0, 255, 0)      /* the empty stock's "O" (cards.dll 68) */
#define SOL_MARK_X      CE_RGB(255, 0, 0)      /* the empty stock's "X" (cards.dll 67) */

/* The 12 card backs (SOL_NBACKS, game.h): back i is XP's cards.dll back 54 + i (the registry's Back
 * value is i + 1), RCDATA CE_ASSET_BACK0 + i = res/solitaire/backs/<54 + i>_<name>.png (CC0 designs from
 * the RevK generator, res/LICENSE-ART.md). */
extern const char *const sol_back_names[SOL_NBACKS];  /* "Sky", "Aqua", ... (logs, tests) */

/* Render resources: the engine card set (52 faces + the 12 backs) and the empty-pile sprites, rebuilt
 * when the card size changes. */
typedef struct SolGfx SolGfx;

SolGfx    *sol_gfx_new(CeAssetLoader loader, void *ctx);   /* NULL if the faces cannot be loaded */
void       sol_gfx_free(SolGfx *g);
CeCardSet *sol_gfx_cards(SolGfx *g);

/* Size the sprites for layout l (after sol_layout_compute, before rendering). quality 0 while the
 * window is being live-resized (ce_backbuf_quality), 1 otherwise. */
void sol_render_prepare(SolGfx *g, const SolLayout *l, int quality);

enum {
    SOL_DRAG_FULL = 0,    /* the dragged cards are lifted and drawn at (drag_x, drag_y) on top */
    SOL_DRAG_LIFTED = 1,  /* lifted but not drawn: the caller floats sol_render_stack's sprite over
                             the back buffer (ce_backbuf_present), as XP's save-under blits */
    SOL_DRAG_OUTLINE = 2  /* "Outline dragging": the cards stay; an inverted outline at (drag_x, drag_y) */
};

/* What the board looks like besides the piles. From the session (session.h): dealt =
 * sol_board_visible(s), waste_fan = sol_waste_fan(s), back = s->back, stock_x = sol_stock_symbol(s) ==
 * SOL_STOCK_X, drag_pile / drag_card = s->drag_pile / s->drag_index, drag_mode from s->opts.outline,
 * target = s->target in outline mode (XP highlights only there). */
typedef struct SolView {
    int dealt;               /* 0: XP's fDealt = 0 (before the first deal, after a win): table only */
    int waste_fan;           /* top waste cards spread by the last draw (layout.h "fan") */
    int back;                /* card back 0..11 */
    int stock_x;             /* the empty stock shows the red X (Vegas, no passes left), else the green O */
    int drag_pile, drag_card;/* cards drag_card..n-1 of drag_pile are being dragged; drag_pile -1: none */
    int drag_x, drag_y;      /* top-left of the first dragged card (mouse - grab offset) */
    int drag_mode;           /* SOL_DRAG_* */
    int target;              /* outline mode: the drop target under the drag (its top card, or the
                                empty pile's card slot, inverted: XP's Hilight); -1 none */
    int sel_pile, sel_card;  /* keyboard selection (extra): cards sel_card..n-1 of sel_pile inverted (an
                                empty pile: its card slot); -1 none */
} SolView;

void sol_view_init(SolView *v);   /* dealt, back 0, O, no drag, no target, no selection */

/* Draw the whole board / the region r of it. */
void sol_render_board(CeImage *fb, const SolLayout *l, const SolBoard *b, const SolView *v, SolGfx *g);
void sol_render_board_rect(CeImage *fb, const SolLayout *l, const SolBoard *b, const SolView *v,
                           SolGfx *g, CeRect r);

/* What the drag overlay covers at v's drag position (the stack, or the outline: one line wider and
 * taller); 0 x 0 without a drag. A move re-renders the union of the old and the new rect. */
CeRect sol_render_drag_rect(const SolLayout *l, const SolBoard *b, const SolView *v);

/* Union of the screen rects of cards first..n-1 of pile at their place (what a lift or drop changes). */
CeRect sol_render_stack_rect(const SolLayout *l, const SolBoard *b, int fan, int pile, int first);

/* The dragged stack as a sprite: cards first..n-1 of pile as laid out, relative to card first, on a
 * transparent background (w x h = sol_layout_stack_size). Free with ce_image_free. NULL if out of
 * memory. Also what the zip-back slide of a refused drop moves. back: for face-down cards (none in a
 * legal drag). */
CeImage *sol_render_stack(SolGfx *g, const SolLayout *l, const SolBoard *b, int fan, int back, int pile,
                          int first);

/* One face (card 0..51) or back (back = 0..11, face < 0) at (x, y): the win cascade's frames, drawn
 * into the back buffer and never erased. */
void sol_render_card(CeImage *fb, SolGfx *g, int card, int back, int x, int y);

/* The Select Card Back dialog's picture of back i at w x h (any size; the master stretched to it,
 * with the card shape and frame), decoded from its asset on each call. NULL if missing / no memory. */
CeImage *sol_back_image(CeAssetLoader loader, void *ctx, int i, int w, int h);

#endif
