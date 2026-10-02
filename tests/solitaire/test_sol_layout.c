/*
 * Solitaire HD — native tests of src/solitaire/layout.c, render.c and winanim.c: XP's coordinates at
 * s = 1 (layout.md §1-§3, live-measured values), XP's horizontal spread, scaling, column compression,
 * hit testing and drop targets (rules.md §2.2-§2.3, the foundation-edge bug fixed), the waste fan
 * (rules.md §3.2), clipped rendering, the empty-pile pictures, the backs, and the win cascade against
 * the logged cascades of XP's sol.exe (layout.md §8: every frame of two wins).
 *
 *   test_sol_layout [res dir]     (default "res")
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sol_assets_native.h"
#include "solitaire/game.h"
#include "solitaire/layout.h"
#include "solitaire/render.h"
#include "solitaire/winanim.h"

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

static SolCard card(const char *s)   /* "9C" face up, "#9C" face down */
{
    const char *ranks = "A23456789TJQK", *suits = "CDHS";
    int up = 1, c;
    if (*s == '#') {
        up = 0;
        s++;
    }
    c = (int)(strchr(ranks, s[0]) - ranks) * 4 + (int)(strchr(suits, s[1]) - suits);
    return (SolCard)(up ? c | SOL_UP : c);
}

/* pile gets the space-separated cards of list, bottom first */
static void set_pile(SolBoard *b, int pile, const char *list)
{
    char buf[256], *t;
    SolPile *p = &b->p[pile];
    p->n = 0;
    snprintf(buf, sizeof buf, "%s", list);
    for (t = strtok(buf, " "); t; t = strtok(NULL, " "))
        p->c[p->n++] = card(t);
}

static int rect_eq(CeRect r, int x, int y, int w, int h)
{
    return r.x == x && r.y == y && r.w == w && r.h == h;
}

static void pos(const SolLayout *l, const SolBoard *b, int fan, int pile, int i, int want_x, int want_y, int line)
{
    int x, y;
    sol_layout_card_pos(l, b, fan, pile, i, &x, &y);
    CHECK(x == want_x && y == want_y, "line %d: pile %d card %d at (%d,%d), want (%d,%d)", line, pile, i, x, y,
          want_x, want_y);
}
#define POS(l, b, fan, pile, i, x, y) pos(l, b, fan, pile, i, x, y, __LINE__)

/* ---- XP at s = 1 --------------------------------------------------------------------------------- */

static void test_xp_geometry(void)
{
    SolLayout l;
    SolBoard b;
    uint32_t rng;
    int k;
    sol_layout_compute(&l, 585, 384, 18);
    CHECK(l.s == 1.0, "s %f", l.s);
    CHECK(l.cw == 71 && l.ch == 96, "card %dx%d", l.cw, l.ch);
    CHECK(l.gap == 11 && l.top == 5 && l.tab_y == 107, "g %d top %d tab %d", l.gap, l.top, l.tab_y);
    CHECK(l.step_dn == 3 && l.step_up == 15 && l.fan_dx == 14 && l.fan_dy == 1, "steps %d %d fan %d %d",
          l.step_dn, l.step_up, l.fan_dx, l.fan_dy);
    CHECK(l.edge_dx == 2 && l.edge_dy == 1, "edge %d %d", l.edge_dx, l.edge_dy);
    /* layout.md §2.1: (11,5)-(92,106), (93,5)-(196,106), (257,5)-(334,106).., (11,107)-(82,401).. */
    CHECK(rect_eq(l.pile[SOL_STOCK], 11, 5, 81, 101), "stock");
    CHECK(rect_eq(l.pile[SOL_WASTE], 93, 5, 103, 101), "waste");
    for (k = 0; k < 4; k++)
        CHECK(rect_eq(l.pile[SOL_FOUND0 + k], 257 + 82 * k, 5, 77, 101), "foundation %d", k);
    for (k = 0; k < 7; k++)
        CHECK(rect_eq(l.pile[SOL_TAB0 + k], 11 + 82 * k, 107, 71, 294), "column %d", k);
    CHECK(rect_eq(l.status, -1, 367, 587, 18), "status %d,%d %dx%d", l.status.x, l.status.y, l.status.w,
          l.status.h);
    CHECK(l.zip_px_per_frame == 36 && l.line == 1, "zip %d", l.zip_px_per_frame);
    /* no status bar: still s = 1 (width-limited) */
    sol_layout_compute(&l, 585, 384, 0);
    CHECK(l.s == 1.0 && l.status.w == 0, "s %f", l.s);

    /* the deal of the XP capture v01_launch.png (seed 27694): stock top at (15,7) (24 cards, 3 layers),
     * column 7's face-up card at y = 107 + 6 * 3 */
    sol_layout_compute(&l, 585, 384, 18);
    sol_deal_board(&b, 27694, &rng);
    CHECK(b.p[SOL_STOCK].n == 24, "stock %d", b.p[SOL_STOCK].n);
    POS(&l, &b, 0, SOL_STOCK, 23, 15, 7);
    POS(&l, &b, 0, SOL_STOCK, 9, 11, 5);
    POS(&l, &b, 0, SOL_STOCK, 10, 13, 6);
    POS(&l, &b, 0, SOL_TAB0 + 6, 6, 503, 125);
    POS(&l, &b, 0, SOL_TAB0 + 0, 0, 11, 107);
    POS(&l, &b, 0, SOL_TAB0 + 3, 3, 257, 116);
    /* a full deck in the stock: 6 layers up to (21,10) */
    set_pile(&b, SOL_STOCK, "#AC #2C #3C #4C #5C #6C #7C #8C #9C #TC #JC #QC #KC #AD #2D #3D #4D #5D #6D #7D "
                            "#8D #9D #TD #JD #QD #KD #AH #2H #3H #4H #5H #6H #7H #8H #9H #TH #JH #QH #KH #AS "
                            "#2S #3S #4S #5S #6S #7S #8S #9S #TS #JS #QS #KS");
    POS(&l, &b, 0, SOL_STOCK, 51, 21, 10);

    /* foundations: +2,+1 after cards 4, 8, 12; K at (+6,+3): K of spades lands at (509,8) */
    set_pile(&b, SOL_FOUND0 + 3, "AS 2S 3S 4S 5S 6S 7S 8S 9S TS JS QS KS");
    POS(&l, &b, 0, SOL_FOUND0 + 3, 12, 509, 8);
    POS(&l, &b, 0, SOL_FOUND0 + 3, 3, 503, 5);
    POS(&l, &b, 0, SOL_FOUND0 + 3, 4, 505, 6);
    POS(&l, &b, 0, SOL_FOUND0 + 3, 8, 507, 7);
}

