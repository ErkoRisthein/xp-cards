/*
 * Solitaire HD — native tests of the v1.1 extras (session.c, assist.c, stats.c, savegame.c):
 * auto-turn (scoring as XP's click, one undo step, Redo), click-to-move (the destination ranking, the
 * session flow), Finish and Finish automatically (lowest card first, flights, one action, the win), the
 * fair Hint (its ranking, invariance under shuffled hidden cards, the flash, input ending it, no hint),
 * statistics (played at the first action, wins, losses, streaks, best time and score, per mode, the
 * file), save and resume (an exact round trip incl. the history, Undo / Redo after it, damaged and
 * foreign files), winnable deals only (the table), the unwinnable warning (requests, once, re-armed,
 * stale answers), the extras' registry values. The proof that all of it off is XP is
 * tests/solitaire/sol_xp_compare.c (make sol-xp-compare).
 */
#include "sol_test.h"
#include "solitaire/assist.h"
#include "solitaire/savegame.h"
#include "solitaire/solver.h"
#include "solitaire/stats.h"
#include "solitaire/winnable_seeds.h"

#define T(k) (SOL_TAB0 + (k))
#define F(k) (SOL_FOUND0 + (k))

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
}

/* A board with no stock: the given piles hold all 52 cards. */
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
}

/* Every card not on the given piles goes face up to the waste, `top` last; the stock is empty. */
static void board_waste(SolSession *s, const char *top, ...)
{
    va_list ap;
    sol_board_clear(&s->board);
    va_start(ap, top);
    for (;;) {
        int p = va_arg(ap, int);
        if (p < 0) break;
        set_pile(&s->board, p, va_arg(ap, const char *));
    }
    va_end(ap);
    fill_stock(&s->board);
    SolPile *st = &s->board.p[SOL_STOCK], *w = &s->board.p[SOL_WASTE];
    SolCard t = C(top);
    for (int i = 0; i < st->n; i++)
        if (sol_card_id(st->c[i]) != sol_card_id(t)) w->c[w->n++] = (SolCard)(st->c[i] | SOL_UP);
    w->c[w->n++] = t;
    st->n = 0;
    char why[128] = "";
    if (!sol_board_valid(&s->board, why, sizeof why)) printf("  waste board invalid: %s\n", why);
    s->waste_fan = 0;
    s->nhist = s->nredo = 0;
}

static void extras_on(SolSession *s, int turn, int click, int finish)
{
    SolExtras x = s->extras;
    x.auto_turn = turn;
    x.click_move = click;
    x.auto_finish = finish;
    sol_set_extras(s, &x);
}

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

/* ---- auto-turn -------------------------------------------------------------------------------------- */

static void test_auto_turn(void)
{
    SolSession s;
    Fake f;
    Reg r;
    static const uint32_t modes[3] = { 0x0B, 0x1B, 0x2B };
    for (int m = 0; m < 3; m++) {
        start(&s, &f, &r, modes[m], 1);
        int base = s.score;
        extras_on(&s, 1, 0, 0);
        board(&s, T(0), "#2C #9C 5H", T(1), "6S", T(2), "#3C AD", T(3), "KD", -1);
        SolBoard before = s.board;
        /* a drop that uncovers #9C: turned over in the same action, +5 more in Standard */
        CHECK(sol_begin_drag(&s, T(0), 2));
        CHECK(sol_drop(&s, T(1)));
        CHECK(pile_is(&s.board, T(0), "#2C 9C"));
        CHECK(pile_is(&s.board, T(1), "6S 5H"));
        CHECK_EQ(s.nhist, 1);
        CHECK_EQ(s.hist[0].autoturn, 1);
        CHECK_EQ(s.score, base + (m == 0 ? 5 : 0));
        /* a double-click home uncovering #3C */
        CHECK_EQ(sol_dblclick(&s, T(2), 1, 0), SOL_PRESS_DONE);
        CHECK(pile_is(&s.board, T(2), "3C"));
        CHECK_EQ(s.hist[1].autoturn, 1 << 2);
        CHECK_EQ(s.score, base + (m == 0 ? 20 : m == 1 ? 5 : 0));
        /* Undo: one step back puts the card face down again; the XP Undo charge applies */
        CHECK(sol_undo(&s));
        CHECK(pile_is(&s.board, T(2), "#3C AD"));
        CHECK(sol_undo(&s));
        CHECK(sol_board_equal(&s.board, &before));
        /* Redo turns them again, scored again */
        CHECK(sol_redo(&s));
        CHECK(pile_is(&s.board, T(0), "#2C 9C"));
        CHECK(sol_redo(&s));
        CHECK(pile_is(&s.board, T(2), "3C"));
        CHECK_EQ(s.hist[1].autoturn, 1 << 2);
        /* a face-down top card left from before (the option turned on later, or a manual turn undone) is
         * turned over by the next action, whatever it is (here a draw) */
        board(&s, T(3), "#KD", -1);
        CHECK(pile_is(&s.board, T(3), "#KD"));
        int sc = s.score;
        CHECK_EQ(sol_press(&s, SOL_STOCK, 0, 0), SOL_PRESS_DONE);
        CHECK(pile_is(&s.board, T(3), "KD"));
        CHECK_EQ(s.hist[0].type, SOL_ACT_DRAW);
        CHECK_EQ(s.hist[0].autoturn, 1 << 3);
        CHECK_EQ(s.score, sc + (m == 0 ? 5 : 0));
        CHECK(sol_undo(&s));
        CHECK(pile_is(&s.board, T(3), "#KD"));
        sol_free(&s);
    }
    /* off: XP, the card stays face down until clicked (+5 then) */
    start(&s, &f, &r, 0, 1);
    board(&s, T(0), "#2C #9C 5H", T(1), "6S", -1);
    CHECK(sol_begin_drag(&s, T(0), 2) && sol_drop(&s, T(1)));
    CHECK(pile_is(&s.board, T(0), "#2C #9C"));
    CHECK_EQ(s.score, 0);
    CHECK_EQ(sol_press(&s, T(0), 1, 0), SOL_PRESS_DONE);
    CHECK_EQ(s.score, 5);
    sol_free(&s);
    /* autoplay (right button) uncovering cards */
    start(&s, &f, &r, 0, 1);
    extras_on(&s, 1, 0, 0);
    board(&s, T(0), "#9C AC", T(1), "#TC 2C", -1);
    CHECK_EQ(sol_autoplay(&s), 2);
    CHECK(pile_is(&s.board, T(0), "9C") && pile_is(&s.board, T(1), "TC"));
    CHECK_EQ(s.nhist, 1);
    CHECK_EQ(s.hist[0].autoturn, 3);
    CHECK_EQ(s.score, 20 + 10);
    sol_free(&s);
}

/* ---- click-to-move ------------------------------------------------------------------------------------ */

static void test_click_dest(void)
{
    SolSession s;
    Fake f;
    Reg r;
    start(&s, &f, &r, 0, 1);
    board(&s, SOL_WASTE, "QH 2D", T(0), "#3C 6H 5S", T(1), "", T(2), "KS", T(3), "#4C 7C", T(4), "7S",
          T(5), "#5C KH QC", F(0), "AD", -1);
    const SolBoard *b = &s.board;
    CHECK_EQ(sol_click_dest(b, SOL_WASTE, 1), F(0));          /* 2D home first */
    CHECK_EQ(sol_click_dest(b, SOL_WASTE, 0), -1);            /* not the waste's top */
    CHECK_EQ(sol_click_dest(b, T(0), 2), -1);                 /* 5S: no 6 red free, no home */
    CHECK_EQ(sol_click_dest(b, T(0), 1), T(3));               /* 6H 5S onto 7C: the first column that takes it */
    CHECK_EQ(sol_click_dest(b, T(2), 0), -1);                 /* KS heads its column: stays */
    CHECK_EQ(sol_click_dest(b, T(5), 1), T(1));               /* KH QC (on a face-down card): the empty column */
    CHECK_EQ(sol_click_dest(b, T(5), 0), -1);                 /* face down */
    CHECK_EQ(sol_click_dest(b, F(0), 0), -1);                 /* a foundation's card: never by a click */
    CHECK_EQ(sol_click_dest(b, SOL_STOCK, b->p[0].n - 1), -1);
    /* 6H onto 7C (T3) or 7S (T4): the leftmost */
    board(&s, T(0), "#3C 6H", T(3), "7C", T(4), "7S", -1);
    CHECK_EQ(sol_click_dest(b, T(0), 1), T(3));
    board(&s, T(0), "#3C 6H", T(3), "7D", T(4), "7S", -1);
    CHECK_EQ(sol_click_dest(b, T(0), 1), T(4));
    /* ... unless the leftmost could go home itself: 7C (clubs up to 6 home) is not buried */
    board(&s, T(0), "#9D 6H", T(3), "7C", T(4), "7S", F(0), "AC 2C 3C 4C 5C 6C", -1);
    CHECK_EQ(sol_click_dest(b, T(0), 1), T(4));
    board(&s, T(0), "#9D 6H", T(3), "7C", T(4), "#4D 7D", F(0), "AC 2C 3C 4C 5C 6C", -1);
    CHECK_EQ(sol_click_dest(b, T(0), 1), T(3));              /* the only column that takes it */
    board(&s, T(0), "#9D 6H", T(3), "7S", T(4), "7C", F(0), "AS 2S 3S 4S 5S 6S", F(1), "AC 2C 3C 4C 5C 6C", -1);
    CHECK_EQ(sol_click_dest(b, T(0), 1), T(3));              /* both could go home: the leftmost */
    /* a single card: home before the tableau */
    board(&s, T(0), "#3C 2H", T(1), "3S", F(2), "AH", -1);
    CHECK_EQ(sol_click_dest(b, T(0), 1), F(2));
    /* an ace to the leftmost empty foundation */
    board(&s, T(0), "#3C AS", F(0), "AD", -1);
    CHECK_EQ(sol_click_dest(b, T(0), 1), F(1));

    /* the session flow: off -> no target; on -> the drop goes there, as an ordinary move */
    board(&s, SOL_WASTE, "QH 2D", T(0), "#3C 6H 5S", T(3), "#4C 7C", F(0), "AD", -1);
    CHECK_EQ(sol_press(&s, SOL_WASTE, 1, 0), SOL_PRESS_DRAG);
    CHECK_EQ(sol_click_target(&s), -1);
    sol_cancel_drag(&s);
    extras_on(&s, 0, 1, 0);
    CHECK_EQ(sol_press(&s, SOL_WASTE, 1, 0), SOL_PRESS_DRAG);
    CHECK_EQ(sol_click_target(&s), F(0));
    CHECK(sol_drop(&s, sol_click_target(&s)));
    CHECK(pile_is(&s.board, F(0), "AD 2D"));
    CHECK_EQ(s.score, 10);
    CHECK_EQ(sol_press(&s, T(0), 1, 0), SOL_PRESS_DRAG);
    CHECK_EQ(sol_click_target(&s), T(3));
    CHECK(sol_drop(&s, T(3)));
    CHECK(pile_is(&s.board, T(3), "#4C 7C 6H 5S"));
    CHECK_EQ(s.nhist, 2);
    CHECK(!sol_dragging(&s));
    CHECK_EQ(sol_click_target(&s), -1);                       /* no drag */
    sol_free(&s);
}

