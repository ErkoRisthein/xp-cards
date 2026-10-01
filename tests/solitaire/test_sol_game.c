/*
 * Solitaire HD — native tests of the rules primitives (src/solitaire/game.c) against
 * docs/xp-reference/solitaire/rules.md: XP's exact deals (§1.3), the drop predicates (§2.1), the score
 * tables and arithmetic (§4), the Options / Back registry formats (§10), the status-bar score text
 * (§4.5), board packing and the structural invariants.
 */
#include "sol_test.h"

/* ---- deals (§1.3) ---------------------------------------------------------------------------------- */

typedef struct DealVec {
    unsigned seed;
    const char *stock;
    const char *tab[7];
} DealVec;

/* Seeds 27937, 28172 and 28481 were checked against the real sol.exe's memory under Wine (§1.3); seed 1
 * is from the reference implementation. (rules.md's listing of 28481 has a garbled T7, "#TC #8C #7D
 * #3S #JH #8S KC", with cards that appear twice; the verified model gives the row below.) */
static const DealVec deals[] = {
    { 27937,
      "#9C #2H #9D #QD #QH #5C #6H #TH #9S #6D #4C #8S #KD #2S #4H #AC #TD #KH #8H #QC #2D #4D #JC #7H",
      { "6S", "#JS 7S", "#AH #2C QS", "#8C #8D #5S 5H", "#AD #AS #7D #4S KS", "#KC #JD #TS #9H #3S 7C",
        "#3H #6C #3D #5D #3C #JH TC" } },
    { 28172,
      "#7H #4S #JH #5C #TD #KH #8C #AD #2S #6S #5D #JS #8S #KD #9H #4C #TS #3D #QD #TH #5H #8D #QH #3S",
      { "JC", "#9S 4D", "#KC #3H 8H", "#7D #9D #AS KS", "#6C #9C #6D #6H AH", "#QC #2H #2C #7S #4H JD",
        "#3C #7C #TC #5S #2D #AC QS" } },
    { 28481,
      "#5H #9H #4C #QS #AC #2C #QH #TS #7H #3D #7D #TH #KC #3S #AD #6S #KS #8D #2H #7C #QC #6H #3H #8S",
      { "6C", "#2D JC", "#6D #5C KH", "#JD #4S #8H TD", "#9S #AH #7S #TC AS", "#2S #QD #8C #4H #9C KD",
        "#5S #3C #4D #JH #5D #9D JS" } },
    { 1,
      "#AS #6D #8C #8H #8S #5H #JS #6C #JH #TS #QC #TC #TH #2D #4C #3C #2C #9C #AH #9S #9H #JD #4H #QS",
      { "5D", "#7H 6S", "#9D #QD TD", "#6H #5S #AC AD", "#KD #JC #2H #3D 7D", "#2S #7C #3S #KH #KS 4D",
        "#8D #4S #7S #KC #5C #3H QH" } },
};

static void test_deals(void)
{
    for (size_t k = 0; k < sizeof deals / sizeof deals[0]; k++) {
        const DealVec *v = &deals[k];
        SolBoard b;
        uint32_t rng = 0;
        sol_deal_board(&b, v->seed, &rng);
        CHECK(pile_is(&b, SOL_STOCK, v->stock));
        for (int t = 0; t < 7; t++) CHECK(pile_is(&b, SOL_TAB0 + t, v->tab[t]));
        CHECK_EQ(b.p[SOL_WASTE].n, 0);
        for (int f = 0; f < 4; f++) CHECK_EQ(b.p[SOL_FOUND0 + f].n, 0);
        CHECK(sol_board_valid(&b, NULL, 0));
        /* the rand state after the deal = 260 calls after srand(seed) */
        uint32_t x = v->seed;
        for (int i = 0; i < 260; i++) sol_rand(&x);
        CHECK_EQ(rng, x);
    }
    /* msvcrt rand from srand(1): 41, 18467, 6334, 26500 */
    uint32_t x = 1;
    CHECK_EQ(sol_rand(&x), 41);
    CHECK_EQ(sol_rand(&x), 18467);
    CHECK_EQ(sol_rand(&x), 6334);
    CHECK_EQ(sol_rand(&x), 26500);
    /* every deal of the 32768 is a legal board: 24 in the stock, column t holds t+1 with one face up */
    int bad = 0;
    for (unsigned seed = 0; seed < 32768; seed++) {
        SolBoard b;
        sol_deal_board(&b, seed, NULL);
        if (!sol_board_valid(&b, NULL, 0) || b.p[SOL_STOCK].n != 24) bad++;
        for (int t = 0; t < 7; t++)
            if (b.p[SOL_TAB0 + t].n != t + 1 || sol_up_run(&b, SOL_TAB0 + t) != 1) bad++;
    }
    CHECK_EQ(bad, 0);
}