/* The waste fan (rules.md §3.2, live-measured positions) */
static void test_waste_fan(void)
{
    SolLayout l;
    SolBoard b;
    int i;
    sol_layout_compute(&l, 585, 384, 18);
    sol_board_clear(&b);
    /* the first draw: 7H@(93,5) JC@(107,6) 4D@(121,7) */
    set_pile(&b, SOL_WASTE, "7H JC 4D");
    POS(&l, &b, 3, SOL_WASTE, 0, 93, 5);
    POS(&l, &b, 3, SOL_WASTE, 1, 107, 6);
    POS(&l, &b, 3, SOL_WASTE, 2, 121, 7);
    /* the next: 7H JC 4D 2D all @(93,5), QC@(107,6), 8H@(121,7) */
    set_pile(&b, SOL_WASTE, "7H JC 4D 2D QC 8H");
    for (i = 0; i < 4; i++)
        POS(&l, &b, 3, SOL_WASTE, i, 93, 5);
    POS(&l, &b, 3, SOL_WASTE, 4, 107, 6);
    POS(&l, &b, 3, SOL_WASTE, 5, 121, 7);
    /* the top card played: the rest of the fan stays (fan 2) */
    b.p[SOL_WASTE].n = 5;
    POS(&l, &b, 2, SOL_WASTE, 4, 107, 6);
    /* all 24 in the waste: (97,7), (111,8), (125,9) */
    set_pile(&b, SOL_WASTE, "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC QC KC AD 2D 3D 4D 5D 6D 7D 8D 9D TD JD");
    POS(&l, &b, 3, SOL_WASTE, 21, 97, 7);
    POS(&l, &b, 3, SOL_WASTE, 22, 111, 8);
    POS(&l, &b, 3, SOL_WASTE, 23, 125, 9);
    POS(&l, &b, 3, SOL_WASTE, 20, 97, 7);   /* collapsed, under the fan's first card */
    POS(&l, &b, 3, SOL_WASTE, 19, 95, 6);
    /* draw one: each card on the previous one, no fan */
    set_pile(&b, SOL_WASTE, "8H 9C");
    POS(&l, &b, 1, SOL_WASTE, 1, 93, 5);
    /* the fan's first card on a layer boundary: on the card below it (one layer lower than collapsed) */
    set_pile(&b, SOL_WASTE, "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC QC");
    POS(&l, &b, 2, SOL_WASTE, 10, 93, 5);
    POS(&l, &b, 2, SOL_WASTE, 11, 107, 6);
    POS(&l, &b, 0, SOL_WASTE, 11, 95, 6);
    /* fan larger than the pile is clamped */
    set_pile(&b, SOL_WASTE, "AC");
    POS(&l, &b, 3, SOL_WASTE, 0, 93, 5);
}

/* XP's horizontal spread (layout.md §1.1, live gaps) and the HD scale */
static void test_scaling(void)
{
    static const struct { int w, g, f0; } spread[] = {
        { 593, 12, 261 }, { 600, 12, 261 }, { 640, 17, 281 }, { 800, 37, 361 }, { 1024, 65, 473 }, { 1800, 162, 861 }
    };
    SolLayout l;
    SolBoard b;
    unsigned k;
    int w, h, x, y;
    for (k = 0; k < sizeof spread / sizeof spread[0]; k++) {
        sol_layout_compute(&l, spread[k].w, 384, 18);
        CHECK(l.s == 1.0 && l.gap == spread[k].g && l.pile[SOL_FOUND0].x == spread[k].f0,
              "W %d: s %f g %d f0 %d", spread[k].w, l.s, l.gap, l.pile[SOL_FOUND0].x);
    }
    sol_layout_compute(&l, 1800, 384, 18);
    CHECK(l.pile[SOL_TAB0 + 6].x == 1560, "col 7 at %d", l.pile[SOL_TAB0 + 6].x);

    /* 1080p maximised: s = 979 / 367, cards 189 x 256, the board spread over the width */
    sol_layout_compute(&l, 1904, 996, 18);
    CHECK(l.s > 2.66 && l.s < 2.67, "s %f", l.s);
    CHECK(l.cw == 189 && l.ch == 256, "card %dx%d", l.cw, l.ch);
    CHECK(l.gap == (1904 - 7 * 189) / 8, "gap %d", l.gap);
    CHECK(l.pile[SOL_TAB0 + 6].x + l.cw + l.gap <= 1904, "right edge");
    CHECK(l.pile[SOL_FOUND0].x == l.pile[SOL_TAB0 + 3].x, "foundations above columns 4-7");
    CHECK(l.tab_y >= l.top + 5 * l.edge_dy + l.ch + 1, "rows apart");
    /* a dealt column of 6 face-down + 10 face-up cards fits without compression */
    sol_board_clear(&b);
    set_pile(&b, SOL_TAB0, "#AC #2C #3C #4C #5C #6C KH QS JH TS 9H 8S 7H 6S 5H 4S");
    CHECK(sol_layout_col_step(&l, &b, SOL_TAB0) == l.step_up, "step %d", sol_layout_col_step(&l, &b, SOL_TAB0));
    sol_layout_card_pos(&l, &b, 0, SOL_TAB0, 15, &x, &y);
    CHECK(y + l.ch <= l.bottom_limit, "bottom %d > %d", y + l.ch, l.bottom_limit);

    /* the window helpers */
    sol_layout_client_for_scale(1.0, 18, &w, &h);
    CHECK(w == 585 && h == 384, "client for s 1: %dx%d", w, h);
    sol_layout_client_for_scale(2.0, 18, &w, &h);
    sol_layout_compute(&l, w, h, 18);
    CHECK(l.s == 2.0 && l.cw == 142 && l.ch == 192 && l.gap == 22, "s 2: %f %d %d %d", l.s, l.cw, l.ch, l.gap);
    sol_layout_min_client(18, &w, &h);
    sol_layout_compute(&l, w, h, 18);
    CHECK(l.s >= 0.5 && l.s < 0.51, "min s %f", l.s);
    /* height-limited and wide: XP's spread; tiny: clamped at 0.5, XP's minimum gap */
    sol_layout_compute(&l, 1904, 400, 18);
    CHECK(l.s < 1.06 && l.gap > 100, "wide s %f gap %d", l.s, l.gap);
    sol_layout_compute(&l, 100, 100, 18);
    CHECK(l.s == 0.5 && l.gap == 6, "tiny s %f gap %d", l.s, l.gap);
}

