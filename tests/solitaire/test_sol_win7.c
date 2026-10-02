/*
 * Solitaire HD — native tests of the Windows 7-inspired batch (ROADMAP 2c; session.c, assist.c,
 * stats.c, savegame.c): hint cycling (the ranked list, the session's cycle and what ends it, fairness),
 * the foundation-down hint, dead ends (a whole stock cycle, the used-up stock, what starts the tracking
 * over, Hint's "There are no more useful moves.", the No More Moves question: Return to Game and End
 * Game), the high scores and the Vegas money (the tables, the dialog's text, the file's version 2 and
 * the migration of version 1), the prompts with "Save game on exit" (New Game: quit / restart / keep
 * playing; Exit; the saved game at start-up) and "Apply option changes to the next game" (the question,
 * the pending Options, the next deal, a saved game under other Options).
 */
#include "sol_test.h"
#include "solitaire/assist.h"
#include "solitaire/savegame.h"
#include "solitaire/stats.h"
#include "solitaire/winnable_seeds.h"

#define T(k) (SOL_TAB0 + (k))
#define F(k) (SOL_FOUND0 + (k))

/* A board with the given piles holding all 52 cards (the stock among them, "#.." face down). */
static void board_exact(SolSession *s, ...)
{
    va_list ap;
    sol_board_clear(&s->board);
    va_start(ap, s);
    for (;;) {
        int p = va_arg(ap, int);
        if (p < 0) break;
        set_pile(&s->board, p, va_arg(ap, const char *));
    }
    va_end(ap);
    char why[128] = "";
    if (!sol_board_valid(&s->board, why, sizeof why)) printf("  exact board invalid: %s\n", why);
    s->waste_fan = 0;
    s->nhist = s->nredo = 0;
    s->recycles = 0;
    s->nm_mark = -1;                                      /* the dead-end tracking starts here */
    s->nm_recycled = s->nm_done = s->nm_shown = 0;
    s->hint_cyc = 0;
}

/* The missing cards face down at the bottom of the stock (as test_sol_extras.c's board()). */
static void board(SolSession *s, ...)
{
    va_list ap;
    sol_board_clear(&s->board);
    va_start(ap, s);
    for (;;) {
        int p = va_arg(ap, int);
        if (p < 0) break;
        set_pile(&s->board, p, va_arg(ap, const char *));
    }
    va_end(ap);
    fill_stock(&s->board);
    char why[128] = "";
    if (!sol_board_valid(&s->board, why, sizeof why)) printf("  scenario board invalid: %s\n", why);
    s->waste_fan = 0;
    s->nhist = s->nredo = 0;
    s->drag_pile = -1;
    s->target = -1;
    s->hint_cyc = 0;
}

static SolExtras extras_of(const SolSession *s) { return s->extras; }

/* ---- memory file ------------------------------------------------------------------------------------ */

typedef struct Mem { uint8_t *d; size_t n; int exists, writes; } Mem;
static long mem_read(void *ctx, void *buf, size_t cap)
{
    Mem *m = ctx;
    if (!m->exists) return -1;
    if (m->n > cap) return (long)cap + 1;
    memcpy(buf, m->d, m->n);
    return (long)m->n;
}
static int mem_write(void *ctx, const void *data, size_t len)
{
    Mem *m = ctx;
    free(m->d);
    m->d = malloc(len ? len : 1);
    memcpy(m->d, data, len);
    m->n = len;
    m->exists = 1;
    m->writes++;
    return 1;
}
static CeBlobIO mem_io(Mem *m) { CeBlobIO io = { m, mem_read, mem_write }; return io; }
static void mem_free(Mem *m) { free(m->d); memset(m, 0, sizeof *m); }

static void shuffle_hidden(SolBoard *b, unsigned seed)
{
    SolCard *ref[52], val[52];
    int n = 0;
    tsrand(seed);
    for (int p = 0; p < SOL_NPILES; p++)
        if (p == SOL_STOCK || sol_is_tab(p))
            for (int i = 0; i < b->p[p].n; i++)
                if (!sol_is_up(b->p[p].c[i])) { ref[n] = &b->p[p].c[i]; val[n] = b->p[p].c[i]; n++; }
    for (int i = n - 1; i > 0; i--) {
        int j = trand() % (i + 1);
        SolCard t = val[i];
        val[i] = val[j];
        val[j] = t;
    }
    for (int i = 0; i < n; i++) *ref[i] = val[i];
}

static int same_move(const SolHintMove *a, const SolHintMove *b)
{
    return a->kind == b->kind && a->src == b->src && a->index == b->index && a->dst == b->dst && a->cls == b->cls;
}

/* ---- hint cycling ------------------------------------------------------------------------------------ */

static void test_hint_cycle(void)
{
    SolSession s;
    Fake f;
    Reg r;
    SolHintMove l[SOL_HINT_MAX], one;
    int n;
    start(&s, &f, &r, 0x09, 1);
    board(&s, T(0), "#2C 5H", T(1), "6S", T(2), "6C", T(3), "#3D", -1);
    n = sol_hint_list(&s.board, 1, l, SOL_HINT_MAX);
    CHECK_EQ(n, 3);
    CHECK(l[0].kind == SOL_HINT_TURN && l[0].src == T(3) && l[0].cls == SOL_HC_TURN);
    CHECK(l[1].kind == SOL_HINT_MOVE && l[1].src == T(0) && l[1].index == 1 && l[1].dst == T(1) &&
          l[1].cls == SOL_HC_REVEAL);                     /* one destination per card: the leftmost */
    CHECK(l[2].kind == SOL_HINT_DRAW && l[2].cls == SOL_HC_DRAW);
    CHECK(sol_hint_find(&s.board, 1, &one) && same_move(&one, &l[0]));
    CHECK_EQ(sol_hint_list(&s.board, 1, l, 2), 2);        /* at most max */

    /* Hint again: the next one, wrapping */
    for (int k = 0; k < 7; k++) {
        sol_command(&s, SOL_CMD_HINT);
        CHECK(s.hint && same_move(&s.hint_move, &l[k % 3]));
    }
    /* (7 hints: the next is l[1]) the flash's own timer steps and H's key-down (a key the game does not use)
     * keep the cycle */
    sol_timer(&s, SOL_TIMER_HINT);
    sol_timer(&s, SOL_TIMER_HINT);
    CHECK_EQ(sol_key(&s, 'H', 0), 0);
    sol_command(&s, SOL_CMD_HINT);
    CHECK(same_move(&s.hint_move, &l[1]));
    /* a key the game uses starts over */
    sol_key(&s, SOL_KEY_LEFT, 0);
    sol_command(&s, SOL_CMD_HINT);
    CHECK(same_move(&s.hint_move, &l[0]));
    /* so does a press, even one that does nothing */
    sol_command(&s, SOL_CMD_HINT);
    CHECK(same_move(&s.hint_move, &l[1]));
    sol_press(&s, SOL_MISS, -1, 0);
    sol_command(&s, SOL_CMD_HINT);
    CHECK(same_move(&s.hint_move, &l[0]));
    /* ... and any other command */
    sol_command(&s, SOL_CMD_HINT);
    sol_command(&s, SOL_CMD_REDO);                       /* (nothing to redo) */
    sol_command(&s, SOL_CMD_HINT);
    CHECK(same_move(&s.hint_move, &l[0]));
    /* a move: a new position, its own best move first */
    CHECK(sol_begin_drag(&s, T(0), 1) && sol_drop(&s, T(1)));
    sol_command(&s, SOL_CMD_HINT);
    CHECK(s.hint_move.kind == SOL_HINT_TURN && s.hint_move.src == T(0));   /* #2C is now on top */
    /* a lone draw: the same move again and again */
    board(&s, T(0), "5H", -1);
    for (int k = 0; k < 3; k++) {
        sol_command(&s, SOL_CMD_HINT);
        CHECK(s.hint && s.hint_move.kind == SOL_HINT_DRAW);
    }
    sol_free(&s);
}

