/*
 * Solitaire HD — board renderer (see render.h).
 *
 * Paint order is XP's (layout.md §2.4): the table, then piles 0..12 back to front (stock, waste,
 * foundations, tableau), each bottom card first; a card lying exactly on the next one is skipped (as
 * XP skips hidden cards). Then the overlays: the dragged stack, the outline, the target, the keyboard
 * selection. Everything is drawn through CeDraw with integer coordinates, so a clipped render equals
 * the same region of a full render (tests/solitaire/test_sol_layout.c checks it).
 *
 * The empty-pile pictures are cards.dll's bitmaps redrawn at the card size, once per size:
 *   ghost (53, an empty foundation): a black outline with the card's rounded shape and a lattice of
 *     black dots, XP's pattern exactly at s = 1 (rows every 2 px: dots every 4 px from x = 3 on
 *     y = 2 mod 4, every 8 px from x = 5 / 1 on y = 4 / 0 mod 8), scaled with an integer pitch;
 *   O (68): a bright green elliptic ring, outer 57 x 61 XP px centred at (35.5, 48.5), 7 px thick;
 *   X (67): two red bars 7 px wide from (9.25, 21.5) to (62.75, 75) and mirrored, square ends;
 * the rings and bars anti-aliased (4 x 4 samples per pixel), inside the same outline. The table shows
 * through (XP draws them on the table colour).
 */
#include "render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/draw.h"

const char *const sol_back_names[SOL_NBACKS] = {
    "Sky", "Aqua", "Fish", "Frog", "Rose", "Island", "Mosaic", "Orchid", "Night", "Space", "Sunset", "Racing"
};

struct SolGfx {
    CeCardSet *cards;
    int        cw, ch;
    double     s;
    CeImage   *ghost, *mark_o, *mark_x;   /* empty foundation / stock pictures at cw x ch, lazily built */
};

static int iround(double v) { return (int)floor(v + 0.5); }

SolGfx *sol_gfx_new(CeAssetLoader loader, void *ctx)
{
    return sol_gfx_new_faces(loader, ctx, CE_FACES_NORMAL);
}

SolGfx *sol_gfx_new_faces(CeAssetLoader loader, void *ctx, int faces)
{
    SolGfx *g = (SolGfx *)calloc(1, sizeof *g);
    if (!g)
        return NULL;
    g->cards = ce_cardset_new_faces(loader, ctx, SOL_NBACKS, faces);
    if (!g->cards) {
        free(g);
        return NULL;
    }
    return g;
}

static void drop_marks(SolGfx *g)
{
    ce_image_free(g->ghost);
    ce_image_free(g->mark_o);
    ce_image_free(g->mark_x);
    g->ghost = g->mark_o = g->mark_x = NULL;
}

void sol_gfx_free(SolGfx *g)
{
    if (!g)
        return;
    drop_marks(g);
    ce_cardset_free(g->cards);
    free(g);
}

CeCardSet *sol_gfx_cards(SolGfx *g)
{
    return g ? g->cards : NULL;
}

void sol_render_prepare(SolGfx *g, const SolLayout *l, int quality)
{
    if (!g || !l)
        return;
    ce_cardset_set_size(g->cards, l->cw, l->ch, quality);
    if (g->cw != l->cw || g->ch != l->ch) {
        drop_marks(g);
        g->cw = l->cw;
        g->ch = l->ch;
    }
    g->s = l->s;
}

/* ---- the empty-pile pictures ------------------------------------------------------------------- */

/* The card frame as the engine draws it (src/engine/cardset.c): radius 0.0372 ch, 1 px up to ch 300. */
static double frame_radius(int ch) { return 0.0372 * ch; }
static double frame_px(int ch) { return ch <= 300 ? 1.0 : ch / 300.0; }

static void put_dot(CeImage *img, int x, int y, int d, uint32_t argb)
{
    ce_fill_rect(img, x, y, d, d, argb);
}

