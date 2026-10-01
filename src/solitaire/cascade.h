/*
 * Solitaire HD — the bouncing-cards win animation of XP Solitaire (KlondWinner 0x1004DF0), platform
 * independent. docs/xp-reference/solitaire/layout.md §8 has the drawing side.
 *
 * The physics is XP's integer code, simulated in XP units (71 x 96 cards) on a virtual canvas of the
 * client size divided by the board scale (layout.md §8.6):
 *
 *   for rank K..A, for foundation 1..4:  vx = rand() % 110 - 65 (|vx| < 15 -> -20); vy = rand() % 110 - 75
 *     from where the card lies, while (x > -cw && x < W): draw at (x, y); x += vx/10; y += vy/10; vy += 3;
 *     if (y > H - ch && vy > 0) vy = -vy*8/10
 *
 * rand() continues from the deal: start from SolSession.rng (the state after the deal's 260 calls).
 * Checked against all 52 logged trajectories of three wins of the real sol.exe (tests).
 *
 *   SolCascade c;  int f, r, x, y;
 *   sol_cascade_init(&c, s->rng, W, H, 71, 96);           // W, H: the client (incl. status bar) / scale
 *   while (sol_cascade_next(&c, &f, &r)) {
 *       sol_cascade_place(&c, x0, y0);                     // where foundation f's card r lies (XP units)
 *       while (sol_cascade_frame(&c, &x, &y)) draw the card at (x, y), never erased; wait; stop on input
 *   }
 *
 * Start positions: a natural win launches each card from its place in the foundation pile (the 3-D
 * edge: +2,+1 XP pixels per 4 cards, so K at +6,+3); after the Alt+Shift+2 cheat (s->forced_win)
 * every card starts at the foundation's origin.
 */
#ifndef SOL_CASCADE_H
#define SOL_CASCADE_H

#include <stdint.h>

typedef struct SolCascade {
    uint32_t rng;              /* msvcrt rand state */
    int w, h, cw, ch;          /* canvas and card size */
    int next;                  /* cards launched so far, 0..52 */
    int x, y, vx, vy;          /* the card in flight */
} SolCascade;

void sol_cascade_init(SolCascade *c, uint32_t rng, int w, int h, int cw, int ch);
/* Launch the next card: returns 0 when all 52 have flown. *found = 0..3 (pile SOL_FOUND0 + found),
 * *rank = 12..0 (also its index in that foundation). Draws its velocity (two rand calls); then call
 * sol_cascade_place with its start position. */
int  sol_cascade_next(SolCascade *c, int *found, int *rank);
void sol_cascade_place(SolCascade *c, int x, int y);
/* One frame of the card in flight: returns 1 with the position to draw it at, then advances it; returns
 * 0 once it is entirely off the canvas on the left or right. Every card leaves (|vx/10| >= 1). */
int  sol_cascade_frame(SolCascade *c, int *x, int *y);

#endif
