/*
 * FreeCell HD — board renderer (see render.h). Paint order follows XP's WM_PAINT (layout.md §5):
 * king frame and king, the 8 top-row slots, columns 1..8 top to bottom, the peeked card, the big
 * win king.
 *
 * A clipped render draws into a view of the framebuffer restricted to the rect, shifted by the
 * rect's origin; every primitive uses integer coordinates, so the pixels are identical to a full
 * render (tests/test_layout.c checks this).
 */
#include "render.h"

typedef struct {
    FcImage img;   /* view of the framebuffer restricted to the clip rect */
    int     ox, oy;
    FcRect  clip;  /* in board coordinates */
} Ctx;

void fc_view_init(FcView *v)
{
    v->sel_col = v->sel_pos = -1;
    v->peek_col = v->peek_pos = -1;
    v->hide_col = v->hide_pos = -1;
    v->king = FC_KINGVIEW_RIGHT;
    v->big_king = 0;
    v->no_game = 0;
}

void fc_render_prepare(FcCardSet *cs, const FcLayout *l, int quality)
{
    fc_cardset_set_size(cs, l->cw, l->ch, l->king.w, l->big_king.w, quality);
}

static int overlaps(const FcRect *a, int x, int y, int w, int h)
{
    return x < a->x + a->w && a->x < x + w && y < a->y + a->h && a->y < y + h;
}

static void fill(Ctx *c, int x, int y, int w, int h, uint32_t argb)
{
    if (w > 0 && h > 0)
        fc_fill_rect(&c->img, x - c->ox, y - c->oy, w, h, argb);
}

/* XP's 1-px bevel generalised to width b (layout.md §3): light/dark lines stop one pixel short at
 * the top-right and bottom-left corners, leaving a background diagonal there (a natural mitre). */
static void bevel(Ctx *c, FcRect r, int b, uint32_t tl, uint32_t br)
{
    int i;
    if (!overlaps(&c->clip, r.x, r.y, r.w, r.h))
        return;
    for (i = 0; i < b && 2 * i + 1 < r.w && 2 * i + 1 < r.h; i++) {
        fill(c, r.x + i, r.y + i, 1, r.h - 1 - 2 * i, tl);                 /* left   */
        fill(c, r.x + i, r.y + i, r.w - 1 - 2 * i, 1, tl);                 /* top    */
        fill(c, r.x + r.w - 1 - i, r.y + 1 + i, 1, r.h - 1 - 2 * i, br);   /* right  */
        fill(c, r.x + 1 + i, r.y + r.h - 1 - i, r.w - 1 - 2 * i, 1, br);   /* bottom */
    }
}

static void sprite(Ctx *c, const FcImage *s, int x, int y, int inverted)
{
    if (!s || !overlaps(&c->clip, x, y, s->w, s->h))
        return;
    if (inverted)
        fc_blit_inverted(&c->img, s, x - c->ox, y - c->oy);
    else
        fc_blit(&c->img, s, x - c->ox, y - c->oy);
}

static void card(Ctx *c, FcCardSet *cs, Card k, int x, int y, int inverted)
{
    if (k < 0 || k >= 52)
        return;
    sprite(c, fc_cardset_card(cs, k), x, y, inverted);
}

static int last_index(const FcBoard *b, int col)
{
    int i = FC_COLLEN - 1;
    while (i >= 0 && b->board[col][i] == FC_EMPTY)
        i--;
    return i;
}