/* The list in random play: its first is sol_hint_find's, classes in order, no move twice, every move
 * legal, the same list when the hidden cards are shuffled (fair). */
static void test_hint_list_random(void)
{
    int positions = 0, bad_first = 0, bad_order = 0, dup = 0, illegal = 0, unfair = 0;
    for (unsigned g = 0; g < 40; g++) {
        SolSession s;
        Fake f;
        Reg r;
        start(&s, &f, &r, g & 1 ? 0x01 : 0x09, (int)(g * 1231u % 32768u));
        tsrand(g * 7u + 3u);
        for (int k = 0; k < 300 && s.dealt; k++) {
            SolHintMove l[SOL_HINT_MAX], l2[SOL_HINT_MAX], one;
            int rec = s.board.p[SOL_STOCK].n == 0 && s.board.p[SOL_WASTE].n > 0;
            int n = sol_hint_list(&s.board, rec, l, SOL_HINT_MAX), n2;
            SolBoard t = s.board;
            positions++;
            if (sol_hint_find(&s.board, rec, &one) != (n > 0) || (n > 0 && !same_move(&one, &l[0]))) bad_first++;
            for (int i = 0; i < n; i++) {
                if (i && l[i].cls < l[i - 1].cls) bad_order++;
                for (int j = 0; j < i; j++)
                    if (l[j].kind == l[i].kind && l[j].src == l[i].src && l[j].index == l[i].index &&
                        l[j].dst == l[i].dst) dup++;
                if (l[i].kind == SOL_HINT_MOVE && !sol_can_drop(&s.board, l[i].dst, l[i].src, l[i].index)) illegal++;
            }
            shuffle_hidden(&t, g * 131u + (unsigned)k);
            n2 = sol_hint_list(&t, rec, l2, SOL_HINT_MAX);
            if (n2 != n || memcmp(l, l2, sizeof l[0] * (size_t)n)) unfair++;
            /* play one of the listed moves at random (so the later ones are tried too) */
            if (n && trand() % 5) {
                SolHintMove m = l[trand() % n];
                if (m.kind == SOL_HINT_MOVE) { if (sol_begin_drag(&s, m.src, m.index)) sol_drop(&s, m.dst); }
                else if (m.kind == SOL_HINT_TURN) sol_press(&s, m.src, m.index, 0);
                else sol_press(&s, SOL_STOCK, 0, 0);
            } else {
                sol_press(&s, SOL_STOCK, 0, 0);
            }
        }
        sol_free(&s);
    }
    CHECK(positions > 5000);
    CHECK_EQ(bad_first, 0);
    CHECK_EQ(bad_order, 0);
    CHECK_EQ(dup, 0);
    CHECK_EQ(illegal, 0);
    CHECK_EQ(unfair, 0);
    printf("  hint lists: %d positions\n", positions);
}

/* Class 9: a foundation's card taken down so that the waste's card (or a run on face-down cards) can go
 * on it; never an ace or a two. */
static void test_hint_down(void)
{
    SolSession s;
    Fake f;
    Reg r;
    SolHintMove m;
    start(&s, &f, &r, 0x01, 1);
    /* the waste's 4C needs a red 5: 5H on its foundation, 6S on the tableau takes it */
    board(&s, SOL_WASTE, "4C", T(0), "#KC 6S", F(2), "AH 2H 3H 4H 5H", -1);
    CHECK(sol_hint_find(&s.board, 0, &m));
    CHECK(m.kind == SOL_HINT_MOVE && m.src == F(2) && m.index == 4 && m.dst == T(0) && m.cls == SOL_HC_DOWN);
    CHECK(sol_useful_move(&s.board));
    /* a run on face-down cards that needs it */
    board(&s, T(1), "#KD 4S", T(0), "#KC 6C", F(2), "AH 2H 3H 4H 5H", -1);
    CHECK(sol_hint_find(&s.board, 0, &m));
    CHECK(m.src == F(2) && m.dst == T(0) && m.cls == SOL_HC_DOWN);
    /* nothing would go on it: never */
    board(&s, T(0), "#KC 6S", F(2), "AH 2H 3H 4H 5H", -1);
    CHECK(sol_hint_find(&s.board, 0, &m) && m.kind == SOL_HINT_DRAW);
    /* a two: never (the ace would go home instead) */
    board(&s, SOL_WASTE, "AS", T(0), "#KC 3S", F(2), "AH 2H", -1);
    CHECK(sol_hint_find(&s.board, 0, &m) && m.src == SOL_WASTE && m.cls == SOL_HC_LOW_HOME);
    sol_free(&s);
}

/* ---- dead ends ---------------------------------------------------------------------------------------- */

/* No useful move anywhere: every tableau top red (none takes another), the threes buried, the aces and
 * twos home, the stock three kings (no empty column for them). */
static void dead_board(SolSession *s, const char *stock)
{
    board_exact(s, F(0), "AC 2C", F(1), "AD 2D", F(2), "AH 2H", F(3), "AS 2S", SOL_STOCK, stock,
                T(0), "#3C #4C #5C #6C #7C 9H", T(1), "#8C #9C #TC #JC #QC 9D", T(2), "#3S #4S #5S #6S #7S 7H",
                T(3), "#8S #9S #TS #JS #QS 7D", T(4), "#KS #3D #4D #6D #8D 5H", T(5), "#TD #JD #QD #3H #4H 5D",
                T(6), "#6H #8H #TH #QH JH", -1);
}