static void test_compression(void)
{
    SolLayout l;
    SolBoard b;
    int x, y, st;
    sol_layout_compute(&l, 585, 384, 18);
    sol_board_clear(&b);
    /* 6 face-down + K..A: 19 cards, XP's capacity. XP ends it at 401, under the status bar (367) */
    set_pile(&b, SOL_TAB0, "#2C #3C #4C #5C #6C #7C KH QS JH TS 9H 8S 7H 6S 5H 4S 3H 2S AH");
    st = sol_layout_col_step(&l, &b, SOL_TAB0);
    CHECK(st == (364 - 107 - 96 - 18) / 12, "step %d", st);
    sol_layout_card_pos(&l, &b, 0, SOL_TAB0, 18, &x, &y);
    CHECK(y + 96 <= l.bottom_limit && y == 107 + 18 + 12 * st, "last at %d", y);
    sol_layout_card_pos(&l, &b, 0, SOL_TAB0, 6, &x, &y);
    CHECK(y == 125, "first face-up at %d", y);
    /* the scale follows the height, so a 19-card column always fits at about 0.12 ch; the floor holds
     * when there is less room (here faked) */
    sol_layout_compute(&l, 585, 250, 18);
    st = sol_layout_col_step(&l, &b, SOL_TAB0);
    CHECK(st > l.step_up_min && st < l.step_up, "short window: step %d", st);
    l.bottom_limit = l.tab_y + l.ch + 40;
    CHECK(sol_layout_col_step(&l, &b, SOL_TAB0) == l.step_up_min, "min step %d", sol_layout_col_step(&l, &b, SOL_TAB0));
    /* other columns keep XP's step */
    set_pile(&b, SOL_TAB0 + 1, "#2D KS QD");
    CHECK(sol_layout_col_step(&l, &b, SOL_TAB0 + 1) == l.step_up, "short column");
    /* at 1080p even the longest column fits at a readable step */
    sol_layout_compute(&l, 1904, 996, 18);
    st = sol_layout_col_step(&l, &b, SOL_TAB0);
    sol_layout_card_pos(&l, &b, 0, SOL_TAB0, 18, &x, &y);
    CHECK(st < l.step_up && st >= l.step_up_min && y + l.ch <= l.bottom_limit, "1080p step %d end %d", st, y + l.ch);
}

/* "Large print cards" (2c): the face-up step round(21 ch / 96) (ce_large_print_step), the rest as without it
 * (the empty columns' drop zones follow the step: 6 face-down + 12 face-up steps + ch); compression as usual. */