/* ---- Finish ------------------------------------------------------------------------------------------- */

static void test_finish(void)
{
    SolSession s;
    Fake f;
    Reg r;
    start(&s, &f, &r, 0, 1);
    /* everything face up, no stock or waste: the lowest card first */
    board_exact(&s, F(0), "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC", F(1), "AD 2D 3D 4D 5D 6D 7D 8D 9D TD JD QD",
                F(2), "AH 2H 3H 4H 5H 6H 7H 8H 9H TH", F(3), "AS 2S 3S 4S 5S 6S 7S 8S 9S TS JS",
                T(0), "KH QC JH", T(1), "KD QS", T(2), "KC", T(3), "KS QH", -1);
    CHECK(sol_finish_ready(&s.board));
    CHECK(sol_finish_enabled(&s));
    int src, dst;
    CHECK(sol_finish_step(&s.board, &src, &dst));
    CHECK_EQ(src, T(0));                                      /* JH, the lowest */
    CHECK_EQ(dst, F(2));
    s.score = 100;
    f.nanim = 0;
    sol_command(&s, SOL_CMD_FINISH);
    CHECK_EQ(f.nanim, 8);
    CHECK(f.anim[0][0] == T(0) && f.anim[0][1] == F(2));      /* JH */
    CHECK(f.anim[1][0] == T(0) && f.anim[1][1] == F(0));      /* QC, the leftmost queen */
    CHECK(f.anim[2][0] == T(1) && f.anim[2][1] == F(3));      /* QS */
    CHECK(f.anim[3][0] == T(3) && f.anim[3][1] == F(2));      /* QH */
    CHECK(f.anim[4][0] == T(0) && f.anim[4][1] == F(2));      /* KH */
    CHECK_EQ(f.ncascade, 1);                                  /* won */
    CHECK(!s.dealt && s.won && !s.forced_win);
    CHECK_EQ(s.score - s.bonus, 100 + 8 * 10);
    sol_free(&s);

    /* not ready: a face-down card, a stock card, a waste card */
    start(&s, &f, &r, 0, 1);
    board_exact(&s, F(0), "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC", F(1), "AD 2D 3D 4D 5D 6D 7D 8D 9D TD JD QD",
                F(2), "AH 2H 3H 4H 5H 6H 7H 8H 9H TH", F(3), "AS 2S 3S 4S 5S 6S 7S 8S 9S TS JS",
                T(0), "#KH QC JH", T(1), "KD QS", T(2), "KC", T(3), "KS QH", -1);
    CHECK(!sol_finish_ready(&s.board) && !sol_finish_enabled(&s));
    sol_command(&s, SOL_CMD_FINISH);
    CHECK_EQ(f.nanim, 0);
    CHECK(s.dealt);
    /* with "Finish automatically" and auto-turn: turning the last card over finishes */
    extras_on(&s, 1, 0, 1);
    CHECK_EQ(sol_press(&s, T(0), 0, 0), SOL_PRESS_NONE);      /* a buried face-down card: nothing */
    board_exact(&s, F(0), "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC", F(1), "AD 2D 3D 4D 5D 6D 7D 8D 9D TD JD QD",
                F(2), "AH 2H 3H 4H 5H 6H 7H 8H 9H TH", F(3), "AS 2S 3S 4S 5S 6S 7S 8S 9S TS",
                T(0), "#KH QC JH", T(1), "KC QH", T(2), "KD QS", T(3), "KS", T(4), "JS", -1);
    /* JS, JH, QC home by double-clicks; the last leaves #KH on top: auto-turn turns it, and Finish
     * follows by itself */
    CHECK_EQ(sol_dblclick(&s, T(4), 0, 0), SOL_PRESS_DONE);   /* JS home */
    CHECK(s.dealt);
    CHECK_EQ(sol_dblclick(&s, T(0), 2, 0), SOL_PRESS_DONE);   /* JH home */
    CHECK(s.dealt);
    f.nanim = 0;
    CHECK_EQ(sol_dblclick(&s, T(0), 1, 0), SOL_PRESS_DONE);   /* QC home: KH turned, then the finish */
    CHECK(!s.dealt && s.won);
    CHECK_EQ(f.nanim, 6);                                     /* KH, KC.. the rest flown */
    CHECK_EQ(f.ncascade, 1);
    sol_free(&s);

    /* "Finish automatically" alone after a drop; not after Undo */
    start(&s, &f, &r, 0, 1);
    board_exact(&s, F(0), "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC", F(1), "AD 2D 3D 4D 5D 6D 7D 8D 9D TD JD QD",
                F(2), "AH 2H 3H 4H 5H 6H 7H 8H 9H TH", F(3), "AS 2S 3S 4S 5S 6S 7S 8S 9S TS JS",
                T(0), "KH QC", T(1), "KD QS", T(2), "KC", T(3), "KS QH", T(4), "JH", -1);
    extras_on(&s, 0, 0, 1);
    CHECK(sol_begin_drag(&s, T(4), 0));
    CHECK(sol_drop(&s, T(0)));                                /* JH onto QC: then the finish */
    CHECK(s.won);
    CHECK_EQ(f.ncascade, 1);
    sol_free(&s);
}

/* ---- Hint ------------------------------------------------------------------------------------------- */

static SolHintMove hint_of(const SolBoard *b, int recycle_ok)
{
    SolHintMove m;
    sol_hint_find(b, recycle_ok, &m);
    return m;
}

/* Shuffle the hidden cards of b (face-down tableau cards and the stock) among themselves. */
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

static void test_hint_rank(void)
{
    SolSession s;
    Fake f;
    Reg r;
    SolHintMove m;
    start(&s, &f, &r, 0, 1);
    const SolBoard *b = &s.board;

    /* 1. a face-down top card first */
    board(&s, T(0), "#2C", T(1), "AS", -1);
    m = hint_of(b, 1);
    CHECK(m.kind == SOL_HINT_TURN && m.src == T(0) && m.cls == SOL_HC_TURN);
    /* 2. an ace home before a revealing run move */
    board(&s, T(0), "#2C 5H", T(1), "6S", T(2), "AS", -1);
    m = hint_of(b, 1);
    CHECK(m.kind == SOL_HINT_MOVE && m.src == T(2) && m.dst == F(0) && m.cls == SOL_HC_LOW_HOME);
    /* 5. a run uncovering a face-down card; the column with more face-down cards wins */
    board(&s, T(0), "#2C 5H", T(1), "6S", T(2), "#3C #4C 5D", T(3), "6C", -1);
    m = hint_of(b, 1);
    CHECK(m.kind == SOL_HINT_MOVE && m.src == T(2) && m.index == 2 && m.dst == T(1) && m.cls == SOL_HC_REVEAL);
    /* a king's run onto an empty column when it uncovers something */
    board(&s, T(0), "#2C #3C KH QS", T(1), "", -1);
    m = hint_of(b, 1);
    CHECK(m.src == T(0) && m.index == 2 && m.dst == T(1) && m.cls == SOL_HC_REVEAL);
    /* never: a king heading its column moved to another empty column; a sideways move that frees
     * nothing; a foundation card back down */
    board(&s, T(0), "KH", T(1), "", T(2), "KD", T(3), "QC JH", T(4), "QS", F(0), "AC 2C 3C 4C 5C 6C 7C 8C 9C TC", -1);
    s.board.p[SOL_STOCK].n = 0;   /* (the stock's cards are irrelevant here: drop them from view) */
    m = hint_of(b, 0);
    CHECK_EQ(m.kind, SOL_HINT_NONE);                          /* KH / KD to T1, TC down, JH to QS, a column
                                                                 emptied for no king: none of them */
    /* 7. a sideways move that frees a card for home: JH off QC so that QC can go home (JC home) */
    board(&s, T(3), "#2D QC JH", T(4), "QS", F(0), "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC", -1);
    m = hint_of(b, 1);
    CHECK(m.kind == SOL_HINT_MOVE && m.src == T(3) && m.index == 2 && m.dst == T(4) && m.cls == SOL_HC_FREE_HOME);
    /* ... but never onto a card that could go home itself: 4S off 5H onto 5D (both bound for home)
     * gains nothing, and the next hint would move it back; 5D home instead */
    board(&s, T(0), "#2C 5H 4S", T(1), "6C 5D", F(0), "AH 2H 3H 4H", F(1), "AD 2D 3D 4D", -1);
    m = hint_of(b, 1);
    CHECK(m.kind == SOL_HINT_MOVE && m.src == T(1) && m.dst == F(1) && m.cls == SOL_HC_HOME);
    board(&s, T(0), "#2C 5H", T(1), "6C 5D 4S", F(0), "AH 2H 3H 4H", F(1), "AD 2D 3D 4D", -1);
    m = hint_of(b, 1);
    CHECK(m.kind == SOL_HINT_MOVE && m.src == T(0) && m.dst == F(0) && m.cls == SOL_HC_HOME_REVEAL);
    /* (the hint never goes round in circles: test_hint_follow) */
    /* 8. the waste's card to the tableau before drawing */
    board(&s, SOL_WASTE, "5H", T(0), "6S", -1);
    m = hint_of(b, 1);
    CHECK(m.kind == SOL_HINT_MOVE && m.src == SOL_WASTE && m.dst == T(0) && m.cls == SOL_HC_WASTE_TAB);
    /* 3. safe home move vs 10. other home move */
    board(&s, T(0), "5H", F(0), "AC 2C 3C 4C", F(1), "AS 2S 3S 4S", F(2), "AH 2H 3H 4H", -1);
    m = hint_of(b, 1);
    CHECK(m.src == T(0) && m.dst == F(2) && m.cls == SOL_HC_SAFE_HOME);
    board(&s, T(0), "5H", F(0), "AC 2C 3C", F(1), "AS 2S 3S 4S", F(2), "AH 2H 3H 4H", -1);
    m = hint_of(b, 1);
    CHECK(m.src == T(0) && m.dst == F(2) && m.cls == SOL_HC_HOME);
    /* 9. emptying a column only when a king waits for it */
    board(&s, T(0), "5H", T(1), "6S", T(2), "#3C KD", T(3), "KC", T(4), "KH", T(5), "KS", T(6), "QD", -1);
    m = hint_of(b, 1);
    if (!(m.src == T(0) && m.dst == T(1) && m.cls == SOL_HC_EMPTY_COL))
        printf("  empty-col hint: kind %d %d.%d -> %d class %d\n", m.kind, m.src, m.index, m.dst, m.cls);
    CHECK(m.src == T(0) && m.dst == T(1) && m.cls == SOL_HC_EMPTY_COL);
    board(&s, T(0), "5H", T(1), "6S", -1);
    m = hint_of(b, 1);
    CHECK(m.kind == SOL_HINT_DRAW && m.cls == SOL_HC_DRAW);   /* nothing better: draw */
    /* 12. recycle when the stock is empty, if the pass limit allows (5C on the waste plays nowhere) */
    board_waste(&s, "5C", T(0), "KC", T(1), "KD", T(2), "KH", T(3), "KS", T(4), "QC", -1);
    m = hint_of(b, 1);
    CHECK(m.kind == SOL_HINT_RECYCLE && m.cls == SOL_HC_RECYCLE && m.src == SOL_STOCK);
    m = hint_of(b, 0);
    CHECK_EQ(m.kind, SOL_HINT_NONE);
    sol_free(&s);

    /* the recycle and "no hint" through the session */
    start(&s, &f, &r, 0x1B, 28172);   /* Vegas, draw three: 3 passes */
    while (s.board.p[SOL_STOCK].n) sol_press(&s, SOL_STOCK, 0, 0);
    m = hint_of(&s.board, 1);
    CHECK(m.kind != SOL_HINT_DRAW);
    sol_free(&s);
}

