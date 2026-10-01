/*
 * Solitaire HD — prints a winning line of an XP deal as fcdrive commands (tests/e2e/solhd_extras.txt
 * embeds its output). The deal is solved by the full-information solver (src/solitaire/solver.c) and
 * the solution replayed through the session; every action becomes a click (the stock, a face-down card)
 * or a drag (the card's top strip to where the card lands on its destination) at the given client size
 * with the status bar, as the layout (src/solitaire/layout.c) places the cards. The line stops at the
 * first position where Finish is possible (sol_finish_ready); the last action is printed apart.
 *
 *   cc -std=c99 -O2 -Isrc -Ithird_party -o build/sol_e2e_line tests/solitaire/sol_e2e_line.c \
 *      src/engine/*.c src/solitaire/*.c -lm
 *   build/sol_e2e_line SEED DRAW [W H STATUS_H]      (default 1264 854 18)
 */
#include "solitaire/layout.h"
#include "solitaire/session.h"
#include "solitaire/solver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void point_of(const SolLayout *l, const SolSession *s, int pile, int i, int *x, int *y)
{
    sol_layout_card_pos(l, &s->board, sol_waste_fan(s), pile, i, x, y);
}

int main(int argc, char **argv)
{
    unsigned seed = argc > 1 ? (unsigned)atoi(argv[1]) : 1;
    int draw = argc > 2 ? atoi(argv[2]) : 1;
    int w = argc > 5 ? atoi(argv[3]) : 1264, h = argc > 5 ? atoi(argv[4]) : 854, sh = argc > 5 ? atoi(argv[5]) : 18;
    SolLayout l;
    SolSession s;
    SolOptions o;
    SolSolveResult r;
    SolSolver *sv = sol_solver_new(0);
    int ready_at = -1;
    if (!sv)
        return 1;
    sol_layout_compute(&l, w, h, sh);
    sol_init(&s, NULL, NULL);
    sol_options_unpack(&o, draw == 1 ? 0x03 : 0x0B);
    s.opts = o;
    sol_deal(&s, seed, 0);
    if (sol_solve(sv, &s.board, draw, -1, NULL, &r) != SOL_SOLVE_SOLVED) {
        fprintf(stderr, "seed %u: not solved (status %d)\n", seed, r.status);
        return 1;
    }
    printf("# seed %u, draw %d: %d actions (solver), %u nodes\n", seed, draw, r.nmoves, (unsigned)r.nodes);
    for (int k = 0; k < r.nmoves; k++) {
        const SolSolveMove *m = &r.moves[k];
        int x, y, tx, ty, ok;
        if (sol_finish_ready(&s.board)) {
            ready_at = k;
            break;
        }
        switch (m->kind) {
        case SOL_SM_DRAW:
        case SOL_SM_RECYCLE:
            printf("click %d %d\n", l.pile[SOL_STOCK].x + l.cw / 2, l.pile[SOL_STOCK].y + l.ch / 2);
            ok = sol_press(&s, SOL_STOCK, 0, 0) == SOL_PRESS_DONE;
            break;
        case SOL_SM_TURN:
            point_of(&l, &s, m->src, m->index, &x, &y);
            printf("click %d %d\n", x + l.cw / 2, y + l.ch / 2);
            ok = sol_press(&s, m->src, m->index, 0) == SOL_PRESS_DONE;
            break;
        default: {
            int n = s.board.p[m->dst].n;
            point_of(&l, &s, m->src, m->index, &x, &y);
            if (n == 0) {
                tx = l.pile[m->dst].x;
                ty = l.pile[m->dst].y;
            } else if (sol_is_tab(m->dst)) {
                point_of(&l, &s, m->dst, n - 1, &tx, &ty);
                ty += sol_layout_col_step(&l, &s.board, m->dst);
            } else {
                point_of(&l, &s, m->dst, n - 1, &tx, &ty);
            }
            printf("drag %d %d %d %d 4\n", x + l.cw / 2, y + 4, tx + l.cw / 2, ty + 4);
            ok = sol_begin_drag(&s, m->src, m->index) && sol_drop(&s, m->dst);
            break;
        }
        }
        if (!ok) {
            fprintf(stderr, "action %d refused\n", k);
            return 1;
        }
    }
    printf("# Finish possible after %d actions; score %d, recycles %d\n", ready_at, s.score, s.recycles);
    sol_solver_free(sv);
    sol_free(&s);
    return ready_at > 0 ? 0 : 1;
}
