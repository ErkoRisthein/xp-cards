/*
 * Solitaire HD — the bouncing-cards win animation (see winanim.h): XP's integer physics in XP pixels,
 * drawn at s times the displacement from each card's start.
 *
 * XP's tests compare absolute positions in client pixels (x > -cw, x < W, y > H - ch). Here a card's
 * position is start + s * displacement with an integer displacement D, so each test "start + s D > t"
 * becomes "D > floor((t - start) / s)" (and "< t" becomes "< ceil(...)"), computed once per card; the
 * frame loop is XP's integer code. At s = 1 it is XP's code exactly.
 */
#include "winanim.h"

#include <math.h>
#include <string.h>

void sol_winanim_start(SolWinAnim *a, const SolLayout *l, const SolBoard *b, uint32_t rng, int from_origin)
{
    int f, r;
    memset(a, 0, sizeof *a);
    a->s = l->s;
    a->cw = l->cw;
    a->ch = l->ch;
    a->w = l->client_w;
    a->h = l->client_h;
    a->rng = rng;
    for (f = 0; f < 4; f++) {
        int pile = SOL_FOUND0 + f, n = b ? b->p[pile].n : 0;
        for (r = 0; r < 13; r++) {
            a->card[f][r] = r < n ? sol_card_id(b->p[pile].c[r]) : -1;
            if (from_origin) {
                a->x0[f][r] = l->pile[pile].x;
                a->y0[f][r] = l->pile[pile].y;
            } else {
                sol_layout_card_pos(l, b, 0, pile, r, &a->x0[f][r], &a->y0[f][r]);
            }
        }
    }
    a->r = 12;
    a->f = -1;
}

/* Launch the next card (K..A, foundations left to right): XP's two rand() calls. 0 when none is left. */
static int launch(SolWinAnim *a)
{
    double s = a->s;
    int x0, y0;
    for (;;) {
        if (++a->f == 4) {
            a->f = 0;
            a->r--;
        }
        if (a->r < 0)
            return 0;
        if (a->card[a->f][a->r] >= 0)
            break;
    }
    a->vx = sol_rand(&a->rng) % 110 - 65;
    if (a->vx > -15 && a->vx < 15)
        a->vx = -20;
    a->vy = sol_rand(&a->rng) % 110 - 75;
    x0 = a->x0[a->f][a->r];
    y0 = a->y0[a->f][a->r];
    a->X = a->Y = 0;
    a->xlo = (int)floor((-a->cw - x0) / s);              /* x > -cw   <=>  X > xlo */
    a->xhi = (int)ceil((a->w - x0) / s);                 /* x < W     <=>  X < xhi */
    a->ymax = (int)floor((a->h - a->ch - y0) / s);       /* y > H-ch  <=>  Y > ymax */
    a->flying = 1;
    return 1;
}

int sol_winanim_frame(SolWinAnim *a, int *card, int *x, int *y)
{
    for (;;) {
        if (a->r < 0)
            return 0;
        if (!a->flying && !launch(a)) {
            a->r = -1;
            return 0;
        }
        if (a->X > a->xlo && a->X < a->xhi)
            break;
        a->flying = 0;                                   /* off the left or right edge: next card */
    }
    *card = a->card[a->f][a->r];
    *x = a->x0[a->f][a->r] + (int)floor(a->X * a->s + 0.5);
    *y = a->y0[a->f][a->r] + (int)floor(a->Y * a->s + 0.5);
    a->X += a->vx / 10;
    a->Y += a->vy / 10;
    a->vy += 3;
    if (a->Y > a->ymax && a->vy > 0)
        a->vy = -a->vy * 8 / 10;
    a->frames++;
    return 1;
}

uint32_t sol_winanim_rng(const SolWinAnim *a)
{
    return a->rng;
}