/* Following the hint with a pass limit (Vegas) always ends, in a win or in "no hint": it never moves
 * cards back and forth. Every hint is a legal action of the session. */
static void test_hint_follow(void)
{
    int games = 0, wins = 0, steps_max = 0, illegal = 0, endless = 0;
    for (unsigned g = 0; g < 400; g++) {
        SolSession s;
        Fake f;
        Reg r;
        int steps = 0;
        start(&s, &f, &r, g & 1 ? 0x1B : 0x13, (int)(g * 7919u % 32768u));   /* Vegas, Draw Three / One */
        for (; steps < 3000 && s.dealt; steps++) {
            SolHintMove m;
            int before = s.nhist;
            sol_hint(&s);
            if (!s.hint)
                break;                                            /* "No hint is available." */
            m = s.hint_move;
            if (m.kind == SOL_HINT_MOVE) {
                if (!sol_begin_drag(&s, m.src, m.index) || !sol_drop(&s, m.dst))
                    illegal++;
            } else if (m.kind == SOL_HINT_TURN) {
                sol_press(&s, m.src, m.index, 0);
            } else {
                sol_press(&s, SOL_STOCK, s.board.p[SOL_STOCK].n ? s.board.p[SOL_STOCK].n - 1 : -1, 0);
            }
            if (s.dealt && s.nhist != before + 1) {
                illegal++;
                break;
            }
        }
        games++;
        if (!s.dealt) wins++;
        if (steps >= 3000) endless++;
        if (steps > steps_max) steps_max = steps;
        sol_free(&s);
    }
    CHECK_EQ(illegal, 0);
    CHECK_EQ(endless, 0);
    CHECK(wins > 0);
    printf("  hint followed: %d Vegas games, %d won by the hint alone, at most %d steps\n", games, wins, steps_max);
}

static void test_hint_fair(void)
{
    /* every position of random play: the hint never changes when the hidden cards are shuffled */
    int positions = 0, differ = 0;
    for (unsigned g = 0; g < 60; g++) {
        SolSession s;
        Fake f;
        Reg r;
        start(&s, &f, &r, g & 1 ? 0x03 : 0x0B, (int)(g * 977u % 32768u));
        tsrand(g + 99);
        for (int k = 0; k < 250 && s.dealt; k++) {
            SolHintMove a, b2;
            SolBoard t = s.board;
            int rec = s.board.p[SOL_STOCK].n == 0 && s.board.p[SOL_WASTE].n > 0;
            sol_hint_find(&s.board, rec, &a);
            for (int v = 0; v < 3; v++) {
                shuffle_hidden(&t, g * 31u + (unsigned)k * 7u + (unsigned)v);
                sol_hint_find(&t, rec, &b2);
                positions++;
                if (memcmp(&a, &b2, sizeof a)) differ++;
            }
            /* play the hint itself most of the time, so that games go deep */
            if (trand() % 4) {
                if (a.kind == SOL_HINT_MOVE) { sol_begin_drag(&s, a.src, a.index); sol_drop(&s, a.dst); }
                else if (a.kind == SOL_HINT_TURN) sol_press(&s, a.src, a.index, 0);
                else if (a.kind != SOL_HINT_NONE) sol_press(&s, SOL_STOCK, 0, 0);
                else break;
            } else {
                sol_press(&s, SOL_STOCK, 0, 0);
            }
        }
        sol_free(&s);
    }
    CHECK(positions > 10000);
    CHECK_EQ(differ, 0);
    printf("  hint fairness: %d shuffled positions, %d differences\n", positions, differ);
}

static void test_hint_flash(void)
{
    SolSession s;
    Fake f;
    Reg r;
    int p, c;
    start(&s, &f, &r, 0, 1);
    board(&s, T(0), "#2C 5H", T(1), "6S", -1);
    CHECK(sol_hint_enabled(&s));
    sol_command(&s, SOL_CMD_HINT);
    CHECK(s.hint);
    CHECK_EQ(f.hint_timer_ms, SOL_HINT_STEP_MS);
    int want[8][2] = { { T(0), 1 }, { -1, -1 }, { T(0), 1 }, { -1, -1 }, { T(1), 0 }, { -1, -1 }, { T(1), 0 }, { -1, -1 } };
    for (int k = 0; k < 8; k++) {
        sol_hint_view(&s, &p, &c);
        CHECK(p == want[k][0] && c == want[k][1]);
        sol_timer(&s, SOL_TIMER_HINT);
    }
    CHECK(!s.hint);
    CHECK_EQ(f.hint_timer_ms, 0);
    sol_hint_view(&s, &p, &c);
    CHECK_EQ(p, -1);
    CHECK_EQ(s.nhist, 0);                                     /* nothing moved */
    CHECK(!s.input);                                          /* the clock did not start */
    /* any input ends it */
    sol_command(&s, SOL_CMD_HINT);
    CHECK(s.hint);
    sol_key(&s, SOL_KEY_LEFT, 0);
    CHECK(!s.hint && f.hint_timer_ms == 0);
    sol_command(&s, SOL_CMD_HINT);
    sol_press(&s, SOL_STOCK, 0, 0);
    CHECK(!s.hint);
    sol_command(&s, SOL_CMD_HINT);
    sol_timer(&s, SOL_TIMER_HINT);
    sol_command(&s, SOL_CMD_UNDO);
    CHECK(!s.hint);
    /* the draw hint flashes the stock, then the waste */
    board(&s, T(0), "5H", -1);
    sol_command(&s, SOL_CMD_HINT);
    sol_hint_view(&s, &p, &c);
    CHECK(p == SOL_STOCK && c == s.board.p[SOL_STOCK].n - 1);
    for (int k = 0; k < 4; k++) sol_timer(&s, SOL_TIMER_HINT);
    sol_hint_view(&s, &p, &c);
    CHECK(p == SOL_WASTE && c == 0);
    sol_free(&s);
    /* nothing to suggest: the message, no flash. Vegas draw one, the one pass used: the aces buried,
     * 2C 2D 2H 2S on top, 5C on the waste */
    start(&s, &f, &r, 0x13, 28172);
    board_waste(&s, "5C", T(0), "#AC #AD #AH #AS 2C", T(1), "2D", T(2), "2H", T(3), "2S", -1);
    CHECK_EQ(sol_stock_symbol(&s), SOL_STOCK_X);
    SolHintMove none;
    CHECK(!sol_hint_find(&s.board, 0, &none));
    sol_command(&s, SOL_CMD_HINT);
    CHECK(!s.hint);
    CHECK_EQ(f.nmsg, 1);
    CHECK_EQ(f.last_msg, SOL_MSG_NO_HINT);
    CHECK_STR(f.last_msg_text, "No hint is available.");
    /* Standard: the waste can be turned over: that is the hint */
    s.opts.scoring = SOL_SCORING_STANDARD;
    sol_command(&s, SOL_CMD_HINT);
    CHECK(s.hint && s.hint_move.kind == SOL_HINT_RECYCLE);
    sol_free(&s);
    /* not dealt / dragging: no hint */
    start(&s, &f, &r, 0, -1);
    CHECK(!sol_hint_enabled(&s));
    sol_command(&s, SOL_CMD_HINT);
    CHECK(!s.hint && f.nmsg == 0);
    sol_free(&s);
}

/* ---- statistics --------------------------------------------------------------------------------------- */