static CeImage *make_ghost(int cw, int ch, double s)
{
    CeImage *img = ce_image_new(cw, ch);
    int d, p, ox, oy, i, j, t;
    uint32_t black = CE_RGB(0, 0, 0);
    if (!img)
        return NULL;
    d = s < 1.5 ? 1 : iround(s);                      /* dot size */
    p = (int)floor(4 * s + 0.05);                     /* lattice pitch: XP's 4 px, an integer */
    if (p < 2)
        p = 2;
    t = (int)ceil(frame_px(ch));
    /* XP's lattice spans x 1..69, y 2..94 (in 4-px units: 17 x 23 pitches); centred (XP's offsets
     * (1, 2) at s = 1) */
    ox = (cw - (17 * p + d) + 1) / 2;
    oy = (ch - (23 * p + d) + 1) / 2;
#define GHOST_DOT(X, Y)                                                                            \
    do {                                                                                           \
        int hx = ox + (((X) - 1) * p) / 4, hy = oy + (((Y) - 2) * p) / 4;                          \
        if (hx >= t && hy >= t && hx + d <= cw - t && hy + d <= ch - t)                            \
            put_dot(img, hx, hy, d, black);                                                        \
    } while (0)
    for (j = 0; j <= 23; j++)                         /* y = 2 mod 4: x = 3, 7, .., 67 */
        for (i = 0; i <= 16; i++)
            GHOST_DOT(3 + 4 * i, 2 + 4 * j);
    for (j = 0; j <= 11; j++)                         /* y = 4 mod 8: x = 5, 13, .., 69 */
        for (i = 0; i <= 8; i++)
            GHOST_DOT(5 + 8 * i, 4 + 8 * j);
    for (j = 0; j <= 10; j++)                         /* y = 0 mod 8 (8 .. 88): x = 1, 9, .., 65 */
        for (i = 0; i <= 8; i++)
            GHOST_DOT(1 + 8 * i, 8 + 8 * j);
#undef GHOST_DOT
    ce_stroke_round_rect(img, 0, 0, cw, ch, frame_radius(ch), frame_px(ch), black);
    return img;
}

/* Anti-aliased shape in XP card coordinates (71 x 96), 4 x 4 samples per pixel, colour c. The shape
 * test runs once per sample when the marker is built (once per card size). */
typedef int (*ShapeFn)(double x, double y);

static int in_o(double x, double y)
{
    double ex = (x - 35.5) / 28.5, ey = (y - 48.5) / 30.5, ix = (x - 35.5) / 21.5, iy = (y - 48.5) / 23.5;
    return ex * ex + ey * ey <= 1.0 && ix * ix + iy * iy > 1.0;
}

/* A bar from (x0, y0) along the unit vector (ux, uy) for len, half_w to each side, square ends. Both of
 * the X's bars run at 45 degrees: from (9.25, 21.5) and from (62.75, 21.5), 53.5 * sqrt(2) long. */
#define BAR_U   0.70710678118654752
#define BAR_LEN 75.660425587
static int in_bar(double x, double y, double x0, double y0, double ux, double uy, double half_w)
{
    double px = x - x0, py = y - y0, along = px * ux + py * uy, across = px * uy - py * ux;
    return along >= 0 && along <= BAR_LEN && across <= half_w && across >= -half_w;
}

static int in_x(double x, double y)
{
    return in_bar(x, y, 9.25, 21.5, BAR_U, BAR_U, 3.54) || in_bar(x, y, 62.75, 21.5, -BAR_U, BAR_U, 3.54);
}

static CeImage *make_mark(int cw, int ch, ShapeFn shape, uint32_t c)
{
    CeImage *img = ce_image_new(cw, ch);
    double kx = 71.0 / cw, ky = 96.0 / ch;
    int x, y, sx, sy;
    if (!img)
        return NULL;
    for (y = 0; y < ch; y++) {
        uint32_t *row = img->px + (size_t)y * img->stride;
        double y0 = y * ky;
        /* rows and columns outside the shapes' extent (XP x 6..66, y 17..79) are empty */
        if (y0 + ky < 17.0 || y0 > 80.0)
            continue;
        for (x = (int)(5.0 / kx); x < cw && x * kx < 67.0; x++) {
            int cov = 0;
            for (sy = 0; sy < 4; sy++)
                for (sx = 0; sx < 4; sx++)
                    cov += shape((x + (sx + 0.5) / 4) * kx, (y + (sy + 0.5) / 4) * ky);
            if (cov) {
                uint32_t a = (uint32_t)(cov * 255 + 8) / 16;
                row[x] = (a << 24) | ((((c >> 16) & 255) * a / 255) << 16) | ((((c >> 8) & 255) * a / 255) << 8) |
                         ((c & 255) * a / 255);
            }
        }
    }
    ce_stroke_round_rect(img, 0, 0, cw, ch, frame_radius(ch), frame_px(ch), CE_RGB(0, 0, 0));
    return img;
}

