/*
 * FreeCell HD — scalable board geometry and hit testing (see layout.h, DESIGN.md "Layout").
 *
 * XP's formulas (layout.md §2) evaluated with a scale s and the board width Wb in place of XP's
 * client width; s = 1, Wc = 632 reproduces XP pixel-exactly (asserted in tests/test_layout.c).
 */
#include "layout.h"

#include <math.h>

#define XP_W     632   /* XP client width */
#define XP_H_MIN 372   /* top row + 10-card column + margin: 106 + 9*18 + 96 + 8 */
#define XP_H     427   /* XP default client height (Wine) */

static int iround(double v) { return (int)floor(v + 0.5); }

static int last_index(const FcBoard *b, int col)
{
    int i = FC_COLLEN - 1;
    while (i >= 0 && b->board[col][i] == FC_EMPTY)
        i--;
    return i;
}

void fc_layout_compute(FcLayout *l, int client_w, int client_h)
{
    double sw = client_w / (double)XP_W, sh = client_h / (double)XP_H_MIN, s = sw < sh ? sw : sh;
    int i, wb, bx, g, k, f;
    int clamped = s < 0.5;
    if (clamped)
        s = 0.5;
    l->client_w = client_w;
    l->client_h = client_h;
    l->s = s;
    /* width-limited: the board spans the client exactly; otherwise it is 632 s wide and centred */
    wb = (!clamped && sw <= sh) ? client_w : iround(XP_W * s);
    bx = wb < client_w ? (client_w - wb) / 2 : 0;
    l->board_x = bx;
    l->board_w = wb;
    l->cw = iround(71 * s);
    l->ch = iround(96 * s);
    for (i = 0; i < 4; i++) {
        FcRect fr = { bx + i * l->cw, 0, l->cw, l->ch };
        FcRect hr = { bx + wb - 4 * l->cw + i * l->cw, 0, l->cw, l->ch };
        l->top[i] = fr;
        l->top[4 + i] = hr;
    }
    g = (wb - 8 * l->cw) / 9;
    if (g < 0)
        g = 0;
    l->col_x[0] = 0;
    for (k = 1; k <= 8; k++)
        l->col_x[k] = bx + g + (int)floor((k - 1) * (double)(wb - g) / 8.0);
    l->col_y0 = l->ch + iround(10 * s);
    l->step = 9 * l->ch / 46;
    l->step_min = iround(0.10 * l->ch);
    if (l->step_min < 1)
        l->step_min = 1;
    if (l->step_min > l->step)
        l->step_min = l->step;
    l->bottom_limit = client_h - iround(4 * s);
    /* king (layout.md §2/§4): K at ((Wb - K)/2, (ch - K)/3), raised frame 3 s outside */
    k = iround(32 * s);
    l->king.x = bx + (wb - k) / 2;
    l->king.y = (l->ch - k) / 3;
    l->king.w = l->king.h = k;
    f = iround(3 * s);
    l->king_frame.x = l->king.x - f;
    l->king_frame.y = l->king.y - f;
    l->king_frame.w = l->king_frame.h = k + 2 * f;
    l->bevel = iround(s) > 1 ? iround(s) : 1;
    /* win king: 320 s square at (10 s, ch + 10 s), shrunk to fit the client height */
    l->big_king.x = bx + iround(10 * s);
    l->big_king.y = l->ch + iround(10 * s);
    k = iround(320 * s);
    if (k > client_h - l->big_king.y)
        k = client_h - l->big_king.y;
    if (k > wb - 2 * iround(10 * s))
        k = wb - 2 * iround(10 * s);
    if (k < l->cw)
        k = l->cw;
    l->big_king.w = l->big_king.h = k;
    l->anim_px_per_frame = iround(37 * s);
    if (l->anim_px_per_frame < 1)
        l->anim_px_per_frame = 1;
}

void fc_layout_min_client(int *w, int *h)
{
    *w = (int)ceil(XP_W * 0.5);
    *h = (int)ceil(XP_H_MIN * 0.5);
}

void fc_layout_client_for_scale(double s, int *w, int *h)
{
    /* XP's own 632 x 427 client proportions: width-limited, so the layout gets exactly scale s */
    if (s < 0.5)
        s = 0.5;
    *w = iround(XP_W * s);
    *h = iround(XP_H * s);
}

int fc_layout_col_step(const FcLayout *l, const FcBoard *b, int col)
{
    int n, avail, step;
    if (!b || col < 1 || col > 8)
        return l->step;
    n = last_index(b, col) + 1;
    if (n < 2 || l->col_y0 + (n - 1) * l->step + l->ch <= l->bottom_limit)
        return l->step;
    /* compress to end at bottom_limit, never below step_min (then it runs off the bottom, as XP) */
    avail = l->bottom_limit - l->col_y0 - l->ch;
    step = avail > 0 ? avail / (n - 1) : 0;
    return step < l->step_min ? l->step_min : step;
}

FcRect fc_layout_card_rect(const FcLayout *l, const FcBoard *b, int col, int pos)
{
    FcRect r = { 0, 0, l->cw, l->ch };
    if (col == 0) {
        if (pos >= 0 && pos < 8)
            r = l->top[pos];
        return r;
    }
    if (col >= 1 && col <= 8) {
        r.x = l->col_x[col];
        r.y = l->col_y0 + pos * fc_layout_col_step(l, b, col);
    }
    return r;
}

/* XP HitTest (0x1002CFC) generalised; see layout.h. */
int fc_layout_hit(const FcLayout *l, const FcBoard *b, int x, int y, int mode, int *col, int *pos)
{
    int c, p, n, step, x_end;
    *col = -1;
    *pos = -1;
    if (y < 0 || x < 0 || x >= l->client_w || y >= l->client_h)
        return 0;
    if (y < l->ch) {
        /* top row: free cells flush left, home cells flush right; the king gap is a miss */
        int fx = l->top[0].x, hx = l->top[4].x;
        if (x >= fx && x < fx + 4 * l->cw) {
            *col = 0;
            *pos = (x - fx) / l->cw;
            return 1;
        }
        if (x >= hx && x < hx + 4 * l->cw) {
            *col = 0;
            *pos = 4 + (x - hx) / l->cw;
            return 1;
        }
        return 0;
    }
    if (y < l->col_y0 || x < l->col_x[1])
        return 0;                                    /* band between the rows, left margin */
    /* the column whose x band [col_x[k], col_x[k+1]) holds x; column 8's band has the same pitch */
    x_end = l->col_x[8] + (l->col_x[8] - l->col_x[7]);
    if (x >= x_end)
        return 0;
    for (c = 8; c > 1 && x < l->col_x[c]; c--)
        ;
    n = b ? last_index(b, c) + 1 : 0;
    *col = c;
    *pos = n - 1;
    if (mode == FC_HIT_DEST)
        return 1;                                    /* XP's wider destination zones (rules.md §2.4) */
    if (x >= l->col_x[c] + l->cw) {
        *pos = -1;
        return 0;                                    /* gap between columns */
    }
    if (n == 0)
        return 1;                                    /* empty column: pos = -1 */
    step = fc_layout_col_step(l, b, c);
    p = step > 0 ? (y - l->col_y0) / step : n - 1;
    if (p < n - 1) {
        *pos = p;                                    /* a buried card's visible strip */
        return 1;
    }
    if (y < l->col_y0 + (n - 1) * step + l->ch) {
        *pos = n - 1;                                /* the exposed card, over its full height */
        return 1;
    }
    *pos = -1;
    return 0;                                        /* below the last card */
}