static void test_dead_cycle(void)
{
    SolSession s;
    Fake f;
    Reg r;
    /* Draw One, Standard (unlimited passes): the first position without a useful move is after the first
     * draw (stock 2); the cycle is whole when the stock is back at 2 after a recycle */
    start(&s, &f, &r, 0x01, 1);
    sol_attach_stats(&s, NULL);
    dead_board(&s, "#KC #KD #KH");
    CHECK(!sol_useful_move(&s.board));
    int want[5] = { 0, 0, 0, 0, 1 };
    for (int k = 0; k < 5; k++) {
        sol_command(&s, SOL_CMD_HINT);                  /* draw, draw, draw, recycle, draw */
        CHECK(s.hint && s.hint_move.cls >= SOL_HC_DRAW);
        sol_command(&s, SOL_CMD_DRAW);
        CHECK_EQ(sol_no_more_moves(&s), want[k]);
    }
    CHECK_EQ(s.board.p[SOL_STOCK].n, 2);
    CHECK_EQ(f.nchoose, 0);                             /* the question is an option, off */
    /* Hint: no more draws */
    int nmsg = f.nmsg;
    sol_command(&s, SOL_CMD_HINT);
    CHECK(!s.hint);
    CHECK_EQ(f.nmsg, nmsg + 1);
    CHECK_EQ(f.last_msg, SOL_MSG_NO_USEFUL);
    CHECK_STR(f.last_msg_text, "There are no more useful moves.");
    /* Undo starts the tracking over: draws are suggested again until a whole cycle has gone by */
    sol_undo(&s);
    CHECK(!sol_no_more_moves(&s));
    sol_command(&s, SOL_CMD_HINT);
    CHECK(s.hint && s.hint_move.kind == SOL_HINT_DRAW);
    sol_free(&s);

    /* a useful card in the stock: never a dead end (8C onto a red 9 each time it comes up) */
    start(&s, &f, &r, 0x01, 1);
    board_exact(&s, F(0), "AC 2C", F(1), "AD 2D", F(2), "AH 2H", F(3), "AS 2S", SOL_STOCK, "#8C #KD #KH",
                T(0), "#3C #4C #5C #6C #7C 9H", T(1), "#KC #9C #TC #JC #QC 9D", T(2), "#3S #4S #5S #6S #7S 7H",
                T(3), "#8S #9S #TS #JS #QS 7D", T(4), "#KS #3D #4D #6D #8D 5H", T(5), "#TD #JD #QD #3H #4H 5D",
                T(6), "#6H #8H #TH #QH JH", -1);
    for (int k = 0; k < 16; k++) {
        sol_command(&s, SOL_CMD_DRAW);
        CHECK(!sol_no_more_moves(&s));
    }
    sol_free(&s);

    /* Draw Three: the cycle starts with one card on the waste (stock 6 -> 3: the mark), so after the
     * recycle the draws go 7, 4, 1, 0: the cycle is whole at the empty stock */
    start(&s, &f, &r, 0x09, 1);
    board_exact(&s, F(0), "AC 2C", F(1), "AD 2D", F(2), "AH 2H", F(3), "AS 2S", SOL_WASTE, "QS",
                SOL_STOCK, "#KC #KD #KH #JS #QC #QD",
                T(0), "#3C #4C #5C #6C #7C 9H", T(1), "#8C #9C #TC #JC 9D", T(2), "#3S #4S #5S #6S #7S 7H",
                T(3), "#8S #9S #TS 7D", T(4), "#KS #3D #4D #6D #8D 5H", T(5), "#TD #JD #3H #4H 5D",
                T(6), "#6H #8H #TH #QH JH", -1);
    {
        char why[128] = "";
        CHECK(sol_board_valid(&s.board, why, sizeof why));
        if (why[0]) printf("  %s\n", why);
    }
    int stock_after[6] = { 3, 0, 7, 4, 1, 0 }, done[6] = { 0, 0, 0, 0, 0, 1 };
    CHECK(!sol_useful_move(&s.board));
    for (int k = 0; k < 6; k++) {
        sol_command(&s, SOL_CMD_DRAW);
        CHECK_EQ(s.board.p[SOL_STOCK].n, stock_after[k]);
        CHECK_EQ(sol_no_more_moves(&s), done[k]);
    }
    sol_free(&s);

    /* Vegas, Draw One: one pass; the stock used up with nothing useful is a dead end at once */
    start(&s, &f, &r, 0x11, 1);
    dead_board(&s, "#KC #KD #KH");
    sol_command(&s, SOL_CMD_DRAW);
    sol_command(&s, SOL_CMD_DRAW);
    CHECK(!sol_no_more_moves(&s));
    sol_command(&s, SOL_CMD_DRAW);
    CHECK_EQ(sol_stock_symbol(&s), SOL_STOCK_X);
    CHECK(sol_no_more_moves(&s));
    nmsg = f.nmsg;
    sol_command(&s, SOL_CMD_HINT);                      /* nothing at all: XP's "No hint is available." */
    CHECK_EQ(f.nmsg, nmsg + 1);
    CHECK_EQ(f.last_msg, SOL_MSG_NO_HINT);
    sol_free(&s);
}