static void test_large_print_layout(void)
{
    SolLayout n, l;
    SolBoard b;
    int x, y, k, st, w, h;
    sol_layout_compute(&n, 585, 384, 18);
    sol_layout_compute_ex(&l, 585, 384, 18, 1);
    CHECK(!n.large_print && l.large_print && n.step_up == 15 && l.step_up == 21 && l.step_dn == 3,
          "steps %d %d", n.step_up, l.step_up);
    for (k = 0; k < 7; k++)
        CHECK(rect_eq(l.pile[SOL_TAB0 + k], 11 + 82 * k, 107, 71, 18 + 12 * 21 + 96), "column %d", k);
    CHECK(rect_eq(l.pile[SOL_STOCK], 11, 5, 81, 101) && l.tab_y == n.tab_y && l.step_up_min == n.step_up_min &&
          l.bottom_limit == n.bottom_limit, "the rest");
    l.step_up = n.step_up;
    l.large_print = 0;
    for (k = SOL_TAB0; k < SOL_NPILES; k++)
        l.pile[k] = n.pile[k];
    CHECK(!memcmp(&l, &n, sizeof l), "nothing else differs");
    sol_layout_compute_ex(&l, 1904, 996, 18, 1);
    CHECK(l.ch == 256 && l.step_up == 56, "1080p: ch %d step %d", l.ch, l.step_up);
    for (w = 293; w <= 4000; w += 61)
        for (h = 200; h <= 2400; h += 37) {
            sol_layout_compute_ex(&l, w, h, 18, 1);
            sol_layout_compute(&n, w, h, 18);
            CHECK(l.step_up == (l.ch * 21 + 48) / 96 && l.step_up >= n.step_up && l.step_up_min <= l.step_up &&
                  l.ch == n.ch, "invariants at %dx%d", w, h);
        }
    /* the deal: column 7's face-up card at 107 + 6 * 3 as before; then the face-up cards 21 apart */
    sol_layout_compute_ex(&l, 585, 384, 18, 1);
    sol_board_clear(&b);
    set_pile(&b, SOL_TAB0, "#2C #3C KH QS JH TS 9H");
    sol_layout_card_pos(&l, &b, 0, SOL_TAB0, 2, &x, &y);
    CHECK(y == 107 + 2 * 3 + 0 * 21, "first face-up at %d", y);
    sol_layout_card_pos(&l, &b, 0, SOL_TAB0, 6, &x, &y);
    CHECK(y == 107 + 2 * 3 + 4 * 21, "fifth face-up at %d", y);
    CHECK(sol_layout_col_step(&l, &b, SOL_TAB0) == 21, "a short column keeps the step");
    /* XP's 19-card capacity: compressed to end above the status bar, never below 0.10 ch */
    set_pile(&b, SOL_TAB0, "#2C #3C #4C #5C #6C #7C KH QS JH TS 9H 8S 7H 6S 5H 4S 3H 2S AH");
    st = sol_layout_col_step(&l, &b, SOL_TAB0);
    CHECK(st == (364 - 107 - 96 - 18) / 12, "step %d", st);
    l.bottom_limit = l.tab_y + l.ch + 40;
    CHECK(sol_layout_col_step(&l, &b, SOL_TAB0) == l.step_up_min, "min step");
}

/* ---- hit testing and drop targets ---------------------------------------------------------------- */

static int accept_all(void *ctx, int pile) { (void)ctx; (void)pile; return 1; }
static int accept_tab(void *ctx, int pile) { (void)ctx; return pile >= SOL_TAB0; }

static void hit(const SolLayout *l, const SolBoard *b, int fan, int x, int y, int ret, int pile, int card, int line)
{
    int p, c, r = sol_layout_hit(l, b, fan, x, y, &p, &c);
    CHECK(r == ret && p == pile && c == card, "line %d: (%d,%d) -> %d pile %d card %d, want %d %d %d", line, x, y, r,
          p, c, ret, pile, card);
}
#define HIT(x, y, ret, pile, card) hit(&l, &b, fan, x, y, ret, pile, card, __LINE__)

static void test_hits(void)
{
    SolLayout l;
    SolBoard b;
    int fan = 3, t;
    sol_layout_compute(&l, 585, 384, 18);
    sol_board_clear(&b);
    set_pile(&b, SOL_STOCK, "#AC #2C #3C #4C #5C #6C #7C #8C #9C #TC #JC #QC");   /* top at (13,6) */
    set_pile(&b, SOL_WASTE, "7H JC 4D");
    set_pile(&b, SOL_FOUND0, "AS 2S 3S 4S 5S 6S 7S 8S 9S TS JS QS KS");            /* top at (263,8) */
    set_pile(&b, SOL_TAB0 + 1, "#2D #3D 9H 8S");                                    /* 93: 107,110,113,128 */
    /* stock: the top card's rect only (half-open) */
    HIT(13, 6, 1, SOL_STOCK, 11);
    HIT(83, 101, 1, SOL_STOCK, 11);
    HIT(84, 101, 0, -1, -1);
    HIT(12, 6, 0, -1, -1);                  /* the 3-D edge strip of a lower layer */
    /* waste: the top card only, not the fanned cards under it */
    HIT(121, 7, 1, SOL_WASTE, 2);
    HIT(100, 50, 0, -1, -1);
    /* foundation: the top card; the 3-D edge (XP picked up 4S..KS from (258,60)) is a miss */
    HIT(300, 50, 1, SOL_FOUND0, 12);
    HIT(258, 60, 0, -1, -1);
    /* tableau: the topmost card under the point, face down or not; below the last card a miss */
    HIT(100, 108, 1, SOL_TAB0 + 1, 0);
    HIT(100, 111, 1, SOL_TAB0 + 1, 1);
    HIT(100, 120, 1, SOL_TAB0 + 1, 2);
    HIT(100, 200, 1, SOL_TAB0 + 1, 3);
    HIT(100, 224, 0, -1, -1);
    HIT(100, 223, 1, SOL_TAB0 + 1, 3);
    HIT(30, 150, 0, -1, -1);                /* an empty column: nothing to pick up */
    HIT(88, 150, 0, -1, -1);                /* between columns */
    /* the empty stock: its card slot, card -1 */
    b.p[SOL_STOCK].n = 0;
    HIT(11, 5, 1, SOL_STOCK, -1);
    HIT(81, 100, 1, SOL_STOCK, -1);
    HIT(82, 100, 0, -1, -1);

    /* v1.2 (click to select): an empty pile's whole rect is a destination; full piles, the stock: no */
    CHECK(sol_layout_hit_empty(&l, &b, 30, 150) == SOL_TAB0, "empty column %d", sol_layout_hit_empty(&l, &b, 30, 150));
    CHECK(sol_layout_hit_empty(&l, &b, 30, 390) == SOL_TAB0, "empty column, low");
    CHECK(sol_layout_hit_empty(&l, &b, 350, 50) == SOL_FOUND0 + 1, "empty foundation");
    CHECK(sol_layout_hit_empty(&l, &b, 300, 50) == -1, "a full foundation");
    CHECK(sol_layout_hit_empty(&l, &b, 100, 120) == -1, "a full column");
    CHECK(sol_layout_hit_empty(&l, &b, 30, 50) == -1, "the stock");
    CHECK(sol_layout_hit_empty(&l, &b, 88, 150) == -1, "between columns");
    b.p[SOL_WASTE].n = 0;
    CHECK(sol_layout_hit_empty(&l, &b, 121, 7) == SOL_WASTE, "the empty waste");
    set_pile(&b, SOL_WASTE, "7H JC 4D");

    /* drop zones: the top card, or the whole pile rect when empty */
    CHECK(rect_eq(sol_layout_drop_zone(&l, &b, fan, SOL_TAB0), 11, 107, 71, 294), "empty column zone");
    CHECK(rect_eq(sol_layout_drop_zone(&l, &b, fan, SOL_TAB0 + 1), 93, 128, 71, 96), "column zone");
    CHECK(rect_eq(sol_layout_drop_zone(&l, &b, fan, SOL_FOUND0 + 1), 339, 5, 77, 101), "empty foundation zone");
    /* XP's example: 5S overlapping 6H (column 2) by 4 px and 6D (column 3) by 56 px goes onto 6H */
    sol_board_clear(&b);
    set_pile(&b, SOL_TAB0 + 1, "6H");
    set_pile(&b, SOL_TAB0 + 2, "6D");
    t = sol_layout_drop_target(&l, &b, fan, 93 + 71 - 4 + 0, 150, -1, accept_all, NULL);
    CHECK(t == SOL_TAB0 + 1, "target %d", t);
    t = sol_layout_drop_target(&l, &b, fan, 160, 150, -1, accept_all, NULL);   /* 4 px over 6H */
    CHECK(t == SOL_TAB0 + 1, "target %d", t);
    t = sol_layout_drop_target(&l, &b, fan, 164, 150, -1, accept_all, NULL);   /* touching only 6D */
    CHECK(t == SOL_TAB0 + 2, "target %d", t);
    t = sol_layout_drop_target(&l, &b, fan, 160, 150, SOL_TAB0 + 1, accept_all, NULL);
    CHECK(t == SOL_TAB0 + 2, "skipping the source: %d", t);
    /* first in index order: an empty column's tall zone, and the foundation above column 4 */
    t = sol_layout_drop_target(&l, &b, fan, 20, 300, -1, accept_all, NULL);
    CHECK(t == SOL_TAB0, "empty column band %d", t);
    t = sol_layout_drop_target(&l, &b, fan, 257, 60, -1, accept_all, NULL);
    CHECK(t == SOL_FOUND0, "foundation first %d", t);
    t = sol_layout_drop_target(&l, &b, fan, 257, 60, -1, accept_tab, NULL);
    CHECK(t == SOL_TAB0 + 3, "column under it %d", t);
    t = sol_layout_drop_target(&l, &b, fan, 1000, 1000, -1, accept_all, NULL);
    CHECK(t == -1, "none %d", t);
}

