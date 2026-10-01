/*
 * Solitaire HD — the win cascade physics (see cascade.h).
 */
#include "cascade.h"
#include "game.h"

void sol_cascade_init(SolCascade *c, uint32_t rng, int w, int h, int cw, int ch)
{
    c->rng = rng;
    c->w = w;
    c->h = h;
    c->cw = cw;
    c->ch = ch;
    c->next = 0;
    c->x = c->y = c->vx = c->vy = 0;
}

int sol_cascade_next(SolCascade *c, int *found, int *rank)
{
    if (c->next >= 52) return 0;
    if (rank) *rank = 12 - c->next / 4;
    if (found) *found = c->next % 4;
    c->next++;
    c->vx = sol_rand(&c->rng) % 110 - 65;
    if (c->vx > -15 && c->vx < 15) c->vx = -20;
    c->vy = sol_rand(&c->rng) % 110 - 75;
    return 1;
}

void sol_cascade_place(SolCascade *c, int x, int y)
{
    c->x = x;
    c->y = y;
}

int sol_cascade_frame(SolCascade *c, int *x, int *y)
{
    if (!(c->x > -c->cw && c->x < c->w)) return 0;
    *x = c->x;
    *y = c->y;
    c->x += c->vx / 10;
    c->y += c->vy / 10;
    c->vy += 3;
    if (c->y > c->h - c->ch && c->vy > 0) c->vy = -c->vy * 8 / 10;
    return 1;
}