/* ---- drop predicates (§2.1) ------------------------------------------------------------------------- */

static void test_can_drop(void)
{
    SolBoard b;
    sol_board_clear(&b);
    set_pile(&b, SOL_TAB0 + 0, "#2C 6H");
    set_pile(&b, SOL_TAB0 + 1, "#3C 5S 4D");
    set_pile(&b, SOL_TAB0 + 2, "");
    set_pile(&b, SOL_TAB0 + 3, "KD QC");
    set_pile(&b, SOL_TAB0 + 4, "#9C");
    set_pile(&b, SOL_TAB0 + 5, "6D");
    set_pile(&b, SOL_FOUND0 + 0, "AH");
    set_pile(&b, SOL_FOUND0 + 1, "");
    set_pile(&b, SOL_WASTE, "7C AS 5C");
    int t0 = SOL_TAB0, t1 = SOL_TAB0 + 1, t2 = SOL_TAB0 + 2, t3 = SOL_TAB0 + 3, t4 = SOL_TAB0 + 4,
        t5 = SOL_TAB0 + 5, f0 = SOL_FOUND0, f1 = SOL_FOUND0 + 1;
    CHECK(sol_can_drop(&b, t0, t1, 1));          /* 5S 4D onto 6H: the run's bottom card decides */
    CHECK(sol_can_drop(&b, t5, t1, 1));          /* 5S 4D onto 6D */
    CHECK(!sol_can_drop(&b, t0, t1, 2));         /* 4D onto 6H: two lower */
    CHECK(!sol_can_drop(&b, t2, t1, 1));         /* 5S to an empty column: kings only */
    CHECK(sol_can_drop(&b, t2, t3, 0));          /* KD QC to the empty column */
    CHECK(!sol_can_drop(&b, t0, t3, 0));         /* a king onto a card */
    CHECK(!sol_can_drop(&b, t4, t5, 0));         /* onto a face-down card */
    CHECK(sol_can_drop(&b, t0, SOL_WASTE, 2));   /* waste 5C onto 6H */
    CHECK(sol_can_drop(&b, t5, SOL_WASTE, 2));   /* waste 5C onto 6D */
    CHECK(!sol_can_drop(&b, f1, SOL_WASTE, 1));  /* AS under 5C: a foundation takes single cards only */
    CHECK(!sol_can_drop(&b, f1, t1, 1));         /* two cards onto a foundation */
    CHECK(!sol_can_drop(&b, f0, t1, 2));         /* 4D onto AH */
    CHECK(!sol_can_drop(&b, SOL_STOCK, t1, 2));  /* the stock and the waste never take cards */
    CHECK(!sol_can_drop(&b, SOL_WASTE, t1, 2));
    CHECK(!sol_can_drop(&b, t1, t1, 2));         /* onto itself */
    CHECK(!sol_can_drop(&b, t0, t1, 3));         /* no such card */
    CHECK(!sol_can_drop(&b, -1, t1, 2) && !sol_can_drop(&b, SOL_NPILES, t1, 2));
    /* foundations: any ace to any empty foundation, then the same suit upwards */
    set_pile(&b, SOL_WASTE, "7C AS");
    CHECK(sol_can_drop(&b, f1, SOL_WASTE, 1));
    CHECK(!sol_can_drop(&b, f0, SOL_WASTE, 1));
    set_pile(&b, SOL_WASTE, "2H");
    CHECK(sol_can_drop(&b, f0, SOL_WASTE, 0));
    set_pile(&b, SOL_WASTE, "2D");
    CHECK(!sol_can_drop(&b, f0, SOL_WASTE, 0));
    set_pile(&b, SOL_WASTE, "3H");
    CHECK(!sol_can_drop(&b, f0, SOL_WASTE, 0));
    /* foundation -> foundation (an ace to another empty foundation) and foundation -> tableau */
    CHECK(sol_can_drop(&b, f1, f0, 0));
    set_pile(&b, t2, "2S");
    CHECK(sol_can_drop(&b, t2, f0, 0));          /* AH onto 2S */
    /* colours: (s1 ^ s2) in {1, 2} */
    CHECK(sol_opposite(C("5S"), C("6H")) && sol_opposite(C("5C"), C("6D")));
    CHECK(!sol_opposite(C("5S"), C("6C")) && !sol_opposite(C("5D"), C("6H")));
}