static void test_no_more_moves_question(void)
{
    SolSession s;
    Fake f;
    Reg r;
    SolExtras x;
    int m1 = sol_stats_mode(1, SOL_SCORING_STANDARD);
    /* Return to Game: asked once; the game goes on; Undo re-arms it */
    start(&s, &f, &r, 0x01, 1);
    sol_attach_stats(&s, NULL);
    x = s.extras;
    x.no_more_moves = 1;
    sol_set_extras(&s, &x);
    dead_board(&s, "#KC #KD #KH");
    f.answer = SOL_ANS_RETURN;
    for (int k = 0; k < 4; k++) sol_command(&s, SOL_CMD_DRAW);
    CHECK_EQ(f.nchoose, 0);
    sol_command(&s, SOL_CMD_DRAW);
    CHECK_EQ(f.nchoose, 1);
    CHECK_EQ(f.last_choose, SOL_ASK_NO_MOVES);
    CHECK(s.dealt && s.nhist == 5);
    for (int k = 0; k < 6; k++) sol_command(&s, SOL_CMD_DRAW);
    CHECK_EQ(f.nchoose, 1);                             /* not again for this dead end */
    CHECK(sol_undo(&s));
    for (int k = 0; k < 5; k++) sol_command(&s, SOL_CMD_DRAW);
    CHECK_EQ(f.nchoose, 2);                             /* a new cycle after the Undo */
    CHECK_EQ(s.stats.m[m1].played, 1);
    CHECK_EQ(s.stats.m[m1].streak, 0);
    /* End Game: a loss, the game ends (the table frozen, nothing to undo), "Deal Again?" */
    f.answer = SOL_ANS_END_GAME;
    f.deal_again = 0;
    CHECK(sol_undo(&s));
    for (int k = 0; k < 5 && s.dealt; k++) sol_command(&s, SOL_CMD_DRAW);
    CHECK_EQ(f.nchoose, 3);
    CHECK(!s.dealt && s.visible && !s.won);
    CHECK_EQ(f.ndealagain, 1);
    CHECK_EQ(s.stats.m[m1].played, 1);
    CHECK_EQ(s.stats.m[m1].streak, -1);
    CHECK_EQ(s.stats.m[m1].loss_streak, 1);
    CHECK(!sol_undo_enabled(&s) && !sol_hint_enabled(&s) && !s.timer_on);
    CHECK_EQ(sol_press(&s, T(0), 5, 0), SOL_PRESS_NONE);   /* board input ignored */
    sol_command(&s, SOL_CMD_DEAL);
    CHECK(s.dealt);
    CHECK_EQ(f.nchoose, 3);                             /* a deal after the end asks nothing */
    /* "Deal Again?" Yes: dealt right away (no post_command) */
    dead_board(&s, "#KC #KD #KH");
    f.deal_again = 1;
    unsigned seed = s.seed;
    for (int k = 0; k < 5 && s.board.p[SOL_STOCK].n + s.board.p[SOL_WASTE].n == 3; k++) sol_command(&s, SOL_CMD_DRAW);
    CHECK(s.dealt && s.seed != seed && s.nhist == 0);
    CHECK_EQ(s.stats.m[m1].streak, -2);
    /* the question never comes with the option off (the default answer would return to the game) */
    x.no_more_moves = 0;
    sol_set_extras(&s, &x);
    dead_board(&s, "#KC #KD #KH");
    int nc = f.nchoose;
    for (int k = 0; k < 8; k++) sol_command(&s, SOL_CMD_DRAW);
    CHECK_EQ(f.nchoose, nc);
    CHECK(s.dealt && sol_no_more_moves(&s));
    /* without a UI for it: Return to Game */
    x.no_more_moves = 1;
    sol_set_extras(&s, &x);
    s.ui.choose = NULL;
    dead_board(&s, "#KC #KD #KH");
    for (int k = 0; k < 8; k++) sol_command(&s, SOL_CMD_DRAW);
    CHECK(s.dealt && s.nm_shown);
    sol_free(&s);
}

/* ---- high scores and money ----------------------------------------------------------------------------- */

static void test_high_scores(void)
{
    SolStats st;
    char lab[512], val[512], d[24];
    uint32_t dates[SOL_STATS_TOP_LINES];
    int ms = sol_stats_mode(3, SOL_SCORING_STANDARD), mv = sol_stats_mode(1, SOL_SCORING_VEGAS),
        mn = sol_stats_mode(3, SOL_SCORING_NONE);
    sol_stats_clear(&st);
    for (int i = 0; i < SOL_STATS_MODES; i++) sol_stats_played(&st, i), sol_stats_played(&st, i);
    /* Standard: two tables, best first, an equal score below the older one, five kept */
    sol_stats_lost(&st, ms, 300, 1, 1, 20261001);
    sol_stats_won(&st, ms, 100, 900, 1, 1, 20261002);
    sol_stats_lost(&st, ms, 300, 1, 1, 20261003);
    sol_stats_lost(&st, ms, 50, 1, 0, 20261004);       /* not timed: the other table */
    const SolModeStats *m = &st.m[ms];
    CHECK_EQ(m->ntop[SOL_TOP_TIMED], 3);
    CHECK_EQ(m->ntop[SOL_TOP_UNTIMED], 1);
    CHECK(m->top[1][0].score == 900 && m->top[1][0].date == 20261002);
    CHECK(m->top[1][1].score == 300 && m->top[1][1].date == 20261001);
    CHECK(m->top[1][2].score == 300 && m->top[1][2].date == 20261003);
    CHECK(m->top[0][0].score == 50 && m->top[0][0].date == 20261004);
    CHECK_EQ(m->best_score, 900);
    sol_stats_lost(&st, ms, 10, 1, 1, 20261005);
    sol_stats_lost(&st, ms, 400, 1, 1, 20261006);
    sol_stats_lost(&st, ms, 5, 1, 1, 20261007);        /* the sixth, the lowest: left out */
    CHECK_EQ(m->ntop[1], 5);
    CHECK(m->top[1][0].score == 900 && m->top[1][1].score == 400 && m->top[1][4].score == 10);
    sol_stats_lost(&st, ms, 350, 1, 1, 99999999);      /* not a date: kept as unknown */
    CHECK(m->top[1][2].score == 350 && m->top[1][2].date == 0);
    CHECK(m->top[1][3].score == 300 && m->top[1][3].date == 20261001);   /* the older 300 above */
    CHECK(m->top[1][4].score == 300 && m->top[1][4].date == 20261003);   /* the 10 dropped out */
    CHECK_EQ(sol_stats_format_top(m, SOL_SCORING_STANDARD, 0, lab, sizeof lab, val, sizeof val, dates), 12);
    CHECK_STR(lab, "Timed games:\n1.\n2.\n3.\n4.\n5.\nNot timed:\n1.\n2.\n3.\n4.\n5.");
    CHECK_STR(val, "\n900\n400\n350\n300\n300\n\n50\n-\n-\n-\n-");
    CHECK(dates[0] == 0 && dates[1] == 20261002 && dates[2] == 20261006 && dates[3] == 0 && dates[7] == 20261004 &&
          dates[8] == 0);
    sol_stats_date_text(20261002, d, sizeof d);
    CHECK_STR(d, "2026-10-02");
    sol_stats_date_text(0, d, sizeof d);
    CHECK_STR(d, "");
    /* Vegas: one table (Timed or not), the money */
    m = &st.m[mv];
    CHECK_EQ(sol_stats_format_top(m, SOL_SCORING_VEGAS, 0, lab, sizeof lab, val, sizeof val, dates), 9);
    CHECK_STR(lab, "High scores:\n1.\n2.\n3.\n4.\n5.\nMost money won:\nMost money lost:\nCurrent winnings:");
    CHECK_STR(val, "\n-\n-\n-\n-\n-\n-\n-\n-");
    sol_stats_lost(&st, mv, -27, 1, 1, 20261001);
    sol_stats_won(&st, mv, 0, 208, 1, 0, 20261002);
    sol_stats_lost(&st, mv, -52, 1, 0, 20261003);
    CHECK_EQ(m->ntop[0], 3);
    CHECK_EQ(m->ntop[1], 0);
    CHECK(m->has_money && m->most_won == 208 && m->most_lost == -52 && m->winnings == 208 - 27 - 52);
    sol_stats_format_top(m, SOL_SCORING_VEGAS, 0, lab, sizeof lab, val, sizeof val, dates);
    CHECK_STR(val, "\n$208\n-$27\n-$52\n-\n-\n$208\n$52\n$129");
    CHECK(dates[1] == 20261002 && dates[3] == 20261003 && dates[6] == 0);
    sol_stats_lost(&st, mv, -52, 1, 0, 20261004);
    sol_stats_lost(&st, mv, -52, 1, 0, 20261005);
    sol_stats_lost(&st, mv, -52, 1, 0, 20261006);
    sol_stats_format_top(m, SOL_SCORING_VEGAS, 3, lab, sizeof lab, val, sizeof val, dates);   /* iCurrency 3 */
    CHECK_STR(val, "\n208 $\n-27 $\n-52 $\n-52 $\n-52 $\n208 $\n52 $\n-27 $");
    /* only losses: nothing won */
    SolStats v;
    sol_stats_clear(&v);
    sol_stats_played(&v, mv);
    sol_stats_lost(&v, mv, -47, 1, 0, 0);
    sol_stats_format_top(&v.m[mv], SOL_SCORING_VEGAS, 0, lab, sizeof lab, val, sizeof val, dates);
    CHECK_STR(val, "\n-$47\n-\n-\n-\n-\n$0\n$47\n-$47");
    /* None: nothing */
    sol_stats_lost(&st, mn, 0, 0, 1, 20261001);
    CHECK(st.m[mn].ntop[0] == 0 && st.m[mn].ntop[1] == 0 && !st.m[mn].has_money);
    CHECK_EQ(sol_stats_format_top(&st.m[mn], SOL_SCORING_NONE, 0, lab, sizeof lab, val, sizeof val, dates), 1);
    CHECK_STR(lab, "No scores with None scoring.");
    CHECK_EQ(sol_stats_mode_scoring(mv), SOL_SCORING_VEGAS);
    CHECK_EQ(sol_stats_mode_scoring(mn), SOL_SCORING_NONE);
    CHECK_EQ(sol_stats_mode_scoring(ms), SOL_SCORING_STANDARD);

    /* through the session: the game's own Timed setting and today's date */
    SolSession s;
    Fake f;
    Reg r;
    start(&s, &f, &r, 0x0B, 1);                         /* Draw Three, Standard, timed */
    sol_attach_stats(&s, NULL);
    f.today = 20261002;
    sol_press(&s, SOL_STOCK, 0, 0);
    s.score = 123;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK(s.stats.m[ms].ntop[1] == 1 && s.stats.m[ms].top[1][0].score == 123 &&
          s.stats.m[ms].top[1][0].date == 20261002);
    SolOptions o = s.opts;
    o.timed = 0;
    sol_apply_options(&s, &o);                           /* (a redeal: nothing was played) */
    sol_press(&s, SOL_STOCK, 0, 0);
    s.score = 77;
    f.today = 20261003;
    sol_abandon(&s);
    CHECK(s.stats.m[ms].ntop[0] == 1 && s.stats.m[ms].top[0][0].score == 77 &&
          s.stats.m[ms].top[0][0].date == 20261003);
    CHECK_EQ(s.stats.m[ms].ntop[1], 1);
    sol_free(&s);
}