/* ---- rendering ----------------------------------------------------------------------------------- */

static int same_region(const CeImage *a, const CeImage *b, CeRect r)
{
    int x, y;
    for (y = r.y; y < r.y + r.h; y++)
        for (x = r.x; x < r.x + r.w; x++)
            if (a->px[(size_t)y * a->stride + x] != b->px[(size_t)y * b->stride + x])
                return 0;
    return 1;
}

static void mid_game(SolBoard *b)
{
    sol_board_clear(b);
    set_pile(b, SOL_STOCK, "#AC #2C #3C #4C #5C #6C #7C #8C #9C #TC #JC #QC");
    set_pile(b, SOL_WASTE, "KC AD 2D 3D 4D");
    set_pile(b, SOL_FOUND0 + 1, "AH 2H 3H 4H 5H");
    set_pile(b, SOL_TAB0, "KS QH JS TH 9S 8H 7S");
    set_pile(b, SOL_TAB0 + 2, "#5D #6D 9D 8C 7D");
    set_pile(b, SOL_TAB0 + 4, "#7H #8D #9C #TD #JD QC JH TS 9H 8S 7H 6S 5H 4S 3H 2S AS 6C 5C");
    set_pile(b, SOL_TAB0 + 6, "#KD #QD #JC #TC #9D #8H #7C");
}

static uint32_t px_at(const CeImage *img, int x, int y) { return img->px[(size_t)y * img->stride + x] & 0xFFFFFFu; }