/* The plain cases of the predicate, one per line (the block above also documents XP's quirks). */
static void test_can_drop_table(void)
{
    static const struct { const char *dst, *src; int ok; } v[] = {
        { "6H", "5S", 1 }, { "6H", "5C", 1 }, { "6D", "5S", 1 }, { "6D", "5C", 1 },
        { "6S", "5H", 1 }, { "6C", "5D", 1 }, { "6H", "5D", 0 }, { "6S", "5C", 0 },
        { "6H", "4S", 0 }, { "6H", "7S", 0 }, { "#6H", "5S", 0 }, { "", "KH", 1 },
        { "", "QH", 0 }, { "AH", "KS", 0 },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        SolBoard b;
        sol_board_clear(&b);
        set_pile(&b, SOL_TAB0, v[i].dst);
        set_pile(&b, SOL_TAB0 + 1, v[i].src);
        if (sol_can_drop(&b, SOL_TAB0, SOL_TAB0 + 1, 0) != v[i].ok) {
            fails++;
            printf("FAIL can_drop %s onto \"%s\" != %d\n", v[i].src, v[i].dst, v[i].ok);
        }
        checks++;
    }
}

/* ---- scoring (§4) ------------------------------------------------------------------------------------ */

static int ev_std(int score, int ev) { sol_change_score(&score, ev, SOL_SCORING_STANDARD, 1, 3, 0, 0); return score; }
static int ev_vegas(int score, int ev) { sol_change_score(&score, ev, SOL_SCORING_VEGAS, 1, 3, 0, 0); return score; }