static void put32le(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void test_stats_file(void)
{
    SolStats st, back;
    Mem mem;
    uint8_t img[SOL_STATS_FILE_SIZE];
    int mv = sol_stats_mode(1, SOL_SCORING_VEGAS), ms = sol_stats_mode(3, SOL_SCORING_STANDARD);
    memset(&mem, 0, sizeof mem);
    CeBlobIO io = mem_io(&mem);
    sol_stats_clear(&st);
    for (int k = 0; k < 9; k++) {
        sol_stats_played(&st, ms);
        sol_stats_lost(&st, ms, k * 37 % 200, 1, k & 1, 20260900u + (unsigned)k + 1);
        sol_stats_played(&st, mv);
        if (k % 3) sol_stats_lost(&st, mv, -52 + k * 5, 1, 0, 20261001);
        else sol_stats_won(&st, mv, 0, 208, 1, 0, 20261001);
    }
    CHECK(sol_stats_save(&st, &io));
    CHECK_EQ(mem.n, SOL_STATS_FILE_SIZE);
    CHECK_EQ(mem.n, 832);
    CHECK_EQ(sol_stats_load(&back, &io), 1);
    CHECK(!memcmp(&back, &st, sizeof st));
    CHECK_EQ(sol_stats_deserialize(&back, mem.d, mem.n), 2);
    /* damage: every 5th byte flipped, a short file */
    for (size_t k = 0; k < mem.n; k += 5) {
        mem.d[k] ^= 0x20;
        CHECK_EQ(sol_stats_load(&back, &io), -1);
        mem.d[k] ^= 0x20;
    }
    /* consistent CRCs over impossible contents: rejected */
    {
        static const struct { size_t off; uint32_t v; } bad[] = {
            { 12 + 32, 6 },                             /* six high scores */
            { 12 + 40 + 4, 20261399 },                  /* month 13 */
            { 12 + 40 + 8, 0x7FFFFFFF },                /* the second entry above the first */
            { 12 + 40 + 40 + 4 * 8, 5 },                /* an unused place not zero (table 1 holds 4) */
            { 12 + 120, 2 },                            /* has_money 2 */
        };
        size_t base = 12 + (size_t)ms * 136;
        for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
            memcpy(img, mem.d, mem.n);
            put32le(img + base + bad[k].off - 12, bad[k].v);
            put32le(img + mem.n - 4, ce_crc32(img, mem.n - 4));
            if (sol_stats_deserialize(&back, img, mem.n) != 0) printf("  stats bad case %zu accepted\n", k);
            CHECK_EQ(sol_stats_deserialize(&back, img, mem.n), 0);
        }
        CHECK_EQ(st.m[ms].ntop[1], 4);                  /* (the case above assumes it) */
    }
    /* version 1 (Solitaire HD 1.1 / 1.2): 208 bytes, the eight values per mode; migrated with no high
     * scores and no money, written back as version 2 */
    {
        uint8_t v1[SOL_STATS_FILE_SIZE_V1];
        memcpy(v1, "SOLS", 4);
        put32le(v1 + 4, 1);
        put32le(v1 + 8, SOL_STATS_MODES);
        for (int i = 0; i < SOL_STATS_MODES; i++) {
            uint8_t *p = v1 + 12 + 32 * i;
            put32le(p, 10u + (unsigned)i);              /* played */
            put32le(p + 4, 3);                          /* won */
            put32le(p + 8, (uint32_t)-2);               /* streak */
            put32le(p + 12, 2);
            put32le(p + 16, 4);
            put32le(p + 20, 95);
            put32le(p + 24, (uint32_t)(i == 1 ? -27 : 1234));
            put32le(p + 28, i % 3 != 2);
        }
        put32le(v1 + 12 + 32 * SOL_STATS_MODES, ce_crc32(v1, 12 + 32 * SOL_STATS_MODES));
        CHECK_EQ(sol_stats_deserialize(&back, v1, sizeof v1), 1);
        CHECK(back.m[4].played == 14 && back.m[4].won == 3 && back.m[4].streak == -2 && back.m[4].win_streak == 2 &&
              back.m[4].loss_streak == 4 && back.m[4].best_time == 95 && back.m[4].best_score == 1234 &&
              back.m[4].has_score);
        CHECK(back.m[1].best_score == -27 && !back.m[2].has_score);
        CHECK(back.m[1].ntop[0] == 0 && back.m[1].ntop[1] == 0 && !back.m[1].has_money);
        mem_write(&mem, v1, sizeof v1);
        CHECK_EQ(sol_stats_load(&back, &io), 1);
        CHECK(sol_stats_save(&back, &io));
        CHECK_EQ(mem.n, SOL_STATS_FILE_SIZE);
        SolStats again;
        CHECK_EQ(sol_stats_load(&again, &io), 1);
        CHECK(!memcmp(&again, &back, sizeof back));
        v1[30] ^= 1;
        CHECK_EQ(sol_stats_deserialize(&back, v1, sizeof v1), 0);   /* the CRC */
        put32le(v1 + 4, 3);
        CHECK_EQ(sol_stats_deserialize(&back, v1, sizeof v1), 0);   /* an unknown version */
    }
    /* Reset clears the new values too */
    {
        SolSession s;
        Fake f;
        Reg r;
        start(&s, &f, &r, 0x09, 1);
        sol_attach_stats(&s, &st);
        CHECK(s.stats.m[mv].has_money && s.stats.m[ms].ntop[1] > 0);
        sol_reset_stats(&s);
        SolStats zero;
        sol_stats_clear(&zero);
        CHECK(!memcmp(&s.stats, &zero, sizeof zero));
        sol_free(&s);
    }
    mem_free(&mem);
}