static void test_render(SolGfx *g)
{
    static const int sizes[3][2] = { { 585, 384 }, { 1264, 669 }, { 1904, 996 } };
    SolLayout l;
    SolBoard b, bb;
    SolView v;
    CeImage *full, *part;
    int k, i, m;
    mid_game(&b);
    for (k = 0; k < 3; k++) {
        int W = sizes[k][0], H = sizes[k][1];
        sol_layout_compute(&l, W, H, 18);
        sol_render_prepare(g, &l, 1);
        for (m = 0; m < 4; m++) {
            sol_view_init(&v);
            v.waste_fan = 2;
            v.back = (k * 4 + m) % SOL_NBACKS;
            v.stock_x = m & 1;
            if (m == 1) {                                /* a full drag of 9D 8C 7D */
                v.drag_pile = SOL_TAB0 + 2;
                v.drag_card = 2;
                v.drag_x = W / 2 + 3;
                v.drag_y = H / 3 + 1;
            } else if (m == 2) {                         /* outline dragging, a target */
                v.drag_pile = SOL_TAB0 + 4;
                v.drag_card = 13;
                v.drag_mode = SOL_DRAG_OUTLINE;
                v.drag_x = W / 4;
                v.drag_y = H / 2;
                v.target = SOL_TAB0 + 1;
            } else if (m == 3) {                         /* keyboard selection, the stock emptied */
                v.sel_pile = SOL_TAB0;
                v.sel_card = 4;
            }
            bb = b;
            if (m == 3)
                bb.p[SOL_STOCK].n = 0;
            full = ce_image_new(W, H);
            part = ce_image_new(W, H);
            sol_render_board(full, &l, &bb, &v, g);
            ce_fill_rect(part, 0, 0, W, H, CE_RGB(1, 2, 3));
            /* a patchwork of odd rects covering the board gives the full render's pixels */
            for (i = 0; i < 7; i++) {
                int y0 = H * i / 7, y1 = H * (i + 1) / 7, x0 = (W * i) / 13;
                sol_render_board_rect(part, &l, &bb, &v, g, ce_rect(0, y0, x0, y1 - y0));
                sol_render_board_rect(part, &l, &bb, &v, g, ce_rect(x0, y0, W - x0, y1 - y0));
            }
            CHECK(same_region(full, part, ce_rect(0, 0, W, H)), "clipped render %dx%d mode %d", W, H, m);
            ce_image_free(full);
            ce_image_free(part);
        }
    }

    /* XP's look at s = 1: table, empty foundation dots, empty stock O, nothing for an empty column */
    sol_layout_compute(&l, 585, 384, 18);
    sol_render_prepare(g, &l, 1);
    sol_board_clear(&b);
    sol_view_init(&v);
    full = ce_image_new(585, 384);
    sol_render_board(full, &l, &b, &v, g);
    CHECK(px_at(full, 5, 300) == 0x008000, "table %06x", px_at(full, 5, 300));
    CHECK(px_at(full, 40, 200) == 0x008000, "empty column");
    /* the O at the stock (11,5): XP's ring at x 7..13 on row 48, rows 18..24 on column 35; the hole */
    CHECK(px_at(full, 11 + 10, 5 + 48) == 0x00FF00 && px_at(full, 11 + 35, 5 + 21) == 0x00FF00, "O ring %06x",
          px_at(full, 11 + 10, 5 + 48));
    CHECK(px_at(full, 11 + 35, 5 + 48) == 0x008000 && px_at(full, 11 + 35, 5 + 30) == 0x008000, "O hole");
    /* the ghost: XP's dot lattice (bitmap 53) inside a black outline */
    {
        int x, y, bad = 0, dots = 0;
        for (y = 3; y < 93; y++)
            for (x = 3; x < 68; x++) {
                int want = ((y % 4 == 2) && (x % 4 == 3)) || ((y % 8 == 4) && (x % 8 == 5)) ||
                           ((y % 8 == 0) && (x % 8 == 1));
                uint32_t p = px_at(full, 339 + x, 5 + y);
                bad += want ? p != 0 : p != 0x008000;
                dots += want;
            }
        CHECK(bad == 0 && dots > 400, "ghost lattice: %d wrong of %d dots", bad, dots);
        CHECK(px_at(full, 339 + 35, 5) == 0 && px_at(full, 339 + 70, 50) == 0, "ghost outline");
    }
    /* the Vegas X */
    v.stock_x = 1;
    sol_render_board(full, &l, &b, &v, g);
    CHECK(px_at(full, 11 + 36, 5 + 48) == 0xFF0000 && px_at(full, 11 + 12, 5 + 24) == 0xFF0000, "X");
    CHECK(px_at(full, 11 + 36, 5 + 25) == 0x008000, "X gap");
    /* outline dragging at s = 1 is XP's R2_NOT polyline (layout.md §6.2, drag/o01_mid.png): green
     * inverted is (255,127,255); the card lines cross the left edge twice (a notch), not the right */
    {
        SolBoard ob;
        sol_board_clear(&ob);
        set_pile(&ob, SOL_TAB0 + 6, "9S 8H 7S");
        sol_view_init(&v);
        v.drag_pile = SOL_TAB0 + 6;
        v.drag_card = 0;
        v.drag_mode = SOL_DRAG_OUTLINE;
        v.drag_x = 263;
        v.drag_y = 200;
        sol_render_board(full, &l, &ob, &v, g);
        CHECK(px_at(full, 263, 200) == 0xFF7FFF && px_at(full, 263 + 71, 200 + 126) == 0xFF7FFF, "outline corners");
        CHECK(px_at(full, 263 + 72, 200) == 0x008000 && px_at(full, 263, 200 + 127) == 0x008000, "outline size");
        CHECK(px_at(full, 263, 214) == 0xFF7FFF && px_at(full, 263, 215) == 0x008000 &&
              px_at(full, 264, 215) == 0xFF7FFF && px_at(full, 263 + 70, 215) == 0xFF7FFF &&
              px_at(full, 263 + 71, 215) == 0xFF7FFF && px_at(full, 263, 230) == 0x008000, "outline card lines");
        CHECK(px_at(full, 280, 216) == 0x008000, "outline inside");
        sol_view_init(&v);
        v.stock_x = 1;
    }
    /* 2d, the hint's pulse: sel_level 256 is the selection's inversion exactly, 0 none, 128 half way */
    {
        CeImage *x = ce_image_new(585, 384), *y = ce_image_new(585, 384);
        int sx, sy, lo, mid, hi;
        mid_game(&b);
        sol_view_init(&v);
        v.sel_pile = SOL_TAB0;
        v.sel_card = 4;
        sol_render_board(full, &l, &b, &v, g);                 /* (level 256: sol_view_init's) */
        v.sel_level = 0;
        sol_render_board(x, &l, &b, &v, g);
        sol_view_init(&v);
        sol_render_board(y, &l, &b, &v, g);
        CHECK(same_region(x, y, ce_rect(0, 0, 585, 384)), "level 0: no selection");
        v.sel_pile = SOL_TAB0;
        v.sel_card = 4;
        v.sel_level = 128;
        sol_render_board(x, &l, &b, &v, g);
        sol_layout_card_pos(&l, &b, 0, SOL_TAB0, b.p[SOL_TAB0].n - 1, &sx, &sy);
        lo = (int)(px_at(full, sx + 3, sy + 60) & 255);
        mid = (int)(px_at(x, sx + 3, sy + 60) & 255);
        hi = (int)(px_at(y, sx + 3, sy + 60) & 255);
        CHECK(lo < mid && mid < hi && mid > 100 && mid < 156, "level 128 half way: %d < %d < %d", lo, mid, hi);
        ce_image_free(x);
        ce_image_free(y);
    }
    /* not dealt: the table only */
    mid_game(&b);
    sol_view_init(&v);
    v.stock_x = 1;
    v.dealt = 0;
    sol_render_board(full, &l, &b, &v, g);
    CHECK(px_at(full, 300, 50) == 0x008000 && px_at(full, 20, 20) == 0x008000, "not dealt");
    ce_image_free(full);
}