static void test_scoring(void)
{
    /* the tables */
    static const int std[8] = { -2, -20, 10, 5, 5, -15, 0, 0 }, veg[8] = { 0, 0, 5, 0, 0, -5, -52, 0 };
    for (int i = 0; i < 8; i++) {
        CHECK_EQ(sol_std_table[i], std[i]);
        CHECK_EQ(sol_vegas_table[i], veg[i]);
    }
    /* ScoreMove: (destination, source) */
    CHECK_EQ(sol_move_event(SOL_STOCK, SOL_WASTE), SOL_EV_RECYCLE);
    CHECK_EQ(sol_move_event(SOL_FOUND0, SOL_WASTE), SOL_EV_TO_FOUND);
    CHECK_EQ(sol_move_event(SOL_FOUND0 + 3, SOL_TAB0 + 6), SOL_EV_TO_FOUND);
    CHECK_EQ(sol_move_event(SOL_TAB0, SOL_WASTE), SOL_EV_WASTE_TO_TAB);
    CHECK_EQ(sol_move_event(SOL_TAB0 + 2, SOL_FOUND0 + 1), SOL_EV_FOUND_TO_TAB);
    CHECK_EQ(sol_move_event(SOL_TAB0, SOL_TAB0 + 1), -1);
    CHECK_EQ(sol_move_event(SOL_FOUND0, SOL_FOUND0 + 1), -1);
    CHECK_EQ(sol_move_event(SOL_WASTE, SOL_STOCK), -1);
    /* Standard: values, the floor at 0 after every change */
    CHECK_EQ(ev_std(0, SOL_EV_TO_FOUND), 10);
    CHECK_EQ(ev_std(0, SOL_EV_WASTE_TO_TAB), 5);
    CHECK_EQ(ev_std(0, SOL_EV_TURN), 5);
    CHECK_EQ(ev_std(20, SOL_EV_FOUND_TO_TAB), 5);
    CHECK_EQ(ev_std(10, SOL_EV_FOUND_TO_TAB), 0);       /* 10 - 15 floored */
    CHECK_EQ(ev_std(1, SOL_EV_CLOCK), 0);
    CHECK_EQ(ev_std(115, SOL_EV_CLOCK), 113);
    CHECK_EQ(ev_std(0, SOL_EV_DEAL), 0);
    /* Vegas: values, negative allowed */
    CHECK_EQ(ev_vegas(0, SOL_EV_DEAL), -52);
    CHECK_EQ(ev_vegas(-52, SOL_EV_TO_FOUND), -47);
    CHECK_EQ(ev_vegas(-47, SOL_EV_FOUND_TO_TAB), -52);
    CHECK_EQ(ev_vegas(-52, SOL_EV_TURN), -52);
    CHECK_EQ(ev_vegas(-52, SOL_EV_WASTE_TO_TAB), -52);
    CHECK_EQ(ev_vegas(-52, SOL_EV_CLOCK), -52);
    CHECK_EQ(ev_vegas(-52, SOL_EV_RECYCLE), -52);
    /* None: nothing ever */
    for (int ev = 0; ev < 8; ev++) {
        int sc = 7;
        CHECK_EQ(sol_change_score(&sc, ev, SOL_SCORING_NONE, 1, 3, 1000, 9), 0);
        CHECK_EQ(sc, 7);
    }
    /* recycles: draw one -100 each; draw three 0 for the first three, -20 from the fourth */
    for (int r = 1; r <= 6; r++) {
        int sc = 1000;
        sol_change_score(&sc, SOL_EV_RECYCLE, SOL_SCORING_STANDARD, 1, 1, 0, r);
        CHECK_EQ(sc, 900);
        sc = 1000;
        sol_change_score(&sc, SOL_EV_RECYCLE, SOL_SCORING_STANDARD, 1, 3, 0, r);
        CHECK_EQ(sc, r <= 3 ? 1000 : 980);
    }
    int sc = 50;
    sol_change_score(&sc, SOL_EV_RECYCLE, SOL_SCORING_STANDARD, 1, 1, 0, 1);
    CHECK_EQ(sc, 0);                                     /* 50 - 100 floored */
    /* the time bonus: 35 * (20000 / s) from 30 s on; returned and added */
    static const struct { int secs, bonus; } tb[] = {
        { 0, 0 }, { 29, 0 }, { 30, 23310 }, { 48, 14560 }, { 100, 7000 }, { 600, 1155 }, { 8191, 70 } };
    for (size_t i = 0; i < sizeof tb / sizeof tb[0]; i++) {
        CHECK_EQ(sol_time_bonus(tb[i].secs * 4), tb[i].bonus);
        CHECK_EQ(sol_time_bonus(tb[i].secs * 4 + 3), tb[i].bonus);   /* whole seconds */
        sc = 100;
        CHECK_EQ(sol_change_score(&sc, SOL_EV_WIN, SOL_SCORING_STANDARD, 1, 3, tb[i].secs * 4, 0), tb[i].bonus);
        CHECK_EQ(sc, 100 + tb[i].bonus);
    }
    CHECK_EQ(sol_time_bonus(119), 0);
    CHECK_EQ(sol_time_bonus(120), 23310);
    sc = 100;                                            /* untimed: no bonus */
    CHECK_EQ(sol_change_score(&sc, SOL_EV_WIN, SOL_SCORING_STANDARD, 0, 3, 400, 0), 0);
    CHECK_EQ(sc, 100);
    sc = 208;                                            /* Vegas: no bonus */
    CHECK_EQ(sol_change_score(&sc, SOL_EV_WIN, SOL_SCORING_VEGAS, 1, 3, 400, 0), 0);
    CHECK_EQ(sc, 208);
}