static void test_stats(void)
{
    SolSession s;
    Fake f;
    Reg r;
    SolStats st;
    char buf[256];
    start(&s, &f, &r, 0x0B, 1);
    sol_attach_stats(&s, NULL);
    int m3 = sol_stats_mode(3, SOL_SCORING_STANDARD), m1v = sol_stats_mode(1, SOL_SCORING_VEGAS);
    CHECK_EQ(m3, 3);
    CHECK_EQ(m1v, 1);
    CHECK_EQ(sol_stats_mode(1, SOL_SCORING_NONE), 2);
    /* a deal alone is not played; a new deal then records nothing */
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.stats.m[m3].played, 0);
    CHECK_EQ(f.nstats, 0);
    /* the first action counts it (once) */
    sol_press(&s, SOL_STOCK, 0, 0);
    sol_press(&s, SOL_STOCK, 0, 0);
    CHECK_EQ(s.stats.m[m3].played, 1);
    CHECK_EQ(f.nstats, 1);
    /* a new deal: lost */
    s.score = 40;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.stats.m[m3].played, 1);
    CHECK_EQ(s.stats.m[m3].streak, -1);
    CHECK_EQ(s.stats.m[m3].loss_streak, 1);
    CHECK_EQ(s.stats.m[m3].best_score, 40);
    CHECK_EQ(f.nstats, 2);
    /* lost again (Undo back to the deal still counts as played) */
    sol_press(&s, SOL_STOCK, 0, 0);
    sol_undo(&s);
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.stats.m[m3].played, 2);
    CHECK_EQ(s.stats.m[m3].streak, -2);
    CHECK_EQ(s.stats.m[m3].loss_streak, 2);
    /* a win: timed, after 100 s */
    sol_press(&s, SOL_STOCK, 0, 0);
    s.ticks = 400;
    board_exact(&s, F(0), "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC QC", F(1), "AD 2D 3D 4D 5D 6D 7D 8D 9D TD JD QD KD",
                F(2), "AH 2H 3H 4H 5H 6H 7H 8H 9H TH JH QH KH", F(3), "AS 2S 3S 4S 5S 6S 7S 8S 9S TS JS QS KS", T(0), "KC", -1);
    s.score = 500;
    CHECK_EQ(sol_dblclick(&s, T(0), 0, 0), SOL_PRESS_DONE);
    CHECK(s.won);
    CHECK_EQ(s.stats.m[m3].played, 3);
    CHECK_EQ(s.stats.m[m3].won, 1);
    CHECK_EQ(s.stats.m[m3].streak, 1);
    CHECK_EQ(s.stats.m[m3].win_streak, 1);
    CHECK_EQ(s.stats.m[m3].best_time, 100);
    CHECK_EQ(s.stats.m[m3].best_score, 510 + 35 * 200);       /* +10, then the time bonus 35 * 20000 / 100 */
    /* the win is not followed by a loss at the next deal */
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.stats.m[m3].streak, 1);
    CHECK_EQ(s.stats.m[m3].played, 3);
    /* a second win in a row: streak 2; a slower time keeps the best */
    sol_press(&s, SOL_STOCK, 0, 0);
    s.ticks = 800;
    board_exact(&s, F(0), "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC QC", F(1), "AD 2D 3D 4D 5D 6D 7D 8D 9D TD JD QD KD",
                F(2), "AH 2H 3H 4H 5H 6H 7H 8H 9H TH JH QH KH", F(3), "AS 2S 3S 4S 5S 6S 7S 8S 9S TS JS QS KS", T(0), "KC", -1);
    s.score = 0;
    sol_dblclick(&s, T(0), 0, 0);
    CHECK_EQ(s.stats.m[m3].won, 2);
    CHECK_EQ(s.stats.m[m3].win_streak, 2);
    CHECK_EQ(s.stats.m[m3].best_time, 100);
    CHECK_EQ(s.stats.m[m3].loss_streak, 2);
    sol_stats_format(&s.stats.m[m3], SOL_SCORING_STANDARD, 0, buf, sizeof buf);
    CHECK_STR(buf, "4\n2\n50%\n2 wins\n2\n2\n1:40\n7510");
    /* the per-mode separation: Draw One Vegas (an Options change deals: the won game is not lost) */
    SolOptions o = s.opts;
    o.draw = 1;
    o.scoring = SOL_SCORING_VEGAS;
    o.cumulative = 1;
    sol_apply_options(&s, &o);
    CHECK_EQ(s.score, -52);
    sol_press(&s, SOL_STOCK, 0, 0);
    CHECK_EQ(s.stats.m[m1v].played, 1);
    CHECK_EQ(s.stats.m[m3].played, 4);
    s.score = -52 + 25;                                       /* five cards home */
    sol_command(&s, SOL_CMD_DEAL);                            /* Cumulative: the next deal bets on -27 */
    CHECK_EQ(s.stats.m[m1v].best_score, -27);
    CHECK_EQ(s.score, -79);
    sol_press(&s, SOL_STOCK, 0, 0);
    s.score = -79 + 50;                                       /* ten cards home this game: +$-2 */
    /* an Options redeal (to Draw Three Vegas) loses it in its own mode */
    o.draw = 3;
    sol_apply_options(&s, &o);
    CHECK_EQ(s.stats.m[m1v].played, 2);
    CHECK_EQ(s.stats.m[m1v].best_score, -2);                  /* the game's own result, not the total */
    CHECK_EQ(s.stats.m[m1v].streak, -2);
    CHECK_EQ(s.stats.m[sol_stats_mode(3, SOL_SCORING_VEGAS)].played, 0);
    sol_stats_format(&s.stats.m[m1v], SOL_SCORING_VEGAS, 0, buf, sizeof buf);
    CHECK_STR(buf, "2\n0\n0%\n2 losses\n0\n2\n-\n-$2");
    /* Exit without saving: lost */
    sol_press(&s, SOL_STOCK, 0, 0);
    sol_abandon(&s);
    CHECK_EQ(s.stats.m[sol_stats_mode(3, SOL_SCORING_VEGAS)].played, 1);
    CHECK_EQ(s.stats.m[sol_stats_mode(3, SOL_SCORING_VEGAS)].streak, -1);
    sol_abandon(&s);                                          /* once */
    CHECK_EQ(s.stats.m[sol_stats_mode(3, SOL_SCORING_VEGAS)].streak, -1);
    /* the forced win counts once the game was played */
    sol_command(&s, SOL_CMD_DEAL);
    sol_command(&s, SOL_CMD_FORCEWIN);
    CHECK_EQ(s.stats.m[sol_stats_mode(3, SOL_SCORING_VEGAS)].played, 1);   /* no action: not counted */
    st = s.stats;
    sol_free(&s);

    /* None scoring: no best score */
    start(&s, &f, &r, 0x2B, 1);
    sol_attach_stats(&s, &st);
    sol_press(&s, SOL_STOCK, 0, 0);
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.stats.m[5].played, 1);
    CHECK(!s.stats.m[5].has_score);
    sol_stats_format(&s.stats.m[5], SOL_SCORING_NONE, 0, buf, sizeof buf);
    CHECK_STR(buf, "1\n0\n0%\n1 loss\n0\n1\n-\n-");
    st = s.stats;
    sol_free(&s);

    /* percentages: rounded, 100% only when all were won */
    SolModeStats ms;
    memset(&ms, 0, sizeof ms);
    ms.played = 3; ms.won = 2;
    CHECK_EQ(sol_stats_percent(&ms), 67);
    ms.played = 200; ms.won = 199;
    CHECK_EQ(sol_stats_percent(&ms), 99);
    ms.won = 200;
    CHECK_EQ(sol_stats_percent(&ms), 100);

    /* the file: a round trip, damage rejected as a whole */
    Mem mem;
    memset(&mem, 0, sizeof mem);
    CeBlobIO io = mem_io(&mem);
    SolStats back;
    CHECK_EQ(sol_stats_load(&back, &io), 0);                  /* no file */
    CHECK(sol_stats_save(&st, &io));
    CHECK_EQ(mem.n, SOL_STATS_FILE_SIZE);
    CHECK_EQ(sol_stats_load(&back, &io), 1);
    CHECK(!memcmp(&back, &st, sizeof st));
    for (size_t k = 0; k < mem.n; k += 7) {
        mem.d[k] ^= 0x10;
        CHECK_EQ(sol_stats_load(&back, &io), -1);
        CHECK_EQ(back.m[3].played, 0);
        mem.d[k] ^= 0x10;
    }
    mem.n--;
    CHECK_EQ(sol_stats_load(&back, &io), -1);
    mem.n = 0;
    CHECK_EQ(sol_stats_load(&back, &io), 0);                  /* empty */
    mem_free(&mem);
}

/* Reset in the middle of a game that counts: the game starts over in the new statistics (it counts
 * again from its next action), so its result never lands without a game played. */
static void test_stats_reset(void)
{
    SolSession s;
    Fake f;
    Reg r;
    int m3 = sol_stats_mode(3, SOL_SCORING_STANDARD), n;
    start(&s, &f, &r, 0x09, 1);                               /* Draw Three, Standard, untimed */
    sol_attach_stats(&s, NULL);
    sol_press(&s, SOL_STOCK, 0, 0);
    CHECK_EQ(s.stats.m[m3].played, 1);
    n = f.nstats;
    sol_reset_stats(&s);
    CHECK_EQ(f.nstats, n + 1);                                /* written */
    CHECK_EQ(s.stats.m[m3].played, 0);
    /* left right away: nothing (not a loss of a game the statistics never saw) */
    sol_abandon(&s);
    CHECK_EQ(s.stats.m[m3].streak, 0);
    CHECK_EQ(s.stats.m[m3].loss_streak, 0);
    /* reset again, then won: played with it */
    sol_command(&s, SOL_CMD_DEAL);
    sol_press(&s, SOL_STOCK, 0, 0);
    sol_reset_stats(&s);
    board_exact(&s, F(0), "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC QC", F(1), "AD 2D 3D 4D 5D 6D 7D 8D 9D TD JD QD KD",
                F(2), "AH 2H 3H 4H 5H 6H 7H 8H 9H TH JH QH KH", F(3), "AS 2S 3S 4S 5S 6S 7S 8S 9S TS JS QS KS", T(0), "KC", -1);
    CHECK_EQ(sol_dblclick(&s, T(0), 0, 0), SOL_PRESS_DONE);
    CHECK(s.won);
    CHECK_EQ(s.stats.m[m3].played, 1);
    CHECK_EQ(s.stats.m[m3].won, 1);
    CHECK_EQ(s.stats.m[m3].streak, 1);
    sol_free(&s);
}

/* ---- save and resume ---------------------------------------------------------------------------------- */

static void play_some(SolSession *s, unsigned seed, int n)
{
    tsrand(seed);
    for (int k = 0; k < n && s->dealt; k++) {
        SolHintMove m;
        sol_hint_find(&s->board, s->board.p[SOL_STOCK].n == 0, &m);
        if (trand() % 5 == 0) sol_undo(s);
        else if (m.kind == SOL_HINT_MOVE) { sol_begin_drag(s, m.src, m.index); sol_drop(s, m.dst); }
        else if (m.kind == SOL_HINT_TURN) sol_press(s, m.src, m.index, 0);
        else sol_press(s, SOL_STOCK, 0, 0);
        for (int t = trand() % 9; t > 0; t--) sol_timer(s, SOL_TIMER_CLOCK);
    }
}

static int same_game(const SolSession *a, const SolSession *b)
{
    int ok = sol_board_equal(&a->board, &b->board) && a->waste_fan == b->waste_fan && a->score == b->score &&
             a->ticks == b->ticks && a->recycles == b->recycles && a->clock_pen == b->clock_pen &&
             a->draw == b->draw && a->game_scoring == b->game_scoring && a->seed == b->seed && a->rng == b->rng &&
             a->nhist == b->nhist && a->nredo == b->nredo && a->undo_fresh == b->undo_fresh &&
             a->counted == b->counted && a->carry == b->carry && a->dealt && b->dealt;
    for (int i = 0; ok && i < a->nhist; i++) {
        const SolAction *x = &a->hist[i], *y = &b->hist[i];
        ok = x->type == y->type && x->src == y->src && x->dst == y->dst && x->n == y->n && x->nsteps == y->nsteps &&
             x->autoturn == y->autoturn && !memcmp(x->steps, y->steps, 2u * x->nsteps) &&
             !memcmp(x->board, y->board, SOL_PACKED_SIZE) && x->waste_fan == y->waste_fan && x->score == y->score &&
             x->recycles == y->recycles && x->clock_pen == y->clock_pen && x->nauto == y->nauto &&
             !memcmp(x->autos, y->autos, x->nauto);
    }
    for (int i = 0; ok && i < a->nredo; i++)
        ok = !memcmp(a->redo[i].board, b->redo[i].board, SOL_PACKED_SIZE) && a->redo[i].type == b->redo[i].type &&
             a->redo[i].nauto == b->redo[i].nauto && !memcmp(a->redo[i].autos, b->redo[i].autos, a->redo[i].nauto);
    return ok && a->redo_group == b->redo_group;
}

