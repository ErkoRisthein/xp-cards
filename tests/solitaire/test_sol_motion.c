/*
 * Solitaire HD — the motion of ROADMAP 2d at full HD (1904 x 996 with the status bar, s = 2.667): the
 * flights of Finish and the automatic moves home before (a straight flight of 60 s px per 10-ms frame)
 * and after (eased, decelerating into the foundation, engine/ease.h), the zip-back of a refused drop
 * (XP's 36 s px steps), and a Finish of 52 cards flown one after another as before and overlapping as
 * now. Checks that nothing is slower; prints the table the DESIGN.md numbers come from.
 */
#include "sol_test.h"

#include "engine/ease.h"
#include "solitaire/assist.h"
#include "solitaire/layout.h"

#define FRAME 10

static SolLayout L;

static int ppf(int xp) { int p = (int)(xp * L.s + 0.5); return p > 0 ? p : 1; }

static int dist(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0, dy = y1 - y0;
    return ce_isqrt((unsigned long)(dx * dx + dy * dy));
}

static void flight(const char *what, int d, int step)
{
    int x = ce_flight_ms_xp(d, ppf(step), FRAME), n = ce_flight_ms(d, (int)(L.s * 1000 + 0.5), ppf(step), FRAME);
    printf("  %-38s %5d px  %4d ms -> %4d ms\n", what, d, x, n);
    CHECK(n <= x);
}

static void card_flight(const char *what, const SolBoard *b, int src, int dst)
{
    SolBoard after = *b;
    int fx, fy, tx, ty;
    sol_layout_card_pos(&L, b, 0, src, b->p[src].n - 1, &fx, &fy);
    after.p[dst].c[after.p[dst].n++] = after.p[src].c[--after.p[src].n];
    sol_layout_card_pos(&L, &after, 0, dst, after.p[dst].n - 1, &tx, &ty);
    flight(what, dist(fx, fy, tx, ty), 60);
}

int main(void)
{
    SolBoard b;
    int k, i, src, dst, xp = 0, start = 0, end = 0, last_end[SOL_NPILES] = { 0 }, n = 0;
    sol_layout_compute(&L, 1904, 996, 18);
    printf("  Solitaire at 1904 x 996 (s = %.3f, card %d x %d; Finish %d px, zip-back %d px per %d-ms frame)\n", L.s,
           L.cw, L.ch, ppf(60), ppf(36), FRAME);
    sol_deal_board(&b, 64, NULL);
    card_flight("column 1 -> foundation 1", &b, SOL_TAB0, SOL_FOUND0);
    card_flight("column 7 -> foundation 4", &b, SOL_TAB0 + 6, SOL_FOUND0 + 3);
    card_flight("column 7 -> foundation 1 (far)", &b, SOL_TAB0 + 6, SOL_FOUND0);
    flight("zip-back of a drop 300 px away", 300, 36);
    flight("zip-back of a drop 1200 px away", 1200, 36);

    /* Finish: four columns K..A of one suit each (face up), every card home, lowest first */
    sol_board_clear(&b);
    for (k = 0; k < 4; k++) {
        SolPile *p = &b.p[SOL_TAB0 + k];
        for (i = 0; i < 13; i++)
            p->c[p->n++] = (SolCard)(((12 - i) * 4 + k) | SOL_UP);
    }
    CHECK(sol_finish_ready(&b));
    while (sol_finish_step(&b, &src, &dst)) {
        SolBoard after = b;
        int fx, fy, tx, ty, d, dur, t0;
        sol_layout_card_pos(&L, &b, 0, src, b.p[src].n - 1, &fx, &fy);
        after.p[dst].c[after.p[dst].n++] = after.p[src].c[--after.p[src].n];
        sol_layout_card_pos(&L, &after, 0, dst, after.p[dst].n - 1, &tx, &ty);
        d = dist(fx, fy, tx, ty);
        dur = ce_flight_ms(d, (int)(L.s * 1000 + 0.5), ppf(60), FRAME);
        xp += ce_flight_ms_xp(d, ppf(60), FRAME);
        t0 = start + ce_flight_delay(dur, last_end[dst] - start);
        last_end[dst] = t0 + dur;
        if (t0 + dur > end)
            end = t0 + dur;
        start = t0 + ce_cascade_ms(dur);
        b = after;
        n++;
    }
    printf("  %-38s %4d ms -> %4d ms\n", "Finish, 52 cards", xp, end);
    CHECK_EQ(n, 52);
    CHECK(end < xp * 3 / 4);
    return test_summary("test_sol_motion");
}