static void draw(Ctx *c, const FcLayout *l, const FcBoard *b, const FcView *v, FcCardSet *cs)
{
    int i, col, game = !v->no_game && b;
    fill(c, c->clip.x, c->clip.y, c->clip.w, c->clip.h, FC_TABLE_GREEN);

    /* king box: raised frame (green top/left, black bottom/right), sprite unless blank */
    bevel(c, l->king_frame, l->bevel, FC_BEVEL_LIGHT, FC_BEVEL_DARK);
    if (v->king == FC_KINGVIEW_RIGHT || v->king == FC_KINGVIEW_LEFT) {
        const FcImage *k = fc_cardset_king(cs, v->king == FC_KINGVIEW_LEFT ? FC_KING_LEFT : FC_KING_RIGHT, 0);
        if (k)
            sprite(c, k, l->king.x + (l->king.w - k->w) / 2, l->king.y + (l->king.h - k->h) / 2, 0);
    }

    /* top row: free cells 0..3, home cells 4..7 */
    for (i = 0; i < 8; i++) {
        FcRect r = l->top[i];
        Card k = game ? b->board[0][i] : FC_EMPTY;
        if (!overlaps(&c->clip, r.x, r.y, r.w, r.h))
            continue;
        if (k != FC_EMPTY && v->hide_col == 0 && v->hide_pos == i) {
            /* lifted off: a home pile shows the previous rank of its suit, a free cell is empty */
            k = (i >= 4 && fc_rank(k) > 0) ? k - 4 : FC_EMPTY;
        }
        if (k == FC_EMPTY)
            bevel(c, r, l->bevel, FC_BEVEL_DARK, FC_BEVEL_LIGHT);
        else
            card(c, cs, k, r.x, r.y, v->sel_col == 0 && v->sel_pos == i);
    }

    if (!game)
        goto big_king;

    /* columns, each top to bottom */
    for (col = 1; col <= 8; col++) {
        int n = last_index(b, col) + 1, step, x = l->col_x[col];
        if (n == 0)
            continue;
        step = fc_layout_col_step(l, b, col);
        if (v->hide_col == col && v->hide_pos >= 0 && v->hide_pos < n)
            n = v->hide_pos;
        if (!overlaps(&c->clip, x, l->col_y0, l->cw, (n - 1) * step + l->ch))
            continue;
        for (i = 0; i < n; i++)
            card(c, cs, b->board[col][i], x, l->col_y0 + i * step, v->sel_col == col && v->sel_pos == i);
    }

    /* right-button / keyboard peek: the buried card drawn fully, in place, on top of its column */
    if (v->peek_col >= 1 && v->peek_col <= 8 && v->peek_pos >= 0) {
        int n = last_index(b, v->peek_col) + 1;
        if (v->hide_col == v->peek_col && v->hide_pos >= 0 && v->hide_pos < n)
            n = v->hide_pos;
        if (v->peek_pos < n) {
            FcRect r = fc_layout_card_rect(l, b, v->peek_col, v->peek_pos);
            card(c, cs, b->board[v->peek_col][v->peek_pos], r.x, r.y,
                 v->sel_col == v->peek_col && v->sel_pos == v->peek_pos);
        }
    }

big_king:
    if (v->big_king) {
        const FcImage *k = fc_cardset_king(cs, FC_KING_SMILE, 1);
        if (k)
            sprite(c, k, l->big_king.x, l->big_king.y, 0);
    }
}

void fc_render_board_rect(FcImage *fb, const FcLayout *l, const FcBoard *b, const FcView *v,
                          FcCardSet *cs, FcRect r)
{
    Ctx c;
    if (!fb || !l || !v)
        return;
    if (r.x < 0) { r.w += r.x; r.x = 0; }
    if (r.y < 0) { r.h += r.y; r.y = 0; }
    if (r.x + r.w > fb->w) r.w = fb->w - r.x;
    if (r.y + r.h > fb->h) r.h = fb->h - r.y;
    if (r.w <= 0 || r.h <= 0)
        return;
    c.img = fc_image_wrap(r.w, r.h, fb->stride, fb->px + (size_t)r.y * fb->stride + r.x);
    c.ox = r.x;
    c.oy = r.y;
    c.clip = r;
    draw(&c, l, b, v, cs);
}

void fc_render_board(FcImage *fb, const FcLayout *l, const FcBoard *b, const FcView *v, FcCardSet *cs)
{
    FcRect r;
    if (!fb)
        return;
    r.x = r.y = 0;
    r.w = fb->w;
    r.h = fb->h;
    fc_render_board_rect(fb, l, b, v, cs, r);
}

void fc_render_card(FcImage *fb, FcCardSet *cs, Card k, int x, int y, int inverted)
{
    const FcImage *s;
    if (!fb || k < 0 || k >= 52)
        return;
    s = fc_cardset_card(cs, k);
    if (!s)
        return;
    if (inverted)
        fc_blit_inverted(fb, s, x, y);
    else
        fc_blit(fb, s, x, y);
}