static void test_save_resume(void)
{
    static const uint32_t modes[] = { 0x0B, 0x03, 0x1B, 0x53, 0x2B, 0x09 };
    for (int mi = 0; mi < 6; mi++) {
        SolSession a, b;
        Fake fa, fb;
        Reg ra, rb;
        Mem mem;
        memset(&mem, 0, sizeof mem);
        CeBlobIO io = mem_io(&mem);
        start(&a, &fa, &ra, modes[mi], 100 + mi);
        sol_attach_stats(&a, NULL);
        extras_on(&a, mi & 1, 0, 0);
        a.extras.auto_home = mi >= 2;                         /* v1.2: the auto-home sequences are saved too */
        play_some(&a, 7u + (unsigned)mi, 120);
        if (!a.dealt) sol_command(&a, SOL_CMD_DEAL), play_some(&a, 8, 30);
        if (mi == 4) {
            sol_undo_all(&a);                                 /* an Undo All group to save */
            CHECK(a.redo_group > 0);
        } else {
            sol_undo(&a);
            sol_undo(&a);                                     /* something to redo */
        }
        CHECK(sol_game_save(&a, &io));
        start(&b, &fb, &rb, modes[mi], -1);                   /* a fresh start, nothing dealt */
        extras_on(&b, mi & 1, 0, 0);                          /* the settings are the registry's, not the file's */
        b.extras.auto_home = mi >= 2;
        int inval = fb.ninval;
        CHECK_EQ(sol_game_load(&b, &io), SOL_LOAD_OK);
        CHECK(same_game(&a, &b));
        CHECK(fb.ninval > inval);
        CHECK(!b.input && !b.timer_on);                       /* the clock waits for the first press */
        CHECK_EQ(b.kbd_pile, SOL_STOCK);
        /* the same future: Undo all, Redo all, and the same moves */
        while (sol_undo(&a)) {}
        while (sol_undo(&b)) {}
        CHECK(sol_board_equal(&a.board, &b.board) && a.score == b.score);
        SolBoard d;
        sol_deal_board(&d, a.seed, NULL);
        CHECK(sol_board_equal(&b.board, &d));                 /* the history goes back to the deal */
        while (sol_redo(&a)) {}
        while (sol_redo(&b)) {}
        CHECK(sol_board_equal(&a.board, &b.board) && a.score == b.score && a.recycles == b.recycles);
        play_some(&a, 50, 40);
        play_some(&b, 50, 40);
        if (!(sol_board_equal(&a.board, &b.board) && a.score == b.score))
            printf("  mode %d: a: dealt %d score %d ticks %d input %d hist %d; b: dealt %d score %d ticks %d input %d hist %d\n",
                   mi, a.dealt, a.score, a.ticks, a.input, a.nhist, b.dealt, b.score, b.ticks, b.input, b.nhist);
        CHECK(sol_board_equal(&a.board, &b.board) && a.score == b.score);

        /* damage: every bit flip, truncation, a foreign file */
        uint8_t *img;
        size_t len;
        CHECK(sol_game_serialize(&a, &img, &len));
        int rejected = 0, flips = 0;
        for (size_t k = 0; k < len; k += (len / 97) + 1, flips++) {
            img[k] ^= 0x04;
            SolBoard keep = b.board;
            if (sol_game_restore(&b, img, len) == SOL_LOAD_DAMAGED && sol_board_equal(&b.board, &keep)) rejected++;
            img[k] ^= 0x04;
        }
        CHECK_EQ(rejected, flips);
        CHECK_EQ(sol_game_restore(&b, img, len - 1), SOL_LOAD_DAMAGED);
        CHECK_EQ(sol_game_restore(&b, img, 10), SOL_LOAD_DAMAGED);
        CHECK_EQ(sol_game_restore(&b, (const uint8_t *)"FCWD", 4), SOL_LOAD_DAMAGED);
        CHECK_EQ(sol_game_restore(&b, img, len), SOL_LOAD_OK);
        /* numbers no game can reach, with a matching CRC (an edited file): rejected before any of
         * them can overflow the score arithmetic (Undo subtracts the clock's penalties) */
        {
            static const struct { size_t off; uint32_t v; } bad[] = {
                { 12 + 4 + 6 + SOL_PACKED_SIZE, 0x7FFFFFF0u },           /* score */
                { 12 + 4 + 6 + SOL_PACKED_SIZE + 12, 0x80000000u },      /* clock_pen */
                { 12 + 4 + 6 + SOL_PACKED_SIZE + 12, 0xFFFFFFFEu },      /* clock_pen -2 */
                { 12 + 4 + 6 + SOL_PACKED_SIZE + 16, 0x80000001u },      /* carry */
            };
            for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
                uint8_t *e = malloc(len);
                uint32_t crc;
                memcpy(e, img, len);
                for (int q = 0; q < 4; q++) e[bad[k].off + (size_t)q] = (uint8_t)(bad[k].v >> (8 * q));
                crc = ce_crc32(e + 12, len - 16);
                for (int q = 0; q < 4; q++) e[len - 4 + (size_t)q] = (uint8_t)(crc >> (8 * q));
                CHECK_EQ(sol_game_restore(&b, e, len), SOL_LOAD_DAMAGED);
                free(e);
            }
        }
        free(img);
        sol_free(&a);
        sol_free(&b);
        mem_free(&mem);
    }

    /* other Options: ignored; no game: the empty file; empty file: none */
    SolSession a, b;
    Fake fa, fb;
    Reg ra, rb;
    Mem mem;
    memset(&mem, 0, sizeof mem);
    CeBlobIO io = mem_io(&mem);
    start(&a, &fa, &ra, 0x0B, 5);
    sol_press(&a, SOL_STOCK, 0, 0);
    CHECK(sol_game_save(&a, &io));
    CHECK(mem.n > 0);
    start(&b, &fb, &rb, 0x03, -1);                            /* Draw One now */
    CHECK_EQ(sol_game_load(&b, &io), SOL_LOAD_OTHER_OPTIONS);
    CHECK(!b.dealt);
    sol_free(&b);
    start(&b, &fb, &rb, 0x09, -1);                            /* untimed now */
    CHECK_EQ(sol_game_load(&b, &io), SOL_LOAD_OTHER_OPTIONS);
    sol_free(&b);
    start(&b, &fb, &rb, 0x0F, -1);                            /* outline dragging: fine */
    CHECK_EQ(sol_game_load(&b, &io), SOL_LOAD_OK);
    sol_free(&b);
    sol_command(&a, SOL_CMD_FORCEWIN);                        /* won: nothing to save */
    CHECK(sol_game_save(&a, &io));
    CHECK_EQ(mem.n, 0);
    start(&b, &fb, &rb, 0x0B, -1);
    CHECK_EQ(sol_game_load(&b, &io), SOL_LOAD_NONE);
    CHECK(!b.dealt);
    mem.exists = 0;
    CHECK_EQ(sol_game_load(&b, &io), SOL_LOAD_NONE);
    sol_free(&a);
    sol_free(&b);
    mem_free(&mem);

    /* the statistics continue: a resumed counted game is lost or won once */
    start(&a, &fa, &ra, 0x0B, 9);
    sol_attach_stats(&a, NULL);
    sol_press(&a, SOL_STOCK, 0, 0);
    CHECK(sol_game_save(&a, &io));
    SolStats st = a.stats;
    start(&b, &fb, &rb, 0x0B, -1);
    sol_attach_stats(&b, &st);
    CHECK_EQ(sol_game_load(&b, &io), SOL_LOAD_OK);
    CHECK(b.counted);
    sol_press(&b, SOL_STOCK, 0, 0);
    CHECK_EQ(b.stats.m[3].played, 1);                         /* not counted twice */
    sol_command(&b, SOL_CMD_DEAL);
    CHECK_EQ(b.stats.m[3].streak, -1);
    sol_free(&a);
    sol_free(&b);
    mem_free(&mem);
}

/* ---- winnable deals only ---------------------------------------------------------------------------- */

static void test_winnable(void)
{
    SolSession s;
    Fake f;
    Reg r;
    static const uint32_t modes[4] = { 0x03, 0x0B, 0x13, 0x1B };   /* draw 1, draw 3, Vegas 1, Vegas 3 */
    for (int c = 0; c < 4; c++) {
        start(&s, &f, &r, modes[c], -1);
        SolExtras x = s.extras;
        x.winnable_only = 1;
        sol_set_extras(&s, &x);
        int scase = sol_seeds_case(s.opts.draw, s.opts.scoring), moved = 0;
        CHECK_EQ(scase, c == 0 ? SOL_SEEDS_DRAW1 : c == 1 ? SOL_SEEDS_DRAW3 : c == 2 ? SOL_SEEDS_DRAW1_VEGAS : SOL_SEEDS_DRAW3_VEGAS);
        for (unsigned t = 0; t < 300; t++) {
            f.now = 1600000000u + t * 7919u;
            sol_command(&s, SOL_CMD_DEAL);
            unsigned want = sol_seed_next_winnable(scase, f.now & 0x7FFF);
            CHECK_EQ(sol_seed_status(scase, s.seed), SOL_SEED_WINNABLE);
            if (s.seed != want) CHECK_EQ(s.seed, sol_seed_next_winnable(scase, want + 1));   /* the repeat rule */
            if (s.seed != (f.now & 0x7FFF)) moved++;
        }
        /* the same second twice: never the same deal */
        unsigned first = s.seed;
        sol_command(&s, SOL_CMD_DEAL);
        CHECK(s.seed != first);
        CHECK_EQ(sol_seed_status(scase, s.seed), SOL_SEED_WINNABLE);
        printf("  winnable only, case %d: %d of 300 deals moved to a later seed\n", scase, moved);
        sol_free(&s);
    }
    /* off: XP's seed */
    start(&s, &f, &r, 0x13, -1);
    f.now = 4;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.seed, 4u);
    sol_free(&s);
    /* a Draw One Vegas deal it picks is won by the solver (a sample) */
    start(&s, &f, &r, 0x13, -1);
    SolExtras x = s.extras;
    x.winnable_only = 1;
    sol_set_extras(&s, &x);
    f.now = 12345;
    sol_command(&s, SOL_CMD_DEAL);
    SolSolver *sv = sol_solver_new(0);
    SolSolveResult res;
    if (sv) {
        int st = sol_solve(sv, &s.board, 1, sol_solve_recycles_left(SOL_SCORING_VEGAS, 1, 0), NULL, &res);
        CHECK(st == SOL_SOLVE_SOLVED || st == SOL_SOLVE_GAVE_UP);   /* never proven unwinnable */
        sol_solver_free(sv);
    }
    sol_free(&s);
}

/* ---- the unwinnable warning ----------------------------------------------------------------------------- */