/* ---- the prompts with "Save game on exit" + "Ask before saving or resuming" -------------------------------- */

/* "Save game on exit" with "Ask before saving or resuming" (the Windows 7 prompts), or neither */
static void save_on(SolSession *s, int on)
{
    SolExtras x = extras_of(s);
    x.save_game = on;
    x.ask_save_game = on;
    sol_set_extras(s, &x);
}

/* the two options one by one */
static void save_ask(SolSession *s, int save, int ask)
{
    SolExtras x = extras_of(s);
    x.save_game = save;
    x.ask_save_game = ask;
    sol_set_extras(s, &x);
}

/* "Save game on exit" alone saves and resumes silently (Windows 7's "Always save game on exit" and "Always
 * continue saved game"): Deal is XP's (a game in progress is lost), Exit saves, a resumed game goes on; "Ask
 * before saving or resuming" alone does nothing. */
static void test_silent_save(void)
{
    SolSession a, b;
    Fake fa, fb;
    Reg ra, rb;
    Mem mem;
    int m3 = sol_stats_mode(3, SOL_SCORING_STANDARD);
    memset(&mem, 0, sizeof mem);
    CeBlobIO io = mem_io(&mem);
    for (int ask_only = 0; ask_only < 2; ask_only++) {
        start(&a, &fa, &ra, 0x09, 5);
        sol_attach_stats(&a, NULL);
        save_ask(&a, !ask_only, ask_only);
        sol_press(&a, SOL_STOCK, 0, 0);
        CHECK(sol_game_started(&a));
        fa.answer = SOL_ANS_KEEP;                           /* (never asked) */
        fa.now = 606;
        sol_command(&a, SOL_CMD_DEAL);                      /* XP's deal: the game in progress is lost */
        CHECK_EQ(fa.nchoose, 0);
        CHECK(a.seed == 606u && a.nhist == 0);
        CHECK_EQ(a.stats.m[m3].streak, -1);
        sol_press(&a, SOL_STOCK, 0, 0);
        sol_press(&a, SOL_STOCK, 0, 0);
        CHECK_EQ(sol_exit_choice(&a), ask_only ? SOL_ANS_EXIT_NOSAVE : SOL_ANS_EXIT_SAVE);
        CHECK_EQ(fa.nchoose, 0);
        if (!ask_only) {
            CHECK(sol_game_save(&a, &io));
            start(&b, &fb, &rb, 0x09, -1);
            sol_attach_stats(&b, &a.stats);
            save_ask(&b, 1, 0);
            CHECK_EQ(sol_game_load(&b, &io), SOL_LOAD_OK);
            fb.answer = SOL_ANS_NEW;                        /* (never asked) */
            CHECK_EQ(sol_offer_resume(&b), 1);              /* continued silently */
            CHECK_EQ(fb.nchoose, 0);
            CHECK(b.seed == 606u && b.nhist == 2 && b.counted);
            sol_free(&b);
        }
        sol_free(&a);
    }
    mem_free(&mem);
}

static void test_new_game_prompt(void)
{
    SolSession s;
    Fake f;
    Reg r;
    int m3 = sol_stats_mode(3, SOL_SCORING_STANDARD);
    SolBoard dealt;
    /* off: XP, no question */
    start(&s, &f, &r, 0x09, 1);
    sol_attach_stats(&s, NULL);
    sol_press(&s, SOL_STOCK, 0, 0);
    f.now = 777;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(f.nchoose, 0);
    CHECK_EQ(s.seed, 777u);
    CHECK_EQ(s.stats.m[m3].streak, -1);
    /* on, nothing played yet: no question either */
    save_on(&s, 1);
    f.now = 778;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(f.nchoose, 0);
    CHECK_EQ(s.seed, 778u);
    dealt = s.board;
    /* on, a game in progress: Keep Playing changes nothing */
    sol_press(&s, SOL_STOCK, 0, 0);
    CHECK(sol_game_started(&s));
    f.answer = SOL_ANS_KEEP;
    f.now = 779;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(f.nchoose, 1);
    CHECK_EQ(f.last_choose, SOL_ASK_NEW_GAME);
    CHECK(s.seed == 778u && s.nhist == 1 && s.board.p[SOL_WASTE].n == 3);
    CHECK_EQ(s.stats.m[m3].played, 2);
    /* Restart This Game: the deal again, the history gone, still played once, not lost */
    s.ticks = 200;
    s.score = 40;
    f.answer = SOL_ANS_RESTART;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(f.nchoose, 2);
    CHECK(s.seed == 778u && s.nhist == 0 && s.nredo == 0 && sol_board_equal(&s.board, &dealt));
    CHECK(s.score == 0 && s.ticks == 0 && !s.input && s.dealt);
    CHECK(s.counted);
    CHECK_EQ(s.stats.m[m3].played, 2);
    CHECK_EQ(s.stats.m[m3].streak, -1);
    CHECK(!sol_game_started(&s) || s.counted);         /* (it still counts: the question comes again) */
    sol_command(&s, SOL_CMD_DEAL);                      /* asked: counted, though nothing to undo */
    CHECK_EQ(f.nchoose, 3);
    /* Quit and Start a New Game: the loss, a new deal */
    f.answer = SOL_ANS_QUIT_NEW;
    f.now = 780;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(f.nchoose, 4);
    CHECK_EQ(s.seed, 780u);
    CHECK_EQ(s.stats.m[m3].played, 2);
    CHECK_EQ(s.stats.m[m3].streak, -2);
    /* no UI for it: XP's deal */
    s.ui.choose = NULL;
    sol_press(&s, SOL_STOCK, 0, 0);
    f.now = 781;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.seed, 781u);
    sol_free(&s);

    /* Restart in Vegas: the game's money back before the bet is placed again (Cumulative: from the carry) */
    start(&s, &f, &r, 0x59, 1);                         /* Draw Three, Vegas, Cumulative */
    save_on(&s, 1);
    CHECK_EQ(s.score, -52);
    s.score = -27;                                      /* five cards home */
    f.now = 900;
    sol_command(&s, SOL_CMD_DEAL);                      /* nothing played: no question */
    CHECK_EQ(s.score, -79);
    CHECK_EQ(s.carry, -27);
    sol_press(&s, SOL_STOCK, 0, 0);
    s.score = -69;
    f.answer = SOL_ANS_RESTART;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.score, -79);
    CHECK_EQ(s.carry, -27);
    sol_free(&s);
    start(&s, &f, &r, 0x19, 1);                         /* Vegas, not Cumulative */
    save_on(&s, 1);
    sol_press(&s, SOL_STOCK, 0, 0);
    s.score = 3;
    f.answer = SOL_ANS_RESTART;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.score, -52);
    sol_free(&s);
}

