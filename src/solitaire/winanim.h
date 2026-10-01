/*
 * Solitaire HD — the bouncing-cards win animation (platform independent).
 *
 * XP's KlondWinner cascade (docs/xp-reference/solitaire/layout.md §8, rules.md §8), exactly, scaled to
 * the board. Cards are launched one at a time, for each rank K down to A, foundations left to right,
 * from where they lie; each with two msvcrt rand() calls:
 *
 *     vx = rand() % 110 - 65;  if (abs(vx) < 15) vx = -20;     vy = rand() % 110 - 75;
 *     while (x > -cw && x < W) {   draw the card at (x, y), never erased (the trail)
 *         x += vx / 10;  y += vy / 10;  vy += 3;
 *         if (y > H - ch && vy > 0) vy = -vy * 8 / 10;     (integer C arithmetic)
 *     }
 *
 * The motion is computed in XP pixels and drawn at s times that (velocities and gravity scale with
 * the board): at s = 1 the positions are XP's to the pixel, and at 1080p the cascade looks the same,
 * with the same bounces and trail spacing relative to the cards. H is the whole client (the status bar
 * included, as XP: the cards sink behind it), W its width. Given the RNG state the result is
 * deterministic: with the state XP's deal left (srand(seed) + the shuffle's 260 calls) it reproduces
 * the logged cascades of XP's sol.exe card for card (tests/solitaire/test_sol_layout.c).
 *
 * Frames: XP waits MsgWaitForMultipleObjects(5 ms) per frame and stops on any key, mouse button or menu
 * (WM_KEYDOWN, WM_SYSKEYDOWN, WM_[NC]{L,R,M}BUTTONDOWN, WM_MENUSELECT); that pacing and the abort are the
 * Win32 layer's (SOL_WINANIM_FRAME_MS). Each frame draws one card (sol_render_card) into the back
 * buffer, which is never cleared until the end, and presents its rect.
 *
 * cascade.h has the same physics in XP units on a virtual canvas; this module drives it for the HD board
 * (start positions from the layout, exact pixel starts, device coordinates). Both reproduce XP's logs.
 *
 *   SolWinAnim a;  int card, x, y;
 *   sol_winanim_start(&a, &layout, &s->board, s->rng, s->forced_win);
 *   while (sol_winanim_frame(&a, &card, &x, &y)) { sol_render_card(fb, gfx, card, 0, x, y); present
 *       (x, y, cw, ch); wait SOL_WINANIM_FRAME_MS; stop on input }
 */
#ifndef SOL_WINANIM_H
#define SOL_WINANIM_H

#include <stdint.h>
#include "layout.h"

#define SOL_WINANIM_FRAME_MS 5

typedef struct SolWinAnim {
    /* set up by sol_winanim_start */
    double   s;
    int      cw, ch;
    int      card[4][13];          /* card r of foundation f (-1 if missing) */
    int      x0[4][13], y0[4][13]; /* where it starts (device px) */
    uint32_t rng;                  /* msvcrt rand() state */
    /* the card in flight */
    int      r, f;                 /* its index r (12 = the king .. 0) and foundation f; r = -1: done */
    int      flying;
    int      X, Y;                 /* its displacement in XP px (integers, as XP's x and y) */
    int      vx, vy;               /* XP's velocities (tenths of an XP px per frame) */
    int      xlo, xhi, ymax;       /* XP's bounds in the same units: in flight while xlo < X < xhi;
                                      bounces when Y > ymax */
    long     frames;               /* frames returned so far */
    int      w, h;                 /* client size */
} SolWinAnim;

/* Set up the cascade for the board laid out by l (b: the four full foundations). rng = the game's
 * msvcrt rand() state (sol_rand, game.h). from_origin = 1: every card starts at its foundation's origin
 * instead of its 3-D position (XP's Alt+Shift+2 fills the foundations without laying them out). */
void sol_winanim_start(SolWinAnim *a, const SolLayout *l, const SolBoard *b, uint32_t rng, int from_origin);

/* The next frame: the card (0..51) and the top-left (device px) to draw it at; returns 0 when the last
 * card has left the client. Each call is one of XP's frames. */
int sol_winanim_frame(SolWinAnim *a, int *card, int *x, int *y);

/* The RNG state now (the game's rand() continues from it, as XP's single msvcrt state). */
uint32_t sol_winanim_rng(const SolWinAnim *a);

#endif