static const CeImage *ghost(SolGfx *g)
{
    if (!g->ghost && g->cw > 0)
        g->ghost = make_ghost(g->cw, g->ch, g->s);
    return g->ghost;
}

static const CeImage *mark(SolGfx *g, int x_mark)
{
    CeImage **m = x_mark ? &g->mark_x : &g->mark_o;
    if (!*m && g->cw > 0)
        *m = make_mark(g->cw, g->ch, x_mark ? in_x : in_o, x_mark ? SOL_MARK_X : SOL_MARK_O);
    return *m;
}

/* ---- the board --------------------------------------------------------------------------------- */

void sol_view_init(SolView *v)
{
    memset(v, 0, sizeof *v);
    v->dealt = 1;
    v->drag_pile = v->drag_card = -1;
    v->drag_mode = SOL_DRAG_FULL;
    v->target = -1;
    v->sel_pile = v->sel_card = -1;
    v->sel_level = 256;
}

static int pile_n(const SolBoard *b, int pile)
{
    int n = b->p[pile].n;
    return n > 52 ? 52 : n;
}

static const CeImage *sprite(SolGfx *g, const SolBoard *b, int back, int pile, int i)
{
    SolCard c = b->p[pile].c[i];
    if (!sol_is_up(c))
        return ce_cardset_back(g->cards, back >= 0 && back < SOL_NBACKS ? back : 0);
    return sol_card_id(c) < 52 ? ce_cardset_card(g->cards, sol_card_id(c)) : NULL;
}

/* Is card i of the pile drawn inverted (keyboard selection)? */
static int selected(const SolView *v, int pile, int i)
{
    return v->sel_level > 0 && v->sel_pile == pile && v->sel_card >= 0 && i >= v->sel_card;
}

/* A card, inverted when selected; a selection at a partial level (the hint's pulse) partly inverted. */
static void sel_sprite(CeDraw *c, const SolView *v, const CeImage *s, int x, int y, int sel)
{
    if (!sel || v->sel_level >= 256) {
        ce_draw_sprite(c, s, x, y, sel);
        return;
    }
    ce_draw_sprite(c, s, x, y, 0);
    ce_draw_invert_mask_level(c, s, x, y, v->sel_level);
}

static int lifted(const SolView *v, int pile)
{
    return v->drag_pile == pile && v->drag_card >= 0 && v->drag_mode != SOL_DRAG_OUTLINE;
}

/* The card-shaped area at (x, y) inverted (an empty pile's slot: XP's InvertRect of a card rect). */
static void invert_slot(CeDraw *c, SolGfx *g, int x, int y)
{
    ce_draw_invert_mask(c, ce_cardset_card(g->cards, 0), x, y);
}

static void invert_slot_level(CeDraw *c, SolGfx *g, int x, int y, int level)
{
    ce_draw_invert_mask_level(c, ce_cardset_card(g->cards, 0), x, y, level);
}