static void test_exit_and_resume(void)
{
    SolSession a, b;
    Fake fa, fb;
    Reg ra, rb;
    Mem mem;
    int m3 = sol_stats_mode(3, SOL_SCORING_STANDARD);
    memset(&mem, 0, sizeof mem);
    CeBlobIO io = mem_io(&mem);
    start(&a, &fa, &ra, 0x09, 5);
    sol_attach_stats(&a, NULL);
    /* off: XP, exit without saving and without a question */
    sol_press(&a, SOL_STOCK, 0, 0);
    CHECK_EQ(sol_exit_choice(&a), SOL_ANS_EXIT_NOSAVE);
    CHECK_EQ(fa.nchoose, 0);
    /* on: the game above is in progress, so Deal asks (the default: quit, a new deal); then a fresh
     * deal is saved at Exit without a question */
    save_on(&a, 1);
    sol_command(&a, SOL_CMD_DEAL);
    CHECK_EQ(fa.nchoose, 1);
    CHECK_EQ(fa.last_choose, SOL_ASK_NEW_GAME);
    CHECK_EQ(sol_exit_choice(&a), SOL_ANS_EXIT_SAVE);
    CHECK_EQ(fa.nchoose, 1);
    /* on, a game in progress: the answer */
    sol_press(&a, SOL_STOCK, 0, 0);
    sol_press(&a, SOL_STOCK, 0, 0);
    for (int ans = 0; ans < 3; ans++) {
        fa.answer = ans;
        CHECK_EQ(sol_exit_choice(&a), ans);
        CHECK_EQ(fa.last_choose, SOL_ASK_EXIT);
    }
    fa.answer = -1;
    CHECK_EQ(sol_exit_choice(&a), SOL_ANS_EXIT_SAVE);   /* the default */
    CHECK(sol_game_save(&a, &io));

    /* start-up: Continue Saved Game */
    start(&b, &fb, &rb, 0x09, -1);
    sol_attach_stats(&b, &a.stats);
    save_on(&b, 1);
    CHECK_EQ(sol_game_load(&b, &io), SOL_LOAD_OK);
    fb.answer = SOL_ANS_CONTINUE;
    CHECK_EQ(sol_offer_resume(&b), 1);
    CHECK_EQ(fb.last_choose, SOL_ASK_RESUME);
    CHECK(b.seed == a.seed && b.nhist == 2 && b.counted);
    /* Play New Game: the saved game is lost */
    int played = b.stats.m[m3].played, streak = b.stats.m[m3].streak;
    CHECK_EQ(sol_game_load(&b, &io), SOL_LOAD_OK);
    fb.answer = SOL_ANS_NEW;
    fb.now = 4242;
    CHECK_EQ(sol_offer_resume(&b), 0);
    CHECK(b.seed == 4242u && b.nhist == 0 && b.dealt);
    CHECK_EQ(b.stats.m[m3].played, played);
    CHECK_EQ(b.stats.m[m3].streak, streak < 0 ? streak - 1 : -1);
    /* a saved deal nobody played: resumed without a question */
    int nc = fb.nchoose;
    sol_free(&a);
    start(&a, &fa, &ra, 0x09, 9);
    CHECK(sol_game_save(&a, &io));
    CHECK_EQ(sol_game_load(&b, &io), SOL_LOAD_OK);
    CHECK_EQ(sol_offer_resume(&b), 1);
    CHECK_EQ(fb.nchoose, nc);
    CHECK_EQ(b.seed, 9u);
    /* option off: never asked */
    save_on(&b, 0);
    sol_press(&b, SOL_STOCK, 0, 0);
    CHECK_EQ(sol_offer_resume(&b), 1);
    CHECK_EQ(fb.nchoose, nc);
    sol_free(&a);
    sol_free(&b);
    mem_free(&mem);
}

/* ---- "Apply option changes to the next game" ------------------------------------------------------------- */

static void next_on(SolSession *s, int on)
{
    SolExtras x = extras_of(s);
    x.next_game_options = on;
    sol_set_extras(s, &x);
}