/* ---- Options / Back / currency (§10, §4.5) ---------------------------------------------------------- */

static void check_opts(uint32_t v, int status, int timed, int outline, int draw, int scoring, int cumul)
{
    SolOptions o;
    sol_options_unpack(&o, v);
    CHECK_EQ(o.status_bar, status);
    CHECK_EQ(o.timed, timed);
    CHECK_EQ(o.outline, outline);
    CHECK_EQ(o.draw, draw);
    CHECK_EQ(o.scoring, scoring);
    CHECK_EQ(o.cumulative, cumul);
}

static void test_options(void)
{
    check_opts(SOL_OPTIONS_DEFAULT, 1, 1, 0, 3, SOL_SCORING_STANDARD, 0);
    check_opts(0x03, 1, 1, 0, 1, SOL_SCORING_STANDARD, 0);    /* after choosing Draw One */
    check_opts(0x13, 1, 1, 0, 1, SOL_SCORING_VEGAS, 0);
    check_opts(0x5B, 1, 1, 0, 3, SOL_SCORING_VEGAS, 1);
    check_opts(0x2D, 1, 0, 1, 3, SOL_SCORING_NONE, 0);        /* written by hand in the research */
    check_opts(0x5F, 1, 1, 1, 3, SOL_SCORING_VEGAS, 1);
    check_opts(0x30, 0, 0, 0, 1, SOL_SCORING_STANDARD, 0);    /* scoring 3 reads as Standard */
    check_opts(0xFFFFFF80u, 0, 0, 0, 1, SOL_SCORING_STANDARD, 0);
    /* pack: exactly the bits XP writes, round trip for all 7-bit values but scoring 3 */
    for (uint32_t v = 0; v < 128; v++) {
        SolOptions o;
        sol_options_unpack(&o, v);
        uint32_t want = ((v & 0x30) == 0x30) ? (v & ~0x30u) : v;
        CHECK_EQ(sol_options_pack(&o), want);
    }
    /* Back: stored as id - 53, loaded as clamp(v + 53, 54, 65) */
    CHECK_EQ(sol_back_from_reg(1), 0);
    CHECK_EQ(sol_back_from_reg(7), 6);                         /* id 60 */
    CHECK_EQ(sol_back_from_reg(10), 9);                        /* id 63 */
    CHECK_EQ(sol_back_from_reg(12), 11);
    CHECK_EQ(sol_back_from_reg(99), 11);                       /* id 65 */
    CHECK_EQ(sol_back_from_reg(0), 0);                         /* 53 -> 54 */
    CHECK_EQ(sol_back_from_reg(0xFFFFFFFFu), 0);               /* -1 */
    CHECK_EQ(sol_back_from_reg(0x80000000u), 0);
    CHECK_EQ(sol_back_from_reg(0x7FFFFFFFu), 11);
    for (int b = 0; b < SOL_NBACKS; b++) CHECK_EQ(sol_back_from_reg(sol_back_to_reg(b)), b);
    CHECK_EQ(sol_back_to_reg(9), 10);                          /* id 63 stored as 10 */
    /* score text */
    char buf[32];
    sol_format_score(-52, 1, 0, buf, sizeof buf); CHECK_STR(buf, "-$52");
    sol_format_score(-52, 1, 1, buf, sizeof buf); CHECK_STR(buf, "-52$");
    sol_format_score(-52, 1, 2, buf, sizeof buf); CHECK_STR(buf, "-$ 52");
    sol_format_score(-52, 1, 3, buf, sizeof buf); CHECK_STR(buf, "-52 $");
    sol_format_score(208, 1, 0, buf, sizeof buf); CHECK_STR(buf, "$208");
    sol_format_score(0, 1, 7, buf, sizeof buf);   CHECK_STR(buf, "$0");
    sol_format_score(0, 0, 0, buf, sizeof buf);   CHECK_STR(buf, "0");
    sol_format_score(1155, 0, 2, buf, sizeof buf); CHECK_STR(buf, "1155");
}