static void draw_pile(CeDraw *c, const SolLayout *l, const SolBoard *b, const SolView *v, SolGfx *g, int pile)
{
    int n = pile_n(b, pile), end = n, i, fan = v->waste_fan;
    CeRect pr = l->pile[pile];
    if (lifted(v, pile) && v->drag_card < n)
        end = v->drag_card;                              /* the dragged cards are not drawn in place */
    if (end == 0) {
        /* XP: the empty stock's O / X, the empty foundation's dotted outline; nothing for the waste or
         * an empty column */
        if (n == 0 && pile == SOL_STOCK)
            ce_draw_sprite(c, mark(g, v->stock_x), pr.x, pr.y, 0);
        else if (pile >= SOL_FOUND0 && pile < SOL_TAB0)
            ce_draw_sprite(c, ghost(g), pr.x, pr.y, 0);
        if (n == 0 && selected(v, pile, 0))
            invert_slot_level(c, g, pr.x, pr.y, v->sel_level);
        return;
    }
    {
        /* card positions only grow in x and y along a pile: the first and last card bound it */
        CeRect a = sol_layout_card_rect(l, b, fan, pile, 0), z = sol_layout_card_rect(l, b, fan, pile, end - 1);
        a = ce_rect_union(a, z);
        if (!ce_draw_visible(c, a.x, a.y, a.w, a.h))
            return;
    }
    for (i = 0; i < end; i++) {
        int x, y, nx, ny;
        sol_layout_card_pos(l, b, fan, pile, i, &x, &y);
        if (i + 1 < end) {
            sol_layout_card_pos(l, b, fan, pile, i + 1, &nx, &ny);
            if (nx == x && ny == y)
                continue;                                /* exactly covered by the next card */
        }
        sel_sprite(c, v, sprite(g, b, v->back, pile, i), x, y, selected(v, pile, i));
    }
}

/* Cards first..n-1 of pile drawn with card first at (x0, y0), as laid out. */
static void draw_stack(CeDraw *c, const SolLayout *l, const SolBoard *b, int fan, int back, const SolView *v,
                       SolGfx *g, int pile, int first, int x0, int y0)
{
    int n = pile_n(b, pile), fx, fy, i;
    sol_layout_card_pos(l, b, fan, pile, first, &fx, &fy);
    for (i = first; i < n; i++) {
        int x, y;
        sol_layout_card_pos(l, b, fan, pile, i, &x, &y);
        ce_draw_sprite(c, sprite(g, b, back, pile, i), x0 + x - fx, y0 + y - fy, v && selected(v, pile, i));
    }
}

/* "Outline dragging" (layout.md §6.2): XP's R2_NOT polyline, scaled. The rectangle x..x+cw, y..y+h
 * inclusive (one line width more than the cards right and down), then one line per further card at
 * the card's top from x to x+cw-1: where it crosses the left edge the pixels are inverted twice (XP's
 * XOR leaves a notch there). */
static void draw_outline(CeDraw *c, const SolLayout *l, const SolBoard *b, int fan, int pile, int first, int x0,
                         int y0)
{
    int n = pile_n(b, pile), t = l->line, w, h, fx, fy, i;
    sol_layout_stack_size(l, b, fan, pile, first, &w, &h);
    ce_draw_invert_frame(c, ce_rect(x0, y0, w + t, h + t), t);
    sol_layout_card_pos(l, b, fan, pile, first, &fx, &fy);
    for (i = first + 1; i < n; i++) {
        int x, y;
        sol_layout_card_pos(l, b, fan, pile, i, &x, &y);
        ce_draw_invert_frame(c, ce_rect(x0 + x - fx, y0 + y - fy, l->cw, t), t);
    }
}

static void draw(CeDraw *c, const SolLayout *l, const SolBoard *b, const SolView *v, SolGfx *g)
{
    int k, dragging, fan = v->waste_fan;
    ce_draw_fill(c, c->clip.x, c->clip.y, c->clip.w, c->clip.h, SOL_TABLE_GREEN);
    if (!v->dealt || !b)
        return;                                          /* XP paints nothing more while fDealt = 0 */
    for (k = 0; k < SOL_NPILES; k++)
        draw_pile(c, l, b, v, g, k);
    dragging = v->drag_pile >= 0 && v->drag_pile < SOL_NPILES && v->drag_card >= 0 &&
               v->drag_card < pile_n(b, v->drag_pile);
    if (dragging && v->drag_mode == SOL_DRAG_FULL)
        draw_stack(c, l, b, fan, v->back, v, g, v->drag_pile, v->drag_card, v->drag_x, v->drag_y);
    if (v->target >= 0 && v->target < SOL_NPILES) {
        int n = pile_n(b, v->target);
        CeRect r;
        if (lifted(v, v->target) && v->drag_card < n)
            n = v->drag_card;                            /* what is left of a lifted pile */
        r = n ? sol_layout_card_rect(l, b, fan, v->target, n - 1) : l->pile[v->target];
        invert_slot(c, g, r.x, r.y);
    }
    if (dragging && v->drag_mode == SOL_DRAG_OUTLINE)
        draw_outline(c, l, b, fan, v->drag_pile, v->drag_card, v->drag_x, v->drag_y);
}