static void test_warning(void)
{
    SolSession s;
    Fake f;
    Reg r;
    start(&s, &f, &r, 0x13, 3);
    CHECK_EQ(f.nsolve, 0);                                    /* off: no searches */
    sol_press(&s, SOL_STOCK, 0, 0);
    CHECK_EQ(f.nsolve, 0);
    SolExtras x = s.extras;
    x.warn_unwinnable = 1;
    sol_set_extras(&s, &x);
    CHECK_EQ(f.nsolve, 1);                                    /* turned on: the position is checked */
    CHECK(sol_board_equal(&f.solve_board, &s.board));
    CHECK_EQ(f.solve_draw, 1);
    CHECK_EQ(f.solve_left, 0);                                /* Vegas draw one: no recycle */
    CHECK_EQ(sol_solve_done(&s, f.solve_id, SOL_SOLVE_UNSOLVABLE), 1);
    CHECK_EQ(f.nmsg, 0);                                      /* silent */
    sol_press(&s, SOL_STOCK, 0, 0);                           /* an action: checked */
    CHECK_EQ(f.nsolve, 2);
    uint32_t id = f.solve_id;
    sol_press(&s, SOL_STOCK, 0, 0);                           /* another one replaces it */
    CHECK_EQ(f.nsolve, 3);
    CHECK_EQ(sol_solve_done(&s, id, SOL_SOLVE_UNSOLVABLE), 1);   /* stale: ignored */
    CHECK_EQ(f.nmsg, 0);
    s.busy = 1;
    CHECK_EQ(sol_solve_done(&s, f.solve_id, SOL_SOLVE_UNSOLVABLE), 0);   /* busy: later */
    s.busy = 0;
    /* cards being dragged: later too (a message box now would leave the drag hanging); the drag
     * changes nothing, so the answer still holds once it is cancelled */
    {
        int t = T(0);
        while (!s.board.p[t].n) t++;
        CHECK(sol_begin_drag(&s, t, s.board.p[t].n - 1));
        CHECK_EQ(sol_solve_done(&s, f.solve_id, SOL_SOLVE_UNSOLVABLE), 0);
        CHECK_EQ(f.nmsg, 0);
        sol_cancel_drag(&s);
    }
    CHECK_EQ(sol_solve_done(&s, f.solve_id, SOL_SOLVE_UNSOLVABLE), 1);
    CHECK_EQ(f.nmsg, 1);
    CHECK_EQ(f.last_msg, SOL_MSG_UNWINNABLE);
    CHECK_STR(f.last_msg_text, "This game can no longer be won. Use Undo to go back.");
    sol_press(&s, SOL_STOCK, 0, 0);
    sol_solve_done(&s, f.solve_id, SOL_SOLVE_UNSOLVABLE);
    CHECK_EQ(f.nmsg, 1);                                      /* once */
    sol_undo(&s);
    sol_solve_done(&s, f.solve_id, SOL_SOLVE_SOLVED);         /* winnable again: re-armed */
    sol_redo(&s);
    sol_solve_done(&s, f.solve_id, SOL_SOLVE_UNSOLVABLE);
    CHECK_EQ(f.nmsg, 2);
    sol_press(&s, SOL_STOCK, 0, 0);
    sol_solve_done(&s, f.solve_id, SOL_SOLVE_GAVE_UP);        /* nothing known */
    CHECK_EQ(f.nmsg, 2);
    /* a deal proven unwinnable: the first action says so */
    sol_command(&s, SOL_CMD_DEAL);
    sol_solve_done(&s, f.solve_id, SOL_SOLVE_UNSOLVABLE);
    CHECK_EQ(f.nmsg, 2);
    sol_press(&s, SOL_STOCK, 0, 0);
    sol_solve_done(&s, f.solve_id, SOL_SOLVE_UNSOLVABLE);
    CHECK_EQ(f.nmsg, 3);
    CHECK_EQ(f.last_msg, SOL_MSG_UNWINNABLE_DEAL);
    /* turned off: the pending search is cancelled */
    sol_press(&s, SOL_STOCK, 0, 0);
    int cancels = f.ncancel;
    x.warn_unwinnable = 0;
    sol_set_extras(&s, &x);
    CHECK_EQ(f.ncancel, cancels + 1);
    sol_press(&s, SOL_STOCK, 0, 0);
    CHECK_EQ(f.nsolve, 10);
    sol_free(&s);

    /* the real solver on a Vegas draw-one deal the table proves unwinnable: the first draw warns */
    start(&s, &f, &r, 0x13, -1);
    unsigned seed = 0;
    while (sol_seed_status(SOL_SEEDS_DRAW1_VEGAS, seed) != SOL_SEED_UNSOLVABLE) seed++;
    sol_deal(&s, seed, 0);
    x = s.extras;
    x.warn_unwinnable = 1;
    sol_set_extras(&s, &x);
    SolSolver *sv = sol_solver_new(0);
    SolSolveResult res;
    if (sv) {
        CHECK_EQ(f.nsolve, 1);                                /* turned on before any action: the deal */
        sol_solve(sv, &f.solve_board, f.solve_draw, f.solve_left, NULL, &res);
        CHECK_EQ(res.status, SOL_SOLVE_UNSOLVABLE);
        sol_solve_done(&s, f.solve_id, res.status);
        CHECK_EQ(f.nmsg, 0);                                  /* silent */
        sol_press(&s, SOL_STOCK, 0, 0);
        sol_solve(sv, &f.solve_board, f.solve_draw, f.solve_left, NULL, &res);
        CHECK_EQ(res.status, SOL_SOLVE_UNSOLVABLE);
        sol_solve_done(&s, f.solve_id, res.status);
        CHECK_EQ(f.nmsg, 1);
        CHECK_EQ(f.last_msg, SOL_MSG_UNWINNABLE_DEAL);
        sol_solver_free(sv);
    }
    sol_free(&s);
}


/* ---- v1.2: move cards home automatically ------------------------------------------------------------ */

static void extra_set(SolSession *s, int *field, int on)
{
    SolExtras x = s->extras;
    *(int *)((char *)&x + ((char *)field - (char *)&s->extras)) = on;
    sol_set_extras(s, &x);
}

static void test_auto_home(void)
{
    SolSession s;
    Fake f;
    Reg r;
    /* the rule: aces and twos always; a higher card once both foundations of the other colour hold its
     * rank - 1 */
    {
        SolBoard b;
        int src, dst;
        sol_board_clear(&b);
        set_pile(&b, SOL_WASTE, "3H");
        set_pile(&b, F(0), "AH 2H");
        set_pile(&b, F(1), "AC 2C");
        set_pile(&b, F(2), "AS");
        CHECK(!sol_auto_home_step(&b, &src, &dst));          /* 3H: spades hold only the ace */
        set_pile(&b, F(2), "AS 2S");
        CHECK(sol_auto_home_step(&b, &src, &dst));
        CHECK(src == SOL_WASTE && dst == F(0));
        set_pile(&b, SOL_WASTE, "");
        set_pile(&b, T(2), "#5D 2D");                        /* a two: its ace is not home yet */
        CHECK(!sol_auto_home_step(&b, &src, &dst));
        set_pile(&b, T(1), "#4D AD");                        /* an ace: always, to the leftmost free foundation */
        CHECK(sol_auto_home_step(&b, &src, &dst));
        CHECK(src == T(1) && dst == F(3));
        set_pile(&b, T(1), "#4D");                           /* face down: never */
        CHECK(!sol_auto_home_step(&b, &src, &dst));
        set_pile(&b, F(3), "AD");
        CHECK(sol_auto_home_step(&b, &src, &dst));           /* now 2D */
        CHECK(src == T(2) && dst == F(3));
        set_pile(&b, T(2), "#5D 3C");                        /* 3C: hearts AH 2H, diamonds AD: no */
        CHECK(!sol_auto_home_step(&b, &src, &dst));
        set_pile(&b, F(3), "AD 2D");
        CHECK(sol_auto_home_step(&b, &src, &dst));
        CHECK(src == T(2) && dst == F(1));
    }

    /* a draw starts a cascade: 2C, then (turned over) 2S, then 3D, each flown; the turns in between;
     * one action, scored as XP's foundation moves and turns */
    start(&s, &f, &r, 0x01, 1);                               /* Draw One, Standard, untimed */
    board(&s, SOL_STOCK, "#QS", F(0), "AC", F(1), "AD 2D", F(2), "AS", T(0), "#9C 2C", T(1), "#8S 2S",
          T(2), "KH 3D", T(3), "#7H 4H", T(4), "5S", -1);
    s.score = 0;
    SolBoard before = s.board;
    sol_press(&s, SOL_STOCK, s.board.p[SOL_STOCK].n - 1, 0);
    CHECK(pile_is(&s.board, SOL_WASTE, "QS"));
    CHECK(pile_is(&s.board, F(0), "AC"));                     /* off: nothing moves by itself */
    CHECK_EQ(f.nanim, 0);
    sol_undo(&s);
    CHECK(sol_board_equal(&s.board, &before));
    s.score = 0;
    extras_on(&s, 1, 0, 0);
    extra_set(&s, &s.extras.auto_home, 1);
    s.nhist = s.nredo = 0;
    sol_press(&s, SOL_STOCK, s.board.p[SOL_STOCK].n - 1, 0);
    CHECK(pile_is(&s.board, F(0), "AC 2C"));
    CHECK(pile_is(&s.board, F(2), "AS 2S"));
    CHECK(pile_is(&s.board, F(1), "AD 2D 3D"));
    CHECK(pile_is(&s.board, T(0), "9C"));
    CHECK(pile_is(&s.board, T(1), "8S"));
    CHECK(pile_is(&s.board, T(2), "KH"));
    CHECK(pile_is(&s.board, T(3), "#7H 4H"));                 /* no hearts home */
    CHECK_EQ(f.nanim, 3);
    CHECK(f.anim[0][0] == T(0) && f.anim[0][1] == F(0));
    CHECK(f.anim[1][0] == T(1) && f.anim[1][1] == F(2));
    CHECK(f.anim[2][0] == T(2) && f.anim[2][1] == F(1));
    CHECK_EQ(s.score, 3 * 10 + 2 * 5);
    CHECK_EQ(s.nhist, 1);
    CHECK_EQ(s.hist[0].type, SOL_ACT_DRAW);
    CHECK_EQ(s.hist[0].nauto, 5);
    CHECK_EQ(s.hist[0].autos[1], SOL_AUTO_TURN | T(0));
    SolBoard after = s.board;
    /* one Undo step back; nothing moves after the Undo; Redo replays it all (not flown) */
    CHECK(sol_undo(&s));
    CHECK(sol_board_equal(&s.board, &before));
    CHECK_EQ(f.nanim, 3);
    CHECK(sol_redo(&s));
    CHECK(sol_board_equal(&s.board, &after));
    CHECK_EQ(f.nanim, 3);
    CHECK_EQ(s.score, 3 * 10 + 2 * 5);                        /* (the Undo's -2 was floored at 0) */
    sol_free(&s);

    /* after a drop, a double-click, a turn and a right-click autoplay too; Vegas +5 a card */
    start(&s, &f, &r, 0x11, 1);                               /* Draw One, Vegas, untimed */
    board(&s, F(0), "AH", T(0), "#9C 2H", T(1), "3S", T(2), "#4C 4D", T(3), "#5C AS", -1);
    extras_on(&s, 0, 0, 0);
    extra_set(&s, &s.extras.auto_home, 1);
    int base = s.score;
    CHECK(sol_begin_drag(&s, T(0), 1));
    CHECK(sol_drop(&s, T(1)));                                /* 2H onto 3S: 2H could go home: it does */
    CHECK(pile_is(&s.board, F(0), "AH 2H"));
    CHECK(pile_is(&s.board, T(3), "#5C"));                    /* AS (an ace) too */
    CHECK(pile_is(&s.board, F(1), "AS"));
    CHECK_EQ(s.score, base + 2 * 5);
    CHECK(pile_is(&s.board, T(0), "#9C"));                    /* no auto-turn */
    CHECK_EQ(sol_press(&s, T(0), 0, 0), SOL_PRESS_DONE);      /* the turn: 9C, nothing for home */
    CHECK_EQ(s.nhist, 2);
    sol_free(&s);

    /* the win: the last card drawn goes home by itself */
    start(&s, &f, &r, 0x01, 1);
    board_exact(&s, SOL_STOCK, "#KC", F(0), "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC QC",
                F(1), "AD 2D 3D 4D 5D 6D 7D 8D 9D TD JD QD KD", F(2), "AH 2H 3H 4H 5H 6H 7H 8H 9H TH JH QH KH",
                F(3), "AS 2S 3S 4S 5S 6S 7S 8S 9S TS JS QS KS", -1);
    extra_set(&s, &s.extras.auto_home, 1);
    CHECK_EQ(sol_press(&s, SOL_STOCK, 0, 0), SOL_PRESS_DONE);
    CHECK(s.won);
    CHECK_EQ(f.ncascade, 1);
    CHECK_EQ(f.nanim, 1);
    sol_free(&s);

    /* a safe card taken down from a foundation goes straight back (it could hold no card in play) */
    start(&s, &f, &r, 0x01, 1);
    board(&s, F(0), "AH 2H 3H", F(1), "AC 2C", F(2), "AS 2S", T(0), "#9C 4S", -1);
    extra_set(&s, &s.extras.auto_home, 1);
    s.score = 50;
    CHECK(sol_begin_drag(&s, F(0), 2));
    CHECK(sol_drop(&s, T(0)));                                /* 3H onto 4S: -15, then home again: +10 */
    CHECK(pile_is(&s.board, F(0), "AH 2H 3H"));
    CHECK(pile_is(&s.board, T(0), "#9C 4S"));
    CHECK_EQ(s.score, 45);
    CHECK_EQ(s.nhist, 1);
    sol_free(&s);
}