static void test_stack_and_backs(SolGfx *g, SolNativeAssets *na)
{
    SolLayout l;
    SolBoard b;
    CeImage *st, *bi;
    CeRect r;
    int i, w, h, distinct = 0;
    uint32_t centre[SOL_NBACKS];
    sol_layout_compute(&l, 585, 384, 18);
    sol_render_prepare(g, &l, 1);
    mid_game(&b);
    st = sol_render_stack(g, &l, &b, 0, 0, SOL_TAB0 + 2, 2);
    r = sol_render_stack_rect(&l, &b, 0, SOL_TAB0 + 2, 2);
    sol_layout_stack_size(&l, &b, 0, SOL_TAB0 + 2, 2, &w, &h);
    CHECK(st && st->w == 71 && st->h == 2 * 15 + 96 && w == st->w && h == st->h, "stack %dx%d", st ? st->w : 0,
          st ? st->h : 0);
    CHECK(rect_eq(r, 175, 107 + 6, 71, 126), "stack rect %d,%d %dx%d", r.x, r.y, r.w, r.h);
    CHECK(st && (st->px[0] >> 24) == 0 && (st->px[(size_t)60 * st->stride + 30] >> 24) == 255, "corners / body");
    ce_image_free(st);
    CHECK(sol_render_stack(g, &l, &b, 0, 0, SOL_TAB0 + 1, 0) == NULL, "empty pile: no stack");
    {
        SolView v;
        sol_view_init(&v);
        CHECK(sol_render_drag_rect(&l, &b, &v).w == 0, "no drag rect");
        v.drag_pile = SOL_TAB0 + 2;
        v.drag_card = 2;
        v.drag_x = 300;
        v.drag_y = 200;
        r = sol_render_drag_rect(&l, &b, &v);
        CHECK(rect_eq(r, 300, 200, 71, 126), "drag rect");
        v.drag_mode = SOL_DRAG_OUTLINE;
        r = sol_render_drag_rect(&l, &b, &v);
        CHECK(rect_eq(r, 300, 200, 72, 127), "outline rect");
    }

    /* all 12 backs load, differ, have the card shape */
    for (i = 0; i < SOL_NBACKS; i++) {
        const CeImage *s = ce_cardset_back(sol_gfx_cards(g), i);
        CHECK(s && s->w == 71 && s->h == 96, "back %d (%s)", i, sol_back_names[i]);
        if (!s)
            continue;
        CHECK((s->px[0] >> 24) == 0 && (s->px[(size_t)48 * s->stride + 35] >> 24) == 255, "back %d shape", i);
        centre[i] = s->px[(size_t)30 * s->stride + 20];
    }
    for (i = 1; i < SOL_NBACKS; i++)
        distinct += centre[i] != centre[0];
    CHECK(distinct >= 9, "backs differ (%d)", distinct);
    /* the dialog's pictures */
    bi = sol_back_image(sol_native_asset_loader, na, 3, 45, 62);
    CHECK(bi && bi->w == 45 && bi->h == 62 && (bi->px[0] >> 24) < 64 &&
              (bi->px[(size_t)31 * bi->stride + 22] >> 24) == 255, "back image %08x %08x", bi ? bi->px[0] : 0,
          bi ? bi->px[(size_t)31 * bi->stride + 22] : 0);
    ce_image_free(bi);
    CHECK(sol_back_image(sol_native_asset_loader, na, SOL_NBACKS, 45, 62) == NULL, "no back 12");
}

/* ---- the win cascade against XP's logs ----------------------------------------------------------- */

/* Two cascades of XP's sol.exe logged frame by frame under Wine (layout.md §8; <sr>/visuals/
 * win1_gdilog.txt, natwin2_gdilog.txt), 585 x 384, foundations clubs, diamonds, hearts, spades: frames
 * per card in launch order (K of clubs, K of diamonds, .., A of spades) and a hash of every position. */
static const int xp_forced_frames[52] = {
    66, 82, 55, 28, 110, 205, 123, 287, 164, 205, 246, 144, 328, 205, 55, 28, 164, 205, 246, 28, 110, 246, 246,
    287, 164, 82, 99, 41, 82, 205, 246, 96, 110, 137, 246, 96, 82, 82, 246, 96, 82, 205, 82, 287, 164, 103, 492,
    115, 110, 205, 123, 287
};
static const int xp_natural_frames[52] = {
    322, 416, 83, 580, 108, 83, 160, 26, 166, 69, 248, 289, 166, 83, 166, 145, 56, 81, 54, 289, 330, 82, 494, 288,
    165, 206, 247, 27, 83, 206, 247, 288, 326, 82, 83, 116, 66, 123, 246, 144, 82, 410, 55, 192, 328, 62, 164, 41,
    164, 205, 246, 28
};

