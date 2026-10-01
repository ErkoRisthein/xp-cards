/*
 * Solitaire HD — scalable board geometry and hit testing (see layout.h).
 *
 * XP's formulas (layout.md §2, rules.md §2-§3) with a scale s; s = 1 with a 585 x 384 client and an
 * 18-px status bar gives XP's coordinates exactly (asserted in tests/solitaire/test_sol_layout.c).
 */
#include "layout.h"

#include <math.h>
#include <string.h>

#define XP_W      585   /* XP's default client width: 7 cw + 8 g with g = cw / 8 + 3 = 11 */
#define XP_BOARD  367   /* XP's default client height (384) minus the visible status bar (17) */

static int iround(double v) { return (int)floor(v + 0.5); }
static int imax(int a, int b) { return a > b ? a : b; }

void sol_layout_compute(SolLayout *l, int client_w, int client_h, int status_h)
{
    int sbv = status_h > 0 ? status_h - 1 : 0, avail = client_h - sbv, k, top_h;
    double sw = client_w / (double)XP_W, sh = avail / (double)XP_BOARD, s = sw < sh ? sw : sh;
    int clamped = s < 0.5;
    memset(l, 0, sizeof *l);
    if (clamped)
        s = 0.5;
    l->client_w = client_w;
    l->client_h = client_h;
    l->status_h = status_h > 0 ? status_h : 0;
    l->board_h = avail;
    l->s = s;
    l->cw = iround(71 * s);
    l->ch = iround(96 * s);
    /* XP: g = max(cw/8 + 3, (W - 7 cw)/8). Here s <= W / 585 keeps (W - 7 cw)/8 >= 11 s - 1.5, so XP's
     * minimum only matters when s is clamped (a window narrower than the smallest board: it is then
     * clipped on the right, as XP's below 585 px). */
    l->gap = (client_w - 7 * l->cw) / 8;
    if (clamped)
        l->gap = imax(l->gap, iround(11 * s));
    l->top = (l->ch * 5 + 50) / 100;                     /* MulDiv(ch, 5, 100) */
    l->edge_dx = imax(1, iround(2 * s));
    l->edge_dy = imax(1, iround(s));
    l->tab_y = imax(l->ch + iround(11 * s), l->top + 5 * l->edge_dy + l->ch + 1);
    l->step_dn = imax(1, iround(3 * s));
    l->step_up = imax(l->step_dn, iround(15 * s));
    l->step_up_min = imax(1, iround(0.10 * l->ch));
    if (l->step_up_min > l->step_up)
        l->step_up_min = l->step_up;
    l->fan_dx = l->cw / 5;
    l->fan_dy = imax(1, iround(s));
    l->line = imax(1, iround(s));
    l->zip_px_per_frame = imax(1, iround(36 * s));
    l->bottom_limit = avail - iround(3 * s);

    /* XP's pile rects (RelayoutColumns 0x10040FE): the top row is ch + 5 layers tall; the stock is 5
     * 3-D layers wider, the waste 2 layers and 2 fan steps, the foundations 3 layers */
    top_h = l->ch + 5 * l->edge_dy;
    l->pile[SOL_STOCK] = ce_rect(l->gap, l->top, l->cw + 5 * l->edge_dx, top_h);
    l->pile[SOL_WASTE] = ce_rect(2 * l->gap + l->cw, l->top, l->cw + 2 * l->fan_dx + 2 * l->edge_dx, top_h);
    for (k = 0; k < 4; k++)
        l->pile[SOL_FOUND0 + k] = ce_rect(l->gap + (3 + k) * (l->cw + l->gap), l->top, l->cw + 3 * l->edge_dx,
                                          top_h);
    for (k = 0; k < 7; k++)
        l->pile[SOL_TAB0 + k] = ce_rect(l->gap + k * (l->cw + l->gap), l->tab_y, l->cw,
                                        6 * l->step_dn + 12 * l->step_up + l->ch);
    if (l->status_h > 0)
        l->status = ce_rect(-1, client_h - l->status_h + 1, client_w + 2, l->status_h);
}

void sol_layout_min_client(int status_h, int *w, int *h)
{
    int sbv = status_h > 0 ? status_h - 1 : 0;
    *w = (int)ceil(XP_W * 0.5);
    *h = (int)ceil(XP_BOARD * 0.5) + sbv;
}

void sol_layout_client_for_scale(double s, int status_h, int *w, int *h)
{
    int sbv = status_h > 0 ? status_h - 1 : 0;
    if (s < 0.5)
        s = 0.5;
    *w = iround(XP_W * s);
    *h = iround(XP_BOARD * s) + sbv;
}

static int pile_n(const SolBoard *b, int pile)
{
    int n;
    if (!b || pile < 0 || pile >= SOL_NPILES)
        return 0;
    n = b->p[pile].n;
    return n > 52 ? 52 : n;
}

int sol_layout_col_step(const SolLayout *l, const SolBoard *b, int pile)
{
    int n = pile_n(b, pile), i, nd = 0, nu = 0, avail, step;
    if (pile < SOL_TAB0 || n < 2)
        return l->step_up;
    for (i = 0; i < n - 1; i++) {
        if (sol_is_up(b->p[pile].c[i]))
            nu++;
        else
            nd++;
    }
    if (nu == 0 || l->tab_y + nd * l->step_dn + nu * l->step_up + l->ch <= l->bottom_limit)
        return l->step_up;
    /* compress the face-up step to end at bottom_limit, never below step_up_min (then the column runs
     * under the status bar, as every long column does in XP) */
    avail = l->bottom_limit - l->tab_y - l->ch - nd * l->step_dn;
    step = avail > 0 ? avail / nu : 0;
    return step < l->step_up_min ? l->step_up_min : step > l->step_up ? l->step_up : step;
}