static void test_next_game_options(void)
{
    SolSession s;
    Fake f;
    Reg r;
    SolOptions o;
    uint32_t v;
    int m3 = sol_stats_mode(3, SOL_SCORING_STANDARD);
    start(&s, &f, &r, 0x09, 1);                         /* Draw Three, Standard, untimed, status bar */
    sol_attach_stats(&s, NULL);
    next_on(&s, 1);
    /* nothing played: XP's redeal at once, no question */
    o = s.opts;
    o.draw = 1;
    f.now = 600;
    CHECK_EQ(sol_apply_options(&s, &o), 1);
    CHECK_EQ(f.nchoose, 0);
    CHECK(s.draw == 1 && s.seed == 600u && !s.pending);
    /* a game in progress: Finish This Game keeps it, the new Options wait */
    sol_press(&s, SOL_STOCK, 0, 0);
    f.answer = SOL_ANS_FINISH;
    o = s.opts;
    o.draw = 3;
    o.scoring = SOL_SCORING_VEGAS;
    o.status_bar = 0;
    o.outline = 1;
    CHECK_EQ(sol_apply_options(&s, &o), 0);
    CHECK_EQ(f.nchoose, 1);
    CHECK_EQ(f.last_choose, SOL_ASK_SETTINGS);
    CHECK(s.pending && s.seed == 600u && s.nhist == 1);
    CHECK(s.opts.draw == 1 && s.opts.scoring == SOL_SCORING_STANDARD && s.draw == 1);   /* the game's */
    CHECK(s.opts.status_bar == 0 && s.opts.outline == 1);                              /* at once */
    CHECK(sol_dialog_options(&s)->draw == 3 && sol_dialog_options(&s)->scoring == SOL_SCORING_VEGAS);
    CHECK(reg_get(&r, "Options", &v) && v == sol_options_pack(&o));   /* written, as XP */
    sol_press(&s, SOL_STOCK, 0, 0);
    CHECK_EQ(s.board.p[SOL_WASTE].n, 2);                /* still drawing one */
    CHECK_EQ(s.stats.m[sol_stats_mode(1, SOL_SCORING_STANDARD)].played, 1);
    /* the same change again: no second question */
    CHECK_EQ(sol_apply_options(&s, &o), 0);
    CHECK_EQ(f.nchoose, 1);
    /* the option turned off while they wait: an OK that leaves the waiting settings alone (the dialog
     * shows them) keeps the game and the wait - no redeal, no loss, no question */
    next_on(&s, 0);
    {
        SolOptions w = *sol_dialog_options(&s);
        w.status_bar = 1;
        f.now = 777;
        CHECK_EQ(sol_apply_options(&s, &w), 0);
        CHECK(s.pending && s.seed == 600u && s.nhist == 2 && s.opts.status_bar == 1 && s.draw == 1);
        CHECK_EQ(f.nchoose, 1);
        CHECK_EQ(s.stats.m[sol_stats_mode(1, SOL_SCORING_STANDARD)].streak, 0);
        w.status_bar = 0;
        CHECK_EQ(sol_apply_options(&s, &w), 0);   /* (back as before) */
    }
    next_on(&s, 1);
    /* Restart This Game keeps the game's settings */
    sol_restart(&s);
    CHECK(s.pending && s.draw == 1 && s.seed == 600u && s.opts.scoring == SOL_SCORING_STANDARD);
    /* the next deal: the new Options, an Options redeal (the Vegas bet from 0), the game lost in its mode */
    f.now = 601;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK(!s.pending && s.draw == 3 && s.game_scoring == SOL_SCORING_VEGAS && s.score == -52);
    CHECK_EQ(s.stats.m[sol_stats_mode(1, SOL_SCORING_STANDARD)].streak, -1);
    CHECK(sol_dialog_options(&s) == &s.opts);
    /* Play New Game: XP's redeal */
    sol_press(&s, SOL_STOCK, 0, 0);
    f.answer = SOL_ANS_PLAY_NEW;
    o = s.opts;
    o.scoring = SOL_SCORING_STANDARD;
    f.now = 602;
    CHECK_EQ(sol_apply_options(&s, &o), 1);
    CHECK(!s.pending && s.seed == 602u && s.game_scoring == SOL_SCORING_STANDARD);
    CHECK_EQ(s.stats.m[sol_stats_mode(3, SOL_SCORING_VEGAS)].played, 1);
    /* back to the game's own settings while they wait: nothing waits any more, no redeal */
    sol_press(&s, SOL_STOCK, 0, 0);
    f.answer = SOL_ANS_FINISH;
    o = s.opts;
    o.timed = 1;
    CHECK_EQ(sol_apply_options(&s, &o), 0);
    CHECK(s.pending && !s.opts.timed);
    o.timed = 0;
    CHECK_EQ(sol_apply_options(&s, &o), 0);
    CHECK(!s.pending && s.seed == 602u);
    /* option off: XP */
    next_on(&s, 0);
    o.timed = 1;
    f.now = 603;
    CHECK_EQ(sol_apply_options(&s, &o), 1);
    CHECK(s.seed == 603u && s.opts.timed);
    CHECK_EQ(s.stats.m[m3].played, 1);
    sol_free(&s);

    /* "Deal only winnable games" picks its seed for the new Options */
    start(&s, &f, &r, 0x09, 1);
    {
        SolExtras x = s.extras;
        x.next_game_options = 1;
        x.winnable_only = 1;
        sol_set_extras(&s, &x);
    }
    sol_press(&s, SOL_STOCK, 0, 0);
    f.answer = SOL_ANS_FINISH;
    o = s.opts;
    o.draw = 1;
    o.scoring = SOL_SCORING_VEGAS;
    sol_apply_options(&s, &o);
    CHECK(s.pending);
    f.now = 12345;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(sol_seed_status(sol_seeds_case(1, SOL_SCORING_VEGAS), s.seed), SOL_SEED_WINNABLE);
    sol_free(&s);

    /* a saved game under other Options: kept, with its own; the current ones wait */
    {
        SolSession a, b;
        Fake fa, fb;
        Reg ra, rb;
        Mem mem;
        memset(&mem, 0, sizeof mem);
        CeBlobIO io = mem_io(&mem);
        start(&a, &fa, &ra, 0x09, 77);                  /* Draw Three, Standard */
        sol_press(&a, SOL_STOCK, 0, 0);
        CHECK(sol_game_save(&a, &io));
        start(&b, &fb, &rb, 0x13, -1);                  /* Draw One, Vegas, timed now */
        CHECK_EQ(sol_game_load(&b, &io), SOL_LOAD_OTHER_OPTIONS);
        next_on(&b, 1);
        CHECK_EQ(sol_game_load(&b, &io), SOL_LOAD_OK);
        CHECK(b.seed == 77u && b.draw == 3 && b.game_scoring == SOL_SCORING_STANDARD && !b.game_timed);
        CHECK(b.opts.draw == 3 && b.opts.scoring == SOL_SCORING_STANDARD && !b.opts.timed);
        CHECK(b.pending && b.pend_opts.draw == 1 && b.pend_opts.scoring == SOL_SCORING_VEGAS && b.pend_opts.timed);
        CHECK(b.opts.status_bar == b.pend_opts.status_bar);
        sol_press(&b, SOL_STOCK, 0, 0);
        CHECK_EQ(b.board.p[SOL_WASTE].n, 6);            /* drawing three */
        sol_command(&b, SOL_CMD_DEAL);
        CHECK(!b.pending && b.draw == 1 && b.game_scoring == SOL_SCORING_VEGAS && b.opts.timed);
        sol_free(&a);
        sol_free(&b);
        mem_free(&mem);
    }
}

int main(void)
{
    test_hint_cycle();
    test_hint_list_random();
    test_hint_down();
    test_dead_cycle();
    test_no_more_moves_question();
    test_high_scores();
    test_stats_file();
    test_new_game_prompt();
    test_exit_and_resume();
    test_silent_save();
    test_next_game_options();
    return test_summary("test_sol_win7");
}
