/*
 * FreeCell HD — board renderer (see render.h). Paint order follows XP's WM_PAINT (layout.md §5):
 * king frame and king, the 8 top-row slots, columns 1..8 top to bottom, the peeked card, the big
 * win king.
 *
 * A clipped render draws through the engine's CeDraw (engine/draw.h): a view of the framebuffer
 * restricted to the rect, shifted by the rect's origin; every primitive uses integer coordinates, so
 * the pixels are identical to a full render (tests/freecell/test_layout.c checks this).
 */
#include "render.h"

#include "engine/draw.h"

void fc_view_init(FcView *v)
{
    v->sel_col = v->sel_pos = -1;
    v->peek_col = v->peek_pos = -1;
    v->hide_col = v->hide_pos = -1;
    v->king = FC_KINGVIEW_RIGHT;
    v->big_king = 0;
    v->no_game = 0;
    v->hint_col = v->hint_pos = -1;
}

void fc_render_prepare(FcCardSet *cs, const FcLayout *l, int quality)
{
    fc_cardset_set_size(cs, l->cw, l->ch, l->king.w, l->big_king.w, quality);
}

/* Empty-cell / king-frame bevel: XP's exact 1-px lines up to s < 1.5 (pixel-identical to XP at
 * s = 1, ce_draw_bevel), the smooth rounded ring (ce_draw_ring, thickness s, outer radius rad, cached
 * in the card set) for larger boards. */
static void cell_bevel(CeDraw *c, FcCardSet *cs, const FcLayout *l, CeRect r, double rad, uint32_t tl,
                       uint32_t br)
{
    if (l->bevel <= 1)
        ce_draw_bevel(c, r, l->bevel, tl, br);
    else
        ce_draw_ring(c, fc_cardset_cards(cs), r, l->s, rad, tl, br);
}

static void card(CeDraw *c, FcCardSet *cs, Card k, int x, int y, int inverted)
{
    if (k < 0 || k >= 52)
        return;
    ce_draw_sprite(c, fc_cardset_card(cs, k), x, y, inverted);
}

/* The hint on an empty cell or column: the card-shaped area at (x, y) inverted (any card's sprite has
 * the card's shape). */
static void invert_slot(CeDraw *c, FcCardSet *cs, int x, int y)
{
    ce_draw_invert_mask(c, fc_cardset_card(cs, 0), x, y);
}

static int hinted(const FcView *v, int col, int pos)
{
    return v->hint_col == col && (col == 0 ? v->hint_pos == pos : pos >= v->hint_pos);
}

static int last_index(const FcBoard *b, int col)
{
    int i = FC_COLLEN - 1;
    while (i >= 0 && b->board[col][i] == FC_EMPTY)
        i--;
    return i;
}