/* ---- v1.2: click to select, click to move ------------------------------------------------------------ */

static int answer_yes = 1, nconfirm;
static int f_confirm(void *ctx, int id, const char *text)
{
    nconfirm++;
    CHECK_EQ(id, SOL_MSG_UNDO_ALL);
    CHECK_STR(text, "Do you want to undo all your moves and return to the start of the game?");
    return answer_yes;
}

static void test_click_select(void)
{
    SolSession s;
    Fake f;
    Reg r;
    int p, i;
    start(&s, &f, &r, 0x01, 1);
    board(&s, SOL_STOCK, "#9H", SOL_WASTE, "QH 2D", T(0), "#3C 6H 5S", T(1), "", T(3), "#4C 7C", T(4), "8D",
          F(0), "AD", -1);
    /* off: a click selects nothing */
    CHECK_EQ(sol_press(&s, T(0), 1, 0), SOL_PRESS_DRAG);
    CHECK_EQ(sol_click(&s), SOL_CLICK_NONE);
    CHECK(sol_dragging(&s));
    sol_cancel_drag(&s);
    sol_selection(&s, &p, &i);
    CHECK_EQ(p, -1);
    extra_set(&s, &s.extras.click_select, 1);
    /* a click selects 6H 5S (inverted); a press on 7C moves them there (one action, as a drop) */
    CHECK_EQ(sol_press(&s, T(0), 1, 0), SOL_PRESS_DRAG);
    int inval = f.ninval;
    CHECK_EQ(sol_click(&s), SOL_CLICK_SELECTED);
    CHECK(!sol_dragging(&s));
    CHECK(f.ninval > inval);
    sol_selection(&s, &p, &i);
    CHECK(p == T(0) && i == 1);
    CHECK_EQ(s.nhist, 0);
    CHECK_EQ(sol_press(&s, T(3), 1, 0), SOL_PRESS_DONE);
    CHECK(pile_is(&s.board, T(3), "#4C 7C 6H 5S"));
    CHECK(pile_is(&s.board, T(0), "#3C"));
    CHECK_EQ(s.nhist, 1);
    sol_selection(&s, &p, &i);
    CHECK_EQ(p, -1);
    CHECK(sol_undo(&s));
    /* a press on the selection's own pile only deselects: its click selects nothing */
    sol_press(&s, T(0), 1, 0);
    sol_click(&s);
    CHECK_EQ(sol_press(&s, T(0), 2, 0), SOL_PRESS_DRAG);
    sol_selection(&s, &p, &i);
    CHECK_EQ(p, -1);
    CHECK_EQ(sol_click(&s), SOL_CLICK_NONE);
    CHECK(sol_dragging(&s));
    CHECK(!sol_drop(&s, s.target));                           /* the UI drops on no target: back */
    sol_selection(&s, &p, &i);
    CHECK_EQ(p, -1);
    /* a press on a pile that does not take them: deselected, and the press acts as usual */
    sol_press(&s, T(0), 2, 0);
    sol_click(&s);                                            /* 5S selected */
    CHECK_EQ(sol_press(&s, T(4), 0, 0), SOL_PRESS_DRAG);      /* 8D: not for 5S; picked up instead */
    sol_selection(&s, &p, &i);
    CHECK_EQ(p, -1);
    CHECK_EQ(s.drag_pile, T(4));
    CHECK_EQ(sol_click(&s), SOL_CLICK_SELECTED);              /* and now 8D is selected */
    sol_selection(&s, &p, &i);
    CHECK(p == T(4) && i == 0);
    CHECK_EQ(sol_press(&s, T(1), -1, 0), SOL_PRESS_NONE);     /* an empty column: only a king; deselected */
    sol_selection(&s, &p, &i);
    CHECK_EQ(p, -1);
    /* the waste's card to its foundation, scored */
    s.score = 0;
    sol_press(&s, SOL_WASTE, 1, 0);
    sol_click(&s);
    CHECK_EQ(sol_press(&s, F(0), 0, 0), SOL_PRESS_DONE);
    CHECK(pile_is(&s.board, F(0), "AD 2D"));
    CHECK_EQ(s.score, 10);
    /* the stock: deselects and draws */
    sol_press(&s, T(4), 0, 0);
    sol_click(&s);
    int h = s.nhist;
    CHECK_EQ(sol_press(&s, SOL_STOCK, s.board.p[SOL_STOCK].n - 1, 0), SOL_PRESS_DONE);
    CHECK_EQ(s.nhist, h + 1);
    CHECK(pile_is(&s.board, SOL_WASTE, "QH 9H"));
    sol_selection(&s, &p, &i);
    CHECK_EQ(p, -1);
    /* a foundation's card can be selected and put back on the tableau */
    sol_press(&s, F(0), 1, 0);
    CHECK_EQ(sol_click(&s), SOL_CLICK_SELECTED);
    CHECK_EQ(sol_press(&s, T(3), 1, 0), SOL_PRESS_DRAG);      /* 7C does not take 2D: deselected; 7C picked up */
    sol_cancel_drag(&s);
    /* keys, commands, autoplay and turning the option off end a selection */
    for (int k = 0; k < 4; k++) {
        sol_press(&s, T(0), 1, 0);
        CHECK_EQ(sol_click(&s), SOL_CLICK_SELECTED);
        if (k == 0) sol_key(&s, SOL_KEY_LEFT, 0);
        else if (k == 1) sol_command(&s, SOL_CMD_UNDO);
        else if (k == 2) sol_autoplay(&s);
        else extra_set(&s, &s.extras.click_select, 0);
        sol_selection(&s, &p, &i);
        CHECK_EQ(p, -1);
    }
    extra_set(&s, &s.extras.click_select, 1);
    /* the double-click: XP's (home first), the selection does not stand in the way */
    board(&s, SOL_WASTE, "2D", T(0), "#3C 6H 5S", F(0), "AD", -1);
    sol_press(&s, SOL_WASTE, 0, 0);
    sol_click(&s);
    CHECK_EQ(sol_dblclick(&s, SOL_WASTE, 0, 0), SOL_PRESS_DONE);
    CHECK(pile_is(&s.board, F(0), "AD 2D"));
    sol_selection(&s, &p, &i);
    CHECK_EQ(p, -1);
    /* with "Single click moves a card" too: a click moves when there is a place, selects when not */
    extras_on(&s, 0, 1, 0);
    board(&s, SOL_WASTE, "2D", T(0), "#3C 6H 5S", T(3), "#4C 7C", F(0), "AD", -1);
    sol_press(&s, SOL_WASTE, 0, 0);
    CHECK_EQ(sol_click(&s), SOL_CLICK_MOVED);                 /* 2D home */
    sol_press(&s, T(0), 2, 0);
    CHECK_EQ(sol_click(&s), SOL_CLICK_SELECTED);              /* 5S: nowhere to go */
    CHECK_EQ(sol_press(&s, T(0), 1, 0), SOL_PRESS_DRAG);      /* its own pile: deselects ... */
    CHECK_EQ(sol_click(&s), SOL_CLICK_NONE);                  /* ... and the click does not move 6H */
    sol_drop(&s, -1);
    CHECK(pile_is(&s.board, T(0), "#3C 6H 5S"));
    sol_free(&s);
}

/* ---- v1.2: Undo All, Draw (D) ---------------------------------------------------------------------- */