/* ---- packing and invariants --------------------------------------------------------------------------- */

static void test_pack_valid(void)
{
    char why[128];
    for (unsigned seed = 0; seed < 200; seed++) {
        SolBoard b, u;
        uint8_t pk[SOL_PACKED_SIZE];
        sol_deal_board(&b, seed * 163, NULL);
        /* move some cards around legally-ish to vary the shape: waste gets 3 face-up cards */
        for (int i = 0; i < 3; i++) b.p[SOL_WASTE].c[b.p[SOL_WASTE].n++] = (SolCard)(b.p[SOL_STOCK].c[--b.p[SOL_STOCK].n] | SOL_UP);
        sol_board_pack(&b, pk);
        sol_board_unpack(&u, pk);
        CHECK(sol_board_equal(&b, &u));
        CHECK(sol_board_valid(&u, why, sizeof why));
    }
    SolBoard b;
    sol_board_clear(&b);
    CHECK(!sol_board_valid(&b, why, sizeof why));               /* 0 cards */
    sol_deal_board(&b, 5, NULL);
    b.p[SOL_TAB0 + 3].c[3] = C("#KS");                           /* a card twice */
    CHECK(!sol_board_valid(&b, why, sizeof why));
    sol_deal_board(&b, 5, NULL);
    b.p[SOL_STOCK].c[0] |= SOL_UP;
    CHECK(!sol_board_valid(&b, why, sizeof why));
    sol_board_clear(&b);
    set_pile(&b, SOL_FOUND0, "AC 2C 4C");
    fill_stock(&b);
    CHECK(!sol_board_valid(&b, why, sizeof why));
    sol_board_clear(&b);
    set_pile(&b, SOL_TAB0, "#2C 9H 8S 7S");
    fill_stock(&b);
    CHECK(!sol_board_valid(&b, why, sizeof why));
    sol_board_clear(&b);
    set_pile(&b, SOL_TAB0, "#2C 9H 8S 7D");
    set_pile(&b, SOL_FOUND0, "AC");
    set_pile(&b, SOL_WASTE, "5D");
    set_pile(&b, SOL_TAB0 + 1, "#KD");
    fill_stock(&b);
    CHECK(sol_board_valid(&b, why, sizeof why));
    CHECK_EQ(sol_up_run(&b, SOL_TAB0), 3);
    CHECK_EQ(sol_up_run(&b, SOL_TAB0 + 1), 0);
    CHECK_EQ(sol_up_run(&b, SOL_TAB0 + 2), 0);
    CHECK_EQ(sol_pile_class(SOL_STOCK), SOL_CLASS_STOCK);
    CHECK_EQ(sol_pile_class(SOL_WASTE), SOL_CLASS_WASTE);
    CHECK_EQ(sol_pile_class(5), SOL_CLASS_FOUND);
    CHECK_EQ(sol_pile_class(6), SOL_CLASS_TAB);
    CHECK_EQ(sol_pile_class(12), SOL_CLASS_TAB);
}

int main(void)
{
    test_deals();
    test_can_drop();
    test_can_drop_table();
    test_scoring();
    test_options();
    test_pack_valid();
    return test_summary("test_sol_game");
}