static void won_board(SolBoard *b)
{
    int f, r;
    sol_board_clear(b);
    for (f = 0; f < 4; f++) {
        b->p[SOL_FOUND0 + f].n = 13;
        for (r = 0; r < 13; r++)
            b->p[SOL_FOUND0 + f].c[r] = (SolCard)((r * 4 + f) | SOL_UP);
    }
}

/* Run the cascade; returns total frames, the position hash, per-card frame counts */
static long run_cascade(const SolLayout *l, const SolBoard *b, unsigned seed, int forced, int scale_div,
                        uint32_t *hash, int frames[52], int *first_ok)
{
    SolWinAnim a;
    SolBoard deal;
    uint32_t rng, h = 0;
    long tot = 0;
    int c, x, y, prev = -1, k = -1, fx, fy;
    sol_deal_board(&deal, seed, &rng);                  /* the rand state the deal left */
    sol_winanim_start(&a, l, b, rng, forced);
    *first_ok = 1;
    memset(frames, 0, sizeof(int) * 52);
    while (sol_winanim_frame(&a, &c, &x, &y)) {
        if (c != prev) {
            k++;
            prev = c;
            /* each card starts where it lies (or at the foundation origin after Alt+Shift+2) */
            if (forced) {
                fx = l->pile[SOL_FOUND0 + (c & 3)].x;
                fy = l->pile[SOL_FOUND0 + (c & 3)].y;
            } else {
                sol_layout_card_pos(l, b, 0, SOL_FOUND0 + (c & 3), c >> 2, &fx, &fy);
            }
            if (x != fx || y != fy || c != ((12 - k / 4) * 4 + k % 4))
                *first_ok = 0;
        }
        if (k >= 0 && k < 52)
            frames[k]++;
        x /= scale_div;
        y /= scale_div;
        h = h * 31 + (uint32_t)x;
        h = h * 31 + (uint32_t)y;
        tot++;
    }
    CHECK(a.frames == tot, "frame count %ld %ld", a.frames, tot);
    *hash = h;
    return tot;
}

static void test_winanim(void)
{
    SolLayout l;
    SolBoard b;
    uint32_t h;
    int frames[52], ok, i, same;
    long tot;
    won_board(&b);
    sol_layout_compute(&l, 585, 384, 18);
    /* Alt+Shift+2 on deal 28087: every card from its foundation's origin */
    tot = run_cascade(&l, &b, 28087, 1, 1, &h, frames, &ok);
    for (i = 0, same = 1; i < 52; i++)
        same &= frames[i] == xp_forced_frames[i];
    CHECK(tot == 8228 && h == 0x670bd56eu && same && ok, "XP forced win: %ld frames, hash %08x", tot, (unsigned)h);
    /* a natural win of deal 29785: the cards from their 3-D places (K at +6,+3) */
    tot = run_cascade(&l, &b, 29785, 0, 1, &h, frames, &ok);
    for (i = 0, same = 1; i < 52; i++)
        same &= frames[i] == xp_natural_frames[i];
    CHECK(tot == 9416 && h == 0xf7f4a3acu && same && ok, "XP natural win: %ld frames, hash %08x", tot, (unsigned)h);
    /* at s = 2 the same cascade, twice as large: same frames, every position doubled */
    sol_layout_compute(&l, 1170, 768, 18);
    CHECK(l.s == 2.0, "s %f", l.s);
    tot = run_cascade(&l, &b, 29785, 0, 2, &h, frames, &ok);
    for (i = 0, same = 1; i < 52; i++)
        same &= frames[i] == xp_natural_frames[i];
    CHECK(tot == 9416 && h == 0xf7f4a3acu && same && ok, "s = 2: %ld frames, hash %08x", tot, (unsigned)h);
    /* at 1080p: deterministic, every card leaves, a comparable length (the board is ~2.7x XP's) */
    sol_layout_compute(&l, 1904, 996, 18);
    tot = run_cascade(&l, &b, 29785, 0, 1, &h, frames, &ok);
    {
        uint32_t h2;
        int f2[52], ok2;
        long tot2 = run_cascade(&l, &b, 29785, 0, 1, &h2, f2, &ok2);
        CHECK(tot2 == tot && h2 == h && ok && ok2, "deterministic");
    }
    for (i = 0, same = 1; i < 52; i++)
        same &= frames[i] > 0;
    CHECK(same && tot > 5000 && tot < 30000, "1080p: %ld frames", tot);
    /* a missing card is skipped without a rand call; an empty board ends at once */
    {
        SolWinAnim a;
        int c, x, y;
        sol_board_clear(&b);
        sol_winanim_start(&a, &l, &b, 1, 0);
        CHECK(!sol_winanim_frame(&a, &c, &x, &y) && sol_winanim_rng(&a) == 1, "empty board");
    }
}

int main(int argc, char **argv)
{
    SolNativeAssets na;
    SolGfx *g;
    const char *res = argc > 1 ? argv[1] : "res";
    test_xp_geometry();
    test_waste_fan();
    test_scaling();
    test_compression();
    test_large_print_layout();
    test_hits();
    test_winanim();
    sol_native_assets_init(&na, res);
    g = sol_gfx_new(sol_native_asset_loader, &na);
    CHECK(g != NULL, "card assets from %s", res);
    if (g) {
        test_render(g);
        test_stack_and_backs(g, &na);
        sol_gfx_free(g);
    }
    sol_native_assets_free(&na);
    printf("test_sol_layout: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