/* XP's 3-D layering: (dx, dy) more after every 'every' cards */
static void layered(const SolLayout *l, int i, int every, int *dx, int *dy)
{
    *dx = l->edge_dx * (i / every);
    *dy = l->edge_dy * (i / every);
}

void sol_layout_card_pos(const SolLayout *l, const SolBoard *b, int fan, int pile, int i, int *x, int *y)
{
    int n = pile_n(b, pile), dx = 0, dy = 0, j;
    CeRect r = l->pile[pile >= 0 && pile < SOL_NPILES ? pile : 0];
    if (i < 0)
        i = 0;
    if (pile == SOL_STOCK) {
        layered(l, i, 10, &dx, &dy);
    } else if (pile == SOL_WASTE) {
        int first = n - (fan < 0 ? 0 : fan > n ? n : fan);   /* the first fanned card */
        if (i < first) {
            layered(l, i, 10, &dx, &dy);                 /* collapsed onto the 3-D pile */
        } else {
            if (first > 0)
                layered(l, first - 1, 10, &dx, &dy);     /* the fan starts on the card below it */
            dx += (i - first) * l->fan_dx;
            dy += (i - first) * l->fan_dy;
        }
    } else if (pile >= SOL_FOUND0 && pile < SOL_TAB0) {
        layered(l, i, 4, &dx, &dy);
    } else if (pile >= SOL_TAB0 && pile < SOL_NPILES) {
        int su = sol_layout_col_step(l, b, pile);
        for (j = 0; j < i && j < n; j++)
            dy += sol_is_up(b->p[pile].c[j]) ? su : l->step_dn;
        if (i > n && n > 0)
            dy += (i - n) * su;                          /* past the end: as further face-up cards */
    }
    *x = r.x + dx;
    *y = r.y + dy;
}

CeRect sol_layout_card_rect(const SolLayout *l, const SolBoard *b, int fan, int pile, int i)
{
    CeRect r;
    sol_layout_card_pos(l, b, fan, pile, i, &r.x, &r.y);
    r.w = l->cw;
    r.h = l->ch;
    return r;
}

void sol_layout_stack_size(const SolLayout *l, const SolBoard *b, int fan, int pile, int first, int *w, int *h)
{
    int n = pile_n(b, pile), x0, y0, x1, y1;
    *w = l->cw;
    *h = l->ch;
    if (first < 0 || first >= n)
        return;
    /* positions only grow along a pile: the last card is the far corner */
    sol_layout_card_pos(l, b, fan, pile, first, &x0, &y0);
    sol_layout_card_pos(l, b, fan, pile, n - 1, &x1, &y1);
    *w = l->cw + (x1 - x0);
    *h = l->ch + (y1 - y0);
}

static int in_rect(CeRect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

int sol_layout_hit(const SolLayout *l, const SolBoard *b, int fan, int x, int y, int *pile, int *card)
{
    int k, i, n;
    *pile = *card = -1;
    /* the stock first (KlondMouseDown 0x1004BDD), then piles 1..12 in index order */
    n = pile_n(b, SOL_STOCK);
    if (n == 0) {
        if (in_rect(ce_rect(l->pile[SOL_STOCK].x, l->pile[SOL_STOCK].y, l->cw, l->ch), x, y)) {
            *pile = SOL_STOCK;
            return 1;                                    /* the empty stock: card -1 */
        }
    } else if (in_rect(sol_layout_card_rect(l, b, fan, SOL_STOCK, n - 1), x, y)) {
        *pile = SOL_STOCK;
        *card = n - 1;
        return 1;
    }
    for (k = 1; k < SOL_NPILES; k++) {
        n = pile_n(b, k);
        if (n == 0)
            continue;
        if (k < SOL_TAB0) {                              /* waste, foundations: the top card only */
            if (in_rect(sol_layout_card_rect(l, b, fan, k, n - 1), x, y)) {
                *pile = k;
                *card = n - 1;
                return 1;
            }
            continue;
        }
        if (!in_rect(ce_rect(l->pile[k].x, l->pile[k].y, l->cw, 0x7fffffff - l->pile[k].y), x, y))
            continue;
        for (i = n - 1; i >= 0; i--)                     /* tableau: the topmost card under the point */
            if (in_rect(sol_layout_card_rect(l, b, fan, k, i), x, y)) {
                *pile = k;
                *card = i;
                return 1;
            }
    }
    return 0;
}

CeRect sol_layout_drop_zone(const SolLayout *l, const SolBoard *b, int fan, int pile)
{
    int n = pile_n(b, pile);
    if (pile < 0 || pile >= SOL_NPILES)
        return ce_rect(0, 0, 0, 0);
    return n ? sol_layout_card_rect(l, b, fan, pile, n - 1) : l->pile[pile];
}

int sol_layout_drop_target(const SolLayout *l, const SolBoard *b, int fan, int x, int y, int from,
                           int (*accept)(void *ctx, int pile), void *ctx)
{
    int k;
    for (k = 0; k < SOL_NPILES; k++) {
        CeRect z;
        if (k == from)
            continue;
        z = sol_layout_drop_zone(l, b, fan, k);
        if (z.w > 0 && z.h > 0 && ce_rect_overlaps(&z, x, y, l->cw, l->ch) && (!accept || accept(ctx, k)))
            return k;
    }
    return -1;
}