void sol_render_board_rect(CeImage *fb, const SolLayout *l, const SolBoard *b, const SolView *v,
                           SolGfx *g, CeRect r)
{
    CeDraw c;
    if (!fb || !l || !v || !g || !ce_draw_begin(&c, fb, r))
        return;
    draw(&c, l, b, v, g);
}

void sol_render_board(CeImage *fb, const SolLayout *l, const SolBoard *b, const SolView *v, SolGfx *g)
{
    if (fb)
        sol_render_board_rect(fb, l, b, v, g, ce_rect(0, 0, fb->w, fb->h));
}

CeRect sol_render_stack_rect(const SolLayout *l, const SolBoard *b, int fan, int pile, int first)
{
    CeRect r = ce_rect(0, 0, 0, 0);
    int w, h;
    if (!b || pile < 0 || pile >= SOL_NPILES || first < 0 || first >= pile_n(b, pile))
        return r;
    sol_layout_card_pos(l, b, fan, pile, first, &r.x, &r.y);
    sol_layout_stack_size(l, b, fan, pile, first, &w, &h);
    r.w = w;
    r.h = h;
    return r;
}

CeRect sol_render_drag_rect(const SolLayout *l, const SolBoard *b, const SolView *v)
{
    int w, h;
    if (!l || !b || !v || v->drag_pile < 0 || v->drag_pile >= SOL_NPILES || v->drag_card < 0 ||
        v->drag_card >= pile_n(b, v->drag_pile))
        return ce_rect(0, 0, 0, 0);
    sol_layout_stack_size(l, b, v->waste_fan, v->drag_pile, v->drag_card, &w, &h);
    if (v->drag_mode == SOL_DRAG_OUTLINE) {
        w += l->line;
        h += l->line;
    }
    return ce_rect(v->drag_x, v->drag_y, w, h);
}

CeImage *sol_render_stack(SolGfx *g, const SolLayout *l, const SolBoard *b, int fan, int back, int pile,
                          int first)
{
    CeImage *img;
    CeDraw c;
    int w, h;
    if (!g || !l || !b || pile < 0 || pile >= SOL_NPILES || first < 0 || first >= pile_n(b, pile))
        return NULL;
    sol_layout_stack_size(l, b, fan, pile, first, &w, &h);
    img = ce_image_new(w, h);                            /* transparent */
    if (!img || !ce_draw_begin(&c, img, ce_rect(0, 0, w, h)))
        return img;
    draw_stack(&c, l, b, fan, back, NULL, g, pile, first, 0, 0);
    return img;
}

void sol_render_card(CeImage *fb, SolGfx *g, int card, int back, int x, int y)
{
    const CeImage *s;
    if (!fb || !g)
        return;
    s = card >= 0 ? ce_cardset_card(g->cards, card) : ce_cardset_back(g->cards, back);
    if (s)
        ce_blit(fb, s, x, y);
}

CeImage *sol_back_image(CeAssetLoader loader, void *ctx, int i, int w, int h)
{
    const void *data;
    size_t len = 0;
    CeImage *m, *img;
    if (!loader || i < 0 || i >= SOL_NBACKS || w < 4 || h < 4)
        return NULL;
    data = loader(CE_ASSET_BACK0 + i, &len, ctx);
    m = data ? ce_image_decode_png(data, len) : NULL;
    if (!m)
        return NULL;
    img = ce_image_resample_card(m, w, h, 1);            /* the masters are opaque (make_backs.py) */
    ce_image_free(m);
    if (img)
        ce_image_card_finish(img, 0.0372 * h, frame_px(h), CE_RGB(0, 0, 0), CE_RGB(255, 255, 255));
    return img;
}