static void test_undo_all(void)
{
    SolSession s, t;
    Fake f, ft;
    Reg r, rt;
    static const uint32_t modes[3] = { 0x0B, 0x13, 0x2B };
    for (int m = 0; m < 3; m++) {
        start(&s, &f, &r, modes[m], 200 + m);
        start(&t, &ft, &rt, modes[m], 200 + m);
        s.ui.confirm = f_confirm;
        play_some(&s, 31u + (unsigned)m, 60);
        play_some(&t, 31u + (unsigned)m, 60);
        for (int k = 0; k < 9; k++) sol_timer(&s, SOL_TIMER_CLOCK), sol_timer(&t, SOL_TIMER_CLOCK);
        if (!s.dealt || s.nhist < 3) {
            printf("  undo all: game %d ended early\n", m);
            fails++;
            continue;
        }
        sol_undo(&s);
        sol_undo(&t);                                         /* an action already on the redo stack */
        SolBoard end = s.board;
        int n = s.nhist, end_score = s.score, r0 = s.nredo;
        /* No: nothing changes */
        answer_yes = 0;
        nconfirm = 0;
        sol_command(&s, SOL_CMD_UNDO_ALL);
        CHECK_EQ(nconfirm, 1);
        CHECK_EQ(s.nhist, n);
        CHECK(sol_board_equal(&s.board, &end));
        /* Yes: back to the deal, the score as after that many Undos */
        answer_yes = 1;
        int solve = f.nsolve;
        sol_command(&s, SOL_CMD_UNDO_ALL);
        CHECK_EQ(nconfirm, 2);
        CHECK_EQ(s.nhist, 0);
        CHECK_EQ(s.nredo, n + r0);
        CHECK_EQ(s.redo_group, n);
        SolBoard d;
        sol_deal_board(&d, s.seed, NULL);
        CHECK(sol_board_equal(&s.board, &d));
        while (sol_undo(&t)) {}
        CHECK_EQ(s.score, t.score);
        CHECK(f.nsolve <= solve + 1);                         /* (no warning: no search at all) */
        CHECK(!sol_undo_enabled(&s));
        sol_command(&s, SOL_CMD_UNDO_ALL);                    /* nothing to undo: no question */
        CHECK_EQ(nconfirm, 2);
        /* one Redo: everything back */
        CHECK(sol_redo(&s));
        CHECK(sol_board_equal(&s.board, &end));
        CHECK_EQ(s.nhist, n);
        CHECK_EQ(s.nredo, r0);
        CHECK_EQ(s.redo_group, 0);
        for (int k = 0; k < n; k++) sol_redo(&t);
        CHECK_EQ(s.score, t.score);
        if (m == 1) CHECK_EQ(s.score, end_score);              /* Vegas: Undo costs nothing */
        CHECK(sol_redo(&s));                                  /* then the single action, alone */
        CHECK_EQ(s.nredo, r0 - 1);
        /* Undo All, then a new action: the group is gone */
        sol_undo_all(&s);
        CHECK(s.redo_group > 0);
        sol_press(&s, SOL_STOCK, 0, 0);
        CHECK_EQ(s.redo_group, 0);
        CHECK_EQ(s.nredo, 0);
        /* never while dragging */
        int nh = s.nhist;
        CHECK(sol_begin_drag(&s, SOL_WASTE, s.board.p[SOL_WASTE].n - 1));
        sol_command(&s, SOL_CMD_UNDO_ALL);
        CHECK_EQ(nconfirm, 3 - 1 + 0);                        /* (2: not asked) */
        CHECK_EQ(s.nhist, nh);
        sol_cancel_drag(&s);
        sol_free(&s);
        sol_free(&t);
    }

    /* Draw (D): a press on the stock, including the recycle; nothing while dragging */
    start(&s, &f, &r, 0x01, 3);
    int st = s.board.p[SOL_STOCK].n;
    sol_command(&s, SOL_CMD_DRAW);
    CHECK_EQ(s.board.p[SOL_STOCK].n, st - 1);
    CHECK_EQ(s.board.p[SOL_WASTE].n, 1);
    CHECK(s.input);                                           /* the clock may start */
    CHECK_EQ(s.nhist, 1);
    CHECK(sol_begin_drag(&s, SOL_WASTE, 0));
    sol_command(&s, SOL_CMD_DRAW);
    CHECK_EQ(s.board.p[SOL_WASTE].n, 1);
    sol_cancel_drag(&s);
    for (int k = 0; k < st - 1; k++) sol_command(&s, SOL_CMD_DRAW);
    CHECK_EQ(s.board.p[SOL_STOCK].n, 0);
    sol_command(&s, SOL_CMD_DRAW);                            /* the recycle */
    CHECK_EQ(s.board.p[SOL_STOCK].n, st);
    CHECK_EQ(s.recycles, 1);
    sol_free(&s);
}

/* A version 1 file (Solitaire HD 1.1) still loads: the version 2 image without the group and the
 * nauto bytes (all 0 here). */
static void test_save_v1(void)
{
    SolSession a, b;
    Fake fa, fb;
    Reg ra, rb;
    uint8_t *img, *v1;
    size_t len, k = 0, at, n1;
    start(&a, &fa, &ra, 0x0B, 77);
    play_some(&a, 5, 40);
    if (!a.dealt) sol_command(&a, SOL_CMD_DEAL), play_some(&a, 6, 20);
    sol_undo(&a);
    CHECK(sol_game_serialize(&a, &img, &len));
    v1 = malloc(len);
    /* header, the fixed part up to the redo count */
    at = 12 + 4 + 6 + SOL_PACKED_SIZE + 7 * 4 + 2 * 4;
    memcpy(v1, img, at);
    k = at;
    size_t q = at + 4;                                        /* skip the group */
    for (int i = 0; i < a.nhist + a.nredo; i++) {
        const SolAction *x = i < a.nhist ? &a.hist[i] : &a.redo[i - a.nhist];
        CHECK_EQ(x->nauto, 0);
        memcpy(v1 + k, img + q, 7);                           /* type .. waste fan */
        k += 7;
        q += 8;                                               /* + nauto */
        size_t rest = 2u * x->nsteps + SOL_PACKED_SIZE + 12;
        memcpy(v1 + k, img + q, rest);
        k += rest;
        q += rest;
    }
    CHECK_EQ(q, len - 4);
    n1 = k + 4;
    v1[4] = 1, v1[5] = v1[6] = v1[7] = 0;                     /* version 1 */
    uint32_t pl = (uint32_t)(n1 - 16), crc;
    for (int z = 0; z < 4; z++) v1[8 + z] = (uint8_t)(pl >> (8 * z));
    crc = ce_crc32(v1 + 12, n1 - 16);
    for (int z = 0; z < 4; z++) v1[k + (size_t)z] = (uint8_t)(crc >> (8 * z));
    start(&b, &fb, &rb, 0x0B, -1);
    CHECK_EQ(sol_game_restore(&b, v1, n1), SOL_LOAD_OK);
    CHECK(same_game(&a, &b));
    CHECK_EQ(img[4], 2);                                      /* written as version 2 */
    free(img);
    free(v1);
    sol_free(&a);
    sol_free(&b);
}

/* ---- the registry values --------------------------------------------------------------------------- */

static void test_extras_store(void)
{
    Reg r;
    memset(&r, 0, sizeof r);
    CeStore st = reg_store(&r);
    SolExtras x;
    sol_extras_load(&x, &st);
    CHECK(!x.auto_turn && !x.click_move && !x.auto_finish && !x.winnable_only && !x.save_game && !x.warn_unwinnable &&
          !x.auto_home && !x.click_select && !x.no_more_moves && !x.next_game_options && !x.enhanced_anim);
    x.auto_turn = 1;
    x.next_game_options = 1;
    x.save_game = 5;
    x.click_select = 1;
    sol_extras_save(&x, &st);
    CHECK_EQ(reg_find(&r, "AutoTurn") >= 0 ? r.val[reg_find(&r, "AutoTurn")].v : 99, 1);
    CHECK_EQ(r.val[reg_find(&r, "SaveGame")].v, 1);
    CHECK_EQ(r.val[reg_find(&r, "ClickToMove")].v, 0);
    CHECK_EQ(r.val[reg_find(&r, "AutoHome")].v, 0);
    CHECK_EQ(r.val[reg_find(&r, "ClickSelect")].v, 1);
    CHECK_EQ(r.val[reg_find(&r, "NoMoreMoves")].v, 0);         /* 2c */
    CHECK_EQ(r.val[reg_find(&r, "NextGameOptions")].v, 1);
    CHECK_EQ(r.val[reg_find(&r, "EnhancedAnimations")].v, 0);  /* 2d */
    CHECK_EQ(r.n, 11);
    SolExtras y;
    sol_extras_load(&y, &st);
    CHECK(y.auto_turn && y.save_game && !y.click_move && !y.warn_unwinnable && !y.auto_home && y.click_select &&
          !y.no_more_moves && y.next_game_options);
    /* the session never writes them into XP's key */
    SolSession s;
    Fake f;
    Reg xr;
    start(&s, &f, &xr, 0, 1);
    sol_set_extras(&s, &y);
    CHECK_EQ(xr.nset, 0);
    sol_free(&s);
}

/* 2d, "Enhanced animations": the double-click and the right button's autoplay fly their cards (the view's
 * animate_move, before each move); off, as XP, nothing is flown; the result and the history are the same. */
static void test_enhanced_flights(void)
{
    SolSession s, t;
    Fake f, g;
    Reg r, q;
    for (int on = 0; on < 2; on++) {
        start(&s, &f, &r, 0x01, 1);
        board(&s, F(0), "AH", T(0), "#9C 2H", T(1), "3S", T(2), "#4C AD", T(3), "#5C AS", -1);
        extra_set(&s, &s.extras.enhanced_anim, on);
        CHECK_EQ(sol_dblclick(&s, T(0), 1, 0), SOL_PRESS_DONE);  /* 2H home */
        CHECK(pile_is(&s.board, F(0), "AH 2H"));
        CHECK_EQ(f.nanim, on);
        if (on)
            CHECK(f.anim[0][0] == T(0) && f.anim[0][1] == F(0));
        CHECK(sol_autoplay(&s) == 2);                            /* AD, AS */
        CHECK_EQ(f.nanim, 3 * on);
        if (on)
            CHECK(f.anim[1][0] == T(2) && f.anim[2][0] == T(3));
        CHECK_EQ(s.nhist, 2);
        CHECK(!s.busy);
        if (on) {                                                /* the same game as without the option */
            start(&t, &g, &q, 0x01, 1);
            board(&t, F(0), "AH", T(0), "#9C 2H", T(1), "3S", T(2), "#4C AD", T(3), "#5C AS", -1);
            sol_dblclick(&t, T(0), 1, 0);
            sol_autoplay(&t);
            CHECK(sol_board_equal(&s.board, &t.board));
            CHECK_EQ(s.score, t.score);
            CHECK_EQ(g.nanim, 0);
            sol_free(&t);
        }
        sol_free(&s);
    }
}

int main(void)
{
    test_auto_turn();
    test_click_dest();
    test_finish();
    test_hint_rank();
    test_hint_fair();
    test_hint_follow();
    test_hint_flash();
    test_stats();
    test_stats_reset();
    test_save_resume();
    test_winnable();
    test_warning();
    test_extras_store();
    test_auto_home();
    test_click_select();
    test_undo_all();
    test_save_v1();
    test_enhanced_flights();
    return test_summary("test_sol_extras");
}
