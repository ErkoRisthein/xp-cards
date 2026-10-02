/*
 * FreeCell HD — the motion of ROADMAP 2d at full HD (1904 x 996, s = 2.677): each typical flight's time
 * before (XP's straight flight, 37 s px per 10-ms frame) and after (eased, engine/ease.h), and two
 * cascades (autoplay of the four aces, Finish of 52 cards) flown one after another as before and
 * overlapping as now. Checks that nothing is slower; prints the table the DESIGN.md numbers come from.
 */
#include <stdio.h>
#include <string.h>

#include "engine/ease.h"
#include "freecell/game.h"
#include "freecell/layout.h"

static int failures, checks;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            printf("FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond);         \
            printf(__VA_ARGS__);                                            \
            printf("\n");                                                   \
        }                                                                   \
    } while (0)

#define FRAME 10

static FcLayout L;

static int dist(CeRect a, CeRect b)
{
    int dx = b.x - a.x, dy = b.y - a.y;
    return ce_isqrt((unsigned long)(dx * dx + dy * dy));
}

static int xp_ms(CeRect a, CeRect b) { return ce_flight_ms_xp(dist(a, b), L.anim_px_per_frame, FRAME); }
static int new_ms(CeRect a, CeRect b) { return ce_flight_ms(dist(a, b), (int)(L.s * 1000 + 0.5), L.anim_px_per_frame, FRAME); }

static void move(const char *what, const FcBoard *b, int sc, int sp, int dc, int dp)
{
    FcBoard after = *b;
    CeRect from = fc_layout_card_rect(&L, b, sc, sp), to;
    int x, n;
    if (dc == 0) {
        after.board[0][dp] = b->board[sc][sp];
    } else {
        int last = fc_last_index(b, dc);
        after.board[dc][last + 1] = b->board[sc][sp];
        dp = last + 1;
    }
    if (sc >= 1)
        after.board[sc][sp] = FC_EMPTY;
    to = fc_layout_card_rect(&L, &after, dc, dp);
    x = xp_ms(from, to);
    n = new_ms(from, to);
    printf("  %-34s %5d px  %4d ms -> %4d ms (%s)\n", what, dist(from, to), x, n, dc == 0 && dp >= 4 ? "decelerate" :
           "standard");
    CHECK(n <= x, "%s: %d ms, XP %d ms", what, n, x);
}

/* A cascade: before, one card after another (each the XP straight flight); now each starts at 60% of the
 * previous one's time, and later only to land after the card before it on the same pile. */
typedef struct Casc { int xp, start, end, last_end[16], n; } Casc;

static void casc_add(Casc *c, CeRect from, CeRect to, int pile)
{
    int dur = new_ms(from, to), t0 = c->start + ce_flight_delay(dur, c->last_end[pile] - c->start);
    c->xp += xp_ms(from, to);
    c->last_end[pile] = t0 + dur;
    if (t0 + dur > c->end)
        c->end = t0 + dur;
    c->start = t0 + ce_cascade_ms(dur);
    c->n++;
}

int main(void)
{
    FcBoard b;
    Casc c;
    int col, k, r;
    fc_layout_compute(&L, 1904, 996);
    printf("  FreeCell at 1904 x 996 (s = %.3f, card %d x %d, XP pace %d px per %d-ms frame)\n", L.s, L.cw, L.ch,
           L.anim_px_per_frame, FRAME);
    fc_deal(&b, 1);
    move("column 4 -> column 5 (a short hop)", &b, 4, fc_last_index(&b, 4), 5, 0);
    move("column 1 -> free cell 0", &b, 1, fc_last_index(&b, 1), 0, 0);
    move("column 1 -> home cell 7", &b, 1, fc_last_index(&b, 1), 0, 7);
    move("column 8 -> free cell 0 (far)", &b, 8, fc_last_index(&b, 8), 0, 0);

    /* autoplay of the four aces, from the tops of columns 1, 3, 5, 7 to the home cells */
    memset(&c, 0, sizeof c);
    for (k = 0; k < 4; k++) {
        CeRect from = fc_layout_card_rect(&L, &b, 1 + 2 * k, fc_last_index(&b, 1 + 2 * k));
        casc_add(&c, from, L.top[4 + k], 4 + k);
    }
    printf("  %-34s %4d ms -> %4d ms\n", "autoplay of 4 aces", c.xp, c.end);
    CHECK(c.end < c.xp, "autoplay %d ms, XP %d", c.end, c.xp);

    /* Finish: four columns K..A of one suit each (the aces on top), every card home, lowest first */
    memset(&b, 0, sizeof b);
    for (col = 0; col < 9; col++)
        for (k = 0; k < FC_COLLEN; k++)
            b.board[col][k] = FC_EMPTY;
    for (col = 1; col <= 4; col++)
        for (k = 0; k < 13; k++)
            b.board[col][k] = (12 - k) * 4 + (col - 1);
    b.cards_left = 52;
    memset(&c, 0, sizeof c);
    for (r = 0; r < 13; r++)
        for (col = 1; col <= 4; col++) {
            int last = fc_last_index(&b, col);
            CeRect from = fc_layout_card_rect(&L, &b, col, last);
            casc_add(&c, from, L.top[4 + col - 1], 4 + col - 1);
            b.board[0][4 + col - 1] = b.board[col][last];
            b.board[col][last] = FC_EMPTY;
        }
    printf("  %-34s %4d ms -> %4d ms\n", "Finish, 52 cards", c.xp, c.end);
    CHECK(c.end < c.xp * 3 / 4, "Finish %d ms, XP %d", c.end, c.xp);
    printf("test_motion: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