static void draw(CeDraw *c, const FcLayout *l, const FcBoard *b, const FcView *v, FcCardSet *cs)
{
    int i, col, game = !v->no_game && b;
    ce_draw_fill(c, c->clip.x, c->clip.y, c->clip.w, c->clip.h, FC_TABLE_GREEN);

    /* king box: raised frame (green top/left, black bottom/right), sprite unless blank */
    cell_bevel(c, cs, l, l->king_frame, 0.0223 * l->ch, FC_BEVEL_LIGHT, FC_BEVEL_DARK);
    if (v->king == FC_KINGVIEW_RIGHT || v->king == FC_KINGVIEW_LEFT) {
        const CeImage *k = fc_cardset_king(cs, v->king == FC_KINGVIEW_LEFT ? FC_KING_LEFT : FC_KING_RIGHT, 0);
        if (k)
            ce_draw_sprite(c, k, l->king.x + (l->king.w - k->w) / 2, l->king.y + (l->king.h - k->h) / 2, 0);
    }

    /* top row: free cells 0..3, home cells 4..7 */
    for (i = 0; i < 8; i++) {
        CeRect r = l->top[i];
        Card k = game ? b->board[0][i] : FC_EMPTY;
        if (!ce_draw_visible(c, r.x, r.y, r.w, r.h))
            continue;
        if (k != FC_EMPTY && v->hide_col == 0 && v->hide_pos == i) {
            /* lifted off: a home pile shows the previous rank of its suit, a free cell is empty */
            k = (i >= 4 && fc_rank(k) > 0) ? k - 4 : FC_EMPTY;
        }
        if (k == FC_EMPTY) {
            cell_bevel(c, cs, l, r, 0.0372 * l->ch, FC_BEVEL_DARK, FC_BEVEL_LIGHT);   /* card corner radius */
            if (game && hinted(v, 0, i))
                invert_slot(c, cs, r.x, r.y);
        } else {
            card(c, cs, k, r.x, r.y, (v->sel_col == 0 && v->sel_pos == i) != hinted(v, 0, i));
        }
    }

    if (!game)
        goto big_king;

    /* columns, each top to bottom */
    for (col = 1; col <= 8; col++) {
        int n = last_index(b, col) + 1, step, x = l->col_x[col];
        if (n == 0) {
            if (v->hint_col == col)
                invert_slot(c, cs, x, l->col_y0);
            continue;
        }
        step = fc_layout_col_step(l, b, col);
        if (v->hide_col == col && v->hide_pos >= 0 && v->hide_pos < n)
            n = v->hide_pos;
        if (!ce_draw_visible(c, x, l->col_y0, l->cw, (n - 1) * step + l->ch))
            continue;
        for (i = 0; i < n; i++)
            card(c, cs, b->board[col][i], x, l->col_y0 + i * step,
                 (v->sel_col == col && v->sel_pos == i) != hinted(v, col, i));
    }

    /* right-button / keyboard peek: the buried card drawn fully, in place, on top of its column */
    if (v->peek_col >= 1 && v->peek_col <= 8 && v->peek_pos >= 0) {
        int n = last_index(b, v->peek_col) + 1;
        if (v->hide_col == v->peek_col && v->hide_pos >= 0 && v->hide_pos < n)
            n = v->hide_pos;
        if (v->peek_pos < n) {
            CeRect r = fc_layout_card_rect(l, b, v->peek_col, v->peek_pos);
            card(c, cs, b->board[v->peek_col][v->peek_pos], r.x, r.y,
                 (v->sel_col == v->peek_col && v->sel_pos == v->peek_pos) != hinted(v, v->peek_col, v->peek_pos));
        }
    }

big_king:
    if (v->big_king) {
        const CeImage *k = fc_cardset_king(cs, FC_KING_SMILE, 1);
        if (k)
            ce_draw_sprite(c, k, l->big_king.x, l->big_king.y, 0);
    }
}

void fc_render_board_rect(CeImage *fb, const FcLayout *l, const FcBoard *b, const FcView *v,
                          FcCardSet *cs, CeRect r)
{
    CeDraw c;
    if (!fb || !l || !v || !ce_draw_begin(&c, fb, r))
        return;
    draw(&c, l, b, v, cs);
}

void fc_render_board(CeImage *fb, const FcLayout *l, const FcBoard *b, const FcView *v, FcCardSet *cs)
{
    CeRect r;
    if (!fb)
        return;
    r.x = r.y = 0;
    r.w = fb->w;
    r.h = fb->h;
    fc_render_board_rect(fb, l, b, v, cs, r);
}

CeImage *fc_render_stack(FcCardSet *cs, const FcLayout *l, const FcBoard *b, int col, int first)
{
    CeImage *img;
    int n, step = 0, i;
    if (!cs || !l || !b || col < 0 || col > 8 || first < 0)
        return NULL;
    if (col == 0) {
        if (first > 7 || b->board[0][first] == FC_EMPTY)
            return NULL;
        n = 1;
    } else {
        n = last_index(b, col) - first + 1;
        if (n <= 0)
            return NULL;
        step = fc_layout_col_step(l, b, col);
    }
    img = ce_image_new(l->cw, (n - 1) * step + l->ch);        /* transparent */
    if (!img)
        return NULL;
    for (i = 0; i < n; i++)
        fc_render_card(img, cs, col == 0 ? b->board[0][first] : b->board[col][first + i], 0, i * step, 0);
    return img;
}

void fc_render_card(CeImage *fb, FcCardSet *cs, Card k, int x, int y, int inverted)
{
    const CeImage *s;
    if (!fb || k < 0 || k >= 52)
        return;
    s = fc_cardset_card(cs, k);
    if (!s)
        return;
    if (inverted)
        ce_blit_inverted(fb, s, x, y);
    else
        ce_blit(fb, s, x, y);
}
