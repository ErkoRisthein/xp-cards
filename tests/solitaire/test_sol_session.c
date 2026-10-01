/*
 * Solitaire HD — native tests of the XP Solitaire controller (src/solitaire/session.c), driven
 * through a recording fake UI and a memory registry, against docs/xp-reference/solitaire/rules.md:
 * deal and settings, stock / waste (draw three and one, the cheat, recycling, Vegas pass limits, the
 * fan), drag and drop, double-click, right-click autoplay, the score of every move in Standard, Vegas
 * and None (incl. the Wine-verified sequences), unlimited undo / redo, the keyboard model, the clock,
 * the win flow (natural and Alt+Shift+2, "Deal Again?"), Options / Deck, and the XP bugs fixed.
 */
#include "sol_test.h"

#define T(k) (SOL_TAB0 + (k))      /* tableau column k = 0..6 */
#define F(k) (SOL_FOUND0 + (k))    /* foundation k = 0..3 */

/* Replace the board by the given piles (pile, "cards", ..., -1); every other card goes face down to
 * the bottom of the stock. History, fan and drag are reset; the game stays dealt. */
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

static int same_state(const SolSession *a, const SolBoard *b, int fan, int recycles)
{
    return sol_board_equal(&a->board, b) && a->waste_fan == fan && a->recycles == recycles;
}

/* ---- init, deal, settings ------------------------------------------------------------------------- */

static void test_init_deal(void)
{
    SolSession s;
    Fake f;
    Reg r;
    char buf[64];

    start(&s, &f, &r, 0, -1);
    CHECK_EQ(s.opts.draw, 3);
    CHECK_EQ(s.opts.scoring, SOL_SCORING_STANDARD);
    CHECK(s.opts.timed && s.opts.status_bar && !s.opts.outline && !s.opts.cumulative);
    CHECK(!s.dealt && !sol_board_visible(&s) && !sol_undo_enabled(&s));
    CHECK(s.back >= 0 && s.back < SOL_NBACKS);
    CHECK_EQ(r.nset, 0);                        /* nothing written at start-up (XP: Back not either) */
    sol_score_text(&s, buf, sizeof buf);
    CHECK_STR(buf, "0");
    CHECK_EQ(sol_press(&s, T(0), 0, 0), SOL_PRESS_NONE);   /* nothing dealt: input ignored */
    sol_command(&s, SOL_CMD_DEAL);              /* XP deals at start-up */
    CHECK_EQ(s.seed, 1234567u & 0x7FFF);
    SolBoard d;
    uint32_t rng;
    sol_deal_board(&d, s.seed, &rng);
    CHECK(sol_board_equal(&s.board, &d));
    CHECK_EQ(s.rng, rng);
    CHECK(s.dealt && sol_board_visible(&s) && !s.input && !s.timer_on);
    CHECK_EQ(s.kbd_pile, SOL_STOCK);
    CHECK_EQ(s.kbd_card, 23);
    CHECK_EQ(s.score, 0);
    CHECK_EQ(sol_stock_symbol(&s), SOL_STOCK_CARDS);
    /* two deals in the same second: the next seed (XP repeated the deal) */
    unsigned first = s.seed;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.seed, (first + 1) & 0x7FFF);
    f.now = 0x7FFF;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.seed, 0x7FFF);
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.seed, 0);                        /* wraps */
    f.now = 40000;
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.seed, 40000u & 0x7FFF);
    sol_free(&s);

    /* registry values */
    memset(&r, 0, sizeof r);
    reg_set(&r, "Options", 0x5B);
    reg_set(&r, "Back", 7);
    reg_set(&r, "iCurrency", 3);
    SolSessionUI ui = fake_ui(&f, 0);
    CeStore st = reg_store(&r);
    sol_init(&s, &ui, &st);
    CHECK_EQ(s.opts.scoring, SOL_SCORING_VEGAS);
    CHECK(s.opts.cumulative);
    CHECK_EQ(s.back, 6);
    CHECK_EQ(s.currency, 3);
    sol_deal(&s, 1, 0);
    sol_score_text(&s, buf, sizeof buf);
    CHECK_STR(buf, "-52 $");
    sol_free(&s);
    reg_set(&r, "iCurrency", 4);
    reg_set(&r, "Back", 99);
    sol_init(&s, &ui, &st);
    CHECK_EQ(s.currency, 0);
    CHECK_EQ(s.back, 11);
    sol_free(&s);

    /* a missing Back: a random back each launch, every one possible */
    int seen[SOL_NBACKS] = { 0 }, kinds = 0;
    memset(&r, 0, sizeof r);
    for (uint32_t t = 0; t < 400; t++) {
        f.now = 1000000 + t;
        sol_init(&s, &ui, &st);
        if (!seen[s.back]++) kinds++;
        sol_free(&s);
    }
    CHECK_EQ(kinds, SOL_NBACKS);
    CHECK_EQ(reg_find(&r, "Back"), -1);         /* not written until the Deck dialog's OK */
    sol_init(&s, NULL, NULL);                   /* no UI, no store: defaults */
    CHECK_EQ(s.opts.draw, 3);
    sol_command(&s, SOL_CMD_DEAL);
    CHECK(s.dealt);
    sol_free(&s);
}

/* ---- stock and waste (§3) ------------------------------------------------------------------------- */

static void test_stock(void)
{
    SolSession s;
    Fake f;
    Reg r;

    /* seed 27937: the stock's top is ... 4D JC 7H; draw three turns the packet: 7H JC 4D */
    start(&s, &f, &r, 0, 27937);
    SolBoard deal = s.board;
    CHECK_EQ(sol_press(&s, SOL_STOCK, 23, 0), SOL_PRESS_DONE);
    CHECK(pile_is(&s.board, SOL_WASTE, "7H JC 4D"));
    CHECK_EQ(s.board.p[SOL_STOCK].n, 21);
    CHECK_EQ(sol_waste_fan(&s), 3);
    CHECK_EQ(sol_waste_fan_start(&s), 0);
    CHECK_EQ(s.score, 0);                       /* no score for drawing */
    CHECK(s.input && s.timer_on && f.timer_ms == SOL_TICK_MS);
    CHECK_EQ(sol_press(&s, SOL_STOCK, 20, 0), SOL_PRESS_DONE);
    CHECK(pile_is(&s.board, SOL_WASTE, "7H JC 4D 2D QC 8H"));
    CHECK_EQ(sol_waste_fan_start(&s), 3);
    /* the cheat: Ctrl+Alt+Shift draws one card in draw three */
    CHECK_EQ(sol_press(&s, SOL_STOCK, 17, SOL_MOD_CHEAT), SOL_PRESS_DONE);
    CHECK(pile_is(&s.board, SOL_WASTE, "7H JC 4D 2D QC 8H KH"));
    CHECK_EQ(sol_waste_fan(&s), 1);
    CHECK_EQ(sol_press(&s, SOL_STOCK, 16, SOL_MOD_CTRL | SOL_MOD_SHIFT), SOL_PRESS_DONE);   /* not all three */
    CHECK_EQ(s.board.p[SOL_WASTE].n, 10);
    /* draw to the end: 14 left -> 3,3,3,3,2 */
    for (int i = 0; i < 4; i++) sol_press(&s, SOL_STOCK, 0, 0);
    CHECK_EQ(s.board.p[SOL_STOCK].n, 2);
    sol_press(&s, SOL_STOCK, 0, 0);
    CHECK_EQ(s.board.p[SOL_STOCK].n, 0);
    CHECK_EQ(sol_waste_fan(&s), 2);
    CHECK_EQ(sol_stock_symbol(&s), SOL_STOCK_O);
    /* recycle: the waste turned over; the next pass sees the cards in the original order */
    CHECK_EQ(sol_press(&s, SOL_STOCK, -1, 0), SOL_PRESS_DONE);
    CHECK_EQ(s.recycles, 1);
    CHECK_EQ(s.board.p[SOL_WASTE].n, 0);
    CHECK_EQ(sol_waste_fan(&s), 0);
    CHECK(s.board.p[SOL_STOCK].n == 24 && !memcmp(s.board.p[SOL_STOCK].c, deal.p[SOL_STOCK].c, 24));
    CHECK_EQ(s.score, 0);                       /* draw three: the first three recycles are free */
    sol_free(&s);

    /* an empty stock with an empty waste does nothing (the clock starts anyway) */
    start(&s, &f, &r, 0, 5);
    board(&s, F(0), "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC QC", F(1), "AD 2D 3D 4D 5D 6D 7D 8D 9D TD JD QD",
          F(2), "AH 2H 3H 4H 5H 6H 7H 8H 9H TH JH QH", F(3), "AS 2S 3S 4S 5S 6S 7S 8S 9S TS JS QS",
          T(0), "KC", T(1), "KD", T(2), "KH", T(3), "KS", -1);
    CHECK_EQ(s.board.p[SOL_STOCK].n, 0);
    CHECK_EQ(sol_stock_symbol(&s), SOL_STOCK_O);
    CHECK_EQ(sol_press(&s, SOL_STOCK, -1, 0), SOL_PRESS_NONE);
    CHECK(s.input);
    CHECK_EQ(s.nhist, 0);
    sol_free(&s);

    /* double-click on the stock draws twice; on an empty stock it recycles, then draws */
    start(&s, &f, &r, 0, 27937);
    CHECK_EQ(sol_press(&s, SOL_STOCK, 23, 0), SOL_PRESS_DONE);
    CHECK_EQ(sol_dblclick(&s, SOL_STOCK, 20, 0), SOL_PRESS_DONE);
    CHECK_EQ(s.board.p[SOL_WASTE].n, 6);
    while (s.board.p[SOL_STOCK].n) sol_press(&s, SOL_STOCK, 0, 0);
    CHECK_EQ(sol_press(&s, SOL_STOCK, -1, 0), SOL_PRESS_DONE);        /* recycle */
    CHECK_EQ(sol_dblclick(&s, SOL_STOCK, 23, 0), SOL_PRESS_DONE);     /* then draw */
    CHECK_EQ(s.board.p[SOL_WASTE].n, 3);
    sol_free(&s);

    /* draw one: one card per click, no fan beyond the card itself */
    start(&s, &f, &r, 0x03, 27937);
    CHECK_EQ(s.draw, 1);
    sol_press(&s, SOL_STOCK, 23, 0);
    CHECK(pile_is(&s.board, SOL_WASTE, "7H"));
    sol_press(&s, SOL_STOCK, 22, 0);
    CHECK(pile_is(&s.board, SOL_WASTE, "7H JC"));
    CHECK_EQ(sol_waste_fan(&s), 1);
    CHECK_EQ(sol_waste_fan_start(&s), 1);
    sol_free(&s);
}

/* Standard recycle penalties and the Vegas pass limits (§3.1, §4.2). */
static void drain(SolSession *s) { while (s->board.p[SOL_STOCK].n) sol_press(s, SOL_STOCK, 0, 0); }

static void test_recycle_limits(void)
{
    SolSession s;
    Fake f;
    Reg r;

    /* Standard, draw three: recycles 1-3 free, then -20 each */
    start(&s, &f, &r, 0, 28172);
    s.score = 100;
    for (int k = 1; k <= 6; k++) {
        drain(&s);
        CHECK_EQ(sol_press(&s, SOL_STOCK, -1, 0), SOL_PRESS_DONE);
        CHECK_EQ(s.recycles, k);
        CHECK_EQ(s.score, k <= 3 ? 100 : 100 - 20 * (k - 3));
        CHECK_EQ(sol_stock_symbol(&s), SOL_STOCK_CARDS);
    }
    /* XP: a recycle -20 from 80 -> 60 -> Undo -> 78 */
    s.score = 80;
    drain(&s);
    sol_press(&s, SOL_STOCK, -1, 0);
    CHECK_EQ(s.score, 60);
    CHECK(sol_undo(&s));
    CHECK_EQ(s.score, 78);
    CHECK_EQ(s.recycles, 6);
    sol_free(&s);

    /* Standard, draw one: -100 every time, floored at 0 */
    start(&s, &f, &r, 0x03, 28172);
    s.score = 250;
    for (int k = 1; k <= 4; k++) {
        drain(&s);
        sol_press(&s, SOL_STOCK, -1, 0);
        CHECK_EQ(s.score, k <= 2 ? 250 - 100 * k : 0);
    }
    sol_free(&s);

    /* Vegas, draw three: 3 passes = 2 recycles; the X when none is left; Undo gives a pass back */
    start(&s, &f, &r, 0x1B, 28172);
    CHECK_EQ(s.score, -52);
    for (int k = 1; k <= 2; k++) {
        drain(&s);
        CHECK_EQ(sol_stock_symbol(&s), SOL_STOCK_O);
        CHECK_EQ(sol_press(&s, SOL_STOCK, -1, 0), SOL_PRESS_DONE);
        CHECK_EQ(s.score, -52);                 /* recycles cost nothing in Vegas */
    }
    drain(&s);
    CHECK_EQ(sol_stock_symbol(&s), SOL_STOCK_X);
    int hist = s.nhist;
    CHECK_EQ(sol_press(&s, SOL_STOCK, -1, 0), SOL_PRESS_NONE);
    CHECK_EQ(s.board.p[SOL_STOCK].n, 0);
    CHECK_EQ(s.nhist, hist);
    /* undo the last draws and the second recycle: the pass is back */
    while (s.nhist && s.hist[s.nhist - 1].type != SOL_ACT_RECYCLE) sol_undo(&s);
    CHECK(sol_undo(&s));
    CHECK_EQ(s.recycles, 1);
    CHECK_EQ(sol_stock_symbol(&s), SOL_STOCK_O);
    CHECK_EQ(sol_press(&s, SOL_STOCK, -1, 0), SOL_PRESS_DONE);
    CHECK_EQ(s.recycles, 2);
    sol_free(&s);

    /* Vegas, draw one: a single pass */
    start(&s, &f, &r, 0x13, 28172);
    drain(&s);
    CHECK_EQ(sol_stock_symbol(&s), SOL_STOCK_X);
    CHECK_EQ(sol_press(&s, SOL_STOCK, -1, 0), SOL_PRESS_NONE);
    CHECK_EQ(s.board.p[SOL_WASTE].n, 24);
    sol_free(&s);

    /* None: unlimited, free */
    start(&s, &f, &r, 0x2B, 28172);
    for (int k = 1; k <= 5; k++) {
        drain(&s);
        CHECK_EQ(sol_press(&s, SOL_STOCK, -1, 0), SOL_PRESS_DONE);
    }
    CHECK_EQ(s.score, 0);
    char buf[16];
    sol_score_text(&s, buf, sizeof buf);
    CHECK_STR(buf, "");
    sol_free(&s);
}

/* ---- moves and their scores (§2, §4) --------------------------------------------------------------- */

static void test_move_scores(void)
{
    SolSession s;
    Fake f;
    Reg r;
    static const uint32_t modes[3] = { 0x0B, 0x1B, 0x2B };   /* Standard, Vegas, None */
    for (int m = 0; m < 3; m++) {
        start(&s, &f, &r, modes[m], 1);
        int base = m == 1 ? -52 : 0;
        board(&s, SOL_WASTE, "AS 5C", T(0), "#2C 6H", T(1), "#3C 5S 4D", T(2), "#KD", T(3), "AH",
              T(4), "2S", -1);
        /* waste -> tableau: Standard +5, Vegas 0 */
        CHECK_EQ(sol_press(&s, SOL_WASTE, 1, 0), SOL_PRESS_DRAG);
        CHECK_EQ(sol_drag_over(&s, T(0)), T(0));
        CHECK(sol_drop(&s, T(0)));
        CHECK(pile_is(&s.board, T(0), "#2C 6H 5C"));
        CHECK_EQ(s.score, base + (m == 0 ? 5 : 0));
        /* waste -> foundation: +10 / +5 */
        CHECK_EQ(sol_dblclick(&s, SOL_WASTE, 0, 0), SOL_PRESS_DONE);
        CHECK(pile_is(&s.board, F(0), "AS"));
        CHECK_EQ(s.score, base + (m == 0 ? 15 : m == 1 ? 5 : 0));
        /* tableau -> foundation: AH to the leftmost empty foundation */
        CHECK(sol_begin_drag(&s, T(3), 0));
        CHECK(!sol_can_drop_on(&s, F(0)));
        CHECK(sol_can_drop_on(&s, F(1)) && sol_can_drop_on(&s, F(2)));
        CHECK(sol_drop(&s, F(2)));                  /* any empty foundation by drag */
        CHECK_EQ(s.score, base + (m == 0 ? 25 : m == 1 ? 10 : 0));
        /* foundation -> foundation: no score */
        CHECK(sol_begin_drag(&s, F(2), 0));
        CHECK(sol_drop(&s, F(1)));
        CHECK(pile_is(&s.board, F(1), "AH") && s.board.p[F(2)].n == 0);
        CHECK_EQ(s.score, base + (m == 0 ? 25 : m == 1 ? 10 : 0));
        /* the turned column: turning #KD over: +5 / 0 */
        CHECK_EQ(sol_press(&s, T(2), 0, 0), SOL_PRESS_DONE);
        CHECK(pile_is(&s.board, T(2), "KD"));
        CHECK_EQ(s.score, base + (m == 0 ? 30 : m == 1 ? 10 : 0));
        /* tableau -> tableau: nothing */
        CHECK(sol_begin_drag(&s, T(1), 1));
        CHECK_EQ(s.drag_count, 2);
        CHECK(sol_drop(&s, T(0)) == 0);             /* 5S 4D onto 5C: refused, back */
        CHECK(sol_begin_drag(&s, T(0), 1));
        CHECK(sol_drop(&s, T(1)) == 0);             /* 6H 5C onto 4D: refused */
        CHECK(pile_is(&s.board, T(0), "#2C 6H 5C") && pile_is(&s.board, T(1), "#3C 5S 4D"));
        CHECK_EQ(s.score, base + (m == 0 ? 30 : m == 1 ? 10 : 0));
        /* foundation -> tableau: -15 / -5 */
        CHECK(sol_begin_drag(&s, F(1), 0));
        CHECK(sol_drop(&s, T(4)));                  /* AH onto 2S */
        CHECK(pile_is(&s.board, T(4), "2S AH"));
        CHECK_EQ(s.score, base + (m == 0 ? 15 : m == 1 ? 5 : 0));
        sol_free(&s);
    }

    /* XP (Wine-verified): 115 -> foundation -> tableau -15 -> 100 -> Undo -> 113 */
    start(&s, &f, &r, 0, 1);
    board(&s, F(0), "AC 2C 3C", T(0), "4H", -1);
    s.score = 115;
    CHECK(sol_begin_drag(&s, F(0), 2));
    CHECK(sol_drop(&s, T(0)));
    CHECK_EQ(s.score, 100);
    CHECK(sol_undo(&s));
    CHECK_EQ(s.score, 113);
    CHECK(pile_is(&s.board, F(0), "AC 2C 3C") && pile_is(&s.board, T(0), "4H"));
    /* Undo never goes below 0 */
    s.score = 1;
    CHECK(sol_begin_drag(&s, F(0), 2));
    CHECK(sol_drop(&s, T(0)));
    CHECK(sol_undo(&s));
    CHECK_EQ(s.score, 0);
    sol_free(&s);
}

/* ---- drag rules ----------------------------------------------------------------------------------- */

static void test_drag(void)
{
    SolSession s;
    Fake f;
    Reg r;
    start(&s, &f, &r, 0, 1);
    board(&s, SOL_WASTE, "QH 3D", T(0), "#2C #9C 6H 5S 4D", T(1), "", T(2), "KS QD", F(0), "AD 2D",
          T(3), "7C", -1);
    SolBoard b0 = s.board;
    /* what can be picked up */
    CHECK(!sol_begin_drag(&s, SOL_STOCK, s.board.p[SOL_STOCK].n - 1));
    CHECK(!sol_begin_drag(&s, T(0), 1));        /* face down */
    CHECK(!sol_begin_drag(&s, SOL_WASTE, 0));   /* not the waste's top */
    CHECK(!sol_begin_drag(&s, F(0), 0));        /* not the foundation's top (XP bug fixed) */
    CHECK(!sol_begin_drag(&s, T(1), 0));        /* empty */
    CHECK(!sol_begin_drag(&s, T(0), 5));
    CHECK(!sol_begin_drag(&s, -1, 0) && !sol_begin_drag(&s, SOL_NPILES, 0));
    CHECK_EQ(sol_press(&s, T(0), 0, 0), SOL_PRESS_NONE);     /* a face-down card that is not the top */
    CHECK_EQ(s.nhist, 0);
    CHECK_EQ(sol_press(&s, T(0), 2, 0), SOL_PRESS_DRAG);     /* 6H 5S 4D */
    CHECK_EQ(s.drag_pile, T(0));
    CHECK_EQ(s.drag_index, 2);
    CHECK_EQ(s.drag_count, 3);
    CHECK(!s.drag_kbd);
    CHECK(!sol_idle(&s) && !sol_undo_enabled(&s));
    /* during a drag: other input is ignored */
    CHECK_EQ(sol_press(&s, SOL_STOCK, 0, 0), SOL_PRESS_NONE);
    CHECK(!sol_begin_drag(&s, T(3), 0));
    CHECK_EQ(sol_dblclick(&s, T(3), 0, 0), SOL_PRESS_NONE);
    sol_command(&s, SOL_CMD_DEAL);              /* Deal is grayed while dragging */
    CHECK(sol_board_equal(&s.board, &b0));
    /* targets: 6H onto 7C only */
    CHECK_EQ(sol_drag_over(&s, T(1)), -1);      /* empty column: kings only */
    CHECK_EQ(sol_drag_over(&s, F(0)), -1);      /* three cards */
    CHECK_EQ(sol_drag_over(&s, SOL_WASTE), -1);
    CHECK_EQ(sol_drag_over(&s, T(2)), -1);      /* onto QD */
    CHECK_EQ(sol_drag_over(&s, T(3)), T(3));
    CHECK_EQ(s.target, T(3));
    CHECK_EQ(sol_drag_over(&s, -1), -1);
    CHECK_EQ(s.target, -1);
    /* an illegal drop: everything back, nothing recorded */
    CHECK(!sol_drop(&s, T(2)));
    CHECK(!sol_dragging(&s));
    CHECK(sol_board_equal(&s.board, &b0));
    CHECK_EQ(s.nhist, 0);
    /* a press and release without movement moves nothing (the target is -1) */
    CHECK_EQ(sol_press(&s, T(0), 2, 0), SOL_PRESS_DRAG);
    CHECK(!sol_drop(&s, s.target));
    CHECK(sol_board_equal(&s.board, &b0));
    /* the run goes onto 7C */
    CHECK_EQ(sol_press(&s, T(0), 2, 0), SOL_PRESS_DRAG);
    sol_drag_over(&s, T(3));
    CHECK(sol_drop(&s, s.target));
    CHECK(pile_is(&s.board, T(3), "7C 6H 5S 4D"));
    CHECK(pile_is(&s.board, T(0), "#2C #9C"));
    CHECK_EQ(s.nhist, 1);
    CHECK_EQ(s.score, 0);
    /* a king run to the empty column; a non-king refused there */
    CHECK(sol_begin_drag(&s, T(2), 0));
    CHECK(sol_drop(&s, T(1)));
    CHECK(pile_is(&s.board, T(1), "KS QD") && s.board.p[T(2)].n == 0);
    CHECK(sol_begin_drag(&s, T(3), 3));
    CHECK(!sol_drop(&s, T(2)));
    /* single cards only onto a foundation: 3D from the waste, not 5S 4D */
    CHECK(sol_begin_drag(&s, T(3), 2));
    CHECK(!sol_can_drop_on(&s, F(0)));
    sol_cancel_drag(&s);
    CHECK(sol_begin_drag(&s, SOL_WASTE, 1));
    CHECK(sol_drop(&s, F(0)));
    CHECK(pile_is(&s.board, F(0), "AD 2D 3D"));
    CHECK_EQ(s.score, 10);
    /* outline dragging: the target change repaints (the inverted target); normal dragging does not */
    sol_cancel_drag(&s);
    CHECK(sol_begin_drag(&s, T(3), 3));
    int inv = f.ninval;
    sol_drag_over(&s, F(0));
    CHECK_EQ(f.ninval, inv);
    s.opts.outline = 1;
    sol_drag_over(&s, -1);
    sol_drag_over(&s, F(0));
    CHECK_EQ(s.target, F(0));
    CHECK(f.ninval > inv);
    CHECK(sol_drop(&s, F(0)));
    CHECK(pile_is(&s.board, F(0), "AD 2D 3D 4D"));
    sol_free(&s);
}

/* ---- the XP bugs fixed (§11) ---------------------------------------------------------------------- */

static void test_bugs(void)
{
    SolSession s;
    Fake f;
    Reg r;

    /* 1. Stale drop target after Esc: XP saved an undo record and kept the target, so the next press
     *    and release without movement put the clicked card onto that column unchecked. */
    start(&s, &f, &r, 0, 1);
    board(&s, T(0), "5S", T(1), "6D", T(2), "KD", T(3), "9C", -1);
    SolBoard b0 = s.board;
    CHECK(sol_begin_drag(&s, T(0), 0));
    CHECK_EQ(sol_drag_over(&s, T(1)), T(1));
    CHECK_EQ(sol_key(&s, SOL_KEY_ESCAPE, 0), 1);
    CHECK(!sol_dragging(&s));
    CHECK_EQ(s.target, -1);
    CHECK_EQ(s.nhist, 0);                       /* no undo record */
    CHECK(sol_board_equal(&s.board, &b0));
    CHECK_EQ(sol_press(&s, T(2), 0, 0), SOL_PRESS_DRAG);   /* KD, released without moving */
    CHECK(!sol_drop(&s, s.target));
    CHECK(sol_board_equal(&s.board, &b0));      /* XP: KD landed on 6D */
    CHECK_EQ(sol_press(&s, T(2), 0, 0), SOL_PRESS_DRAG);
    CHECK(!sol_drop(&s, T(1)));                 /* even an explicit stale target is checked again */
    CHECK(sol_board_equal(&s.board, &b0));
    /* the same with focus loss */
    CHECK(sol_begin_drag(&s, T(0), 0));
    sol_drag_over(&s, T(1));
    sol_cancel_drag(&s);
    CHECK_EQ(s.target, -1);
    CHECK_EQ(s.nhist, 0);
    sol_free(&s);

    /* 2. A run grabbed from a foundation's 3-D edge: only the top card comes off a foundation, by
     *    mouse and by keyboard (the cursor cannot reach the lower cards). */
    start(&s, &f, &r, 0, 1);
    board(&s, F(0), "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC QC KC", T(0), "5H", -1);
    for (int i = 0; i < 12; i++) {
        CHECK_EQ(sol_press(&s, F(0), i, 0), SOL_PRESS_NONE);
        CHECK(!sol_dragging(&s));
    }
    CHECK_EQ(sol_press(&s, F(0), 12, 0), SOL_PRESS_DRAG);
    CHECK_EQ(s.drag_count, 1);
    CHECK(!sol_drop(&s, T(0)));                 /* KC onto 5H: refused (XP put 4C..KC there) */
    s.kbd_pile = F(0);
    s.kbd_card = 12;
    for (int i = 0; i < 5; i++) sol_key(&s, SOL_KEY_UP, 0);
    CHECK_EQ(s.kbd_card, 12);
    s.kbd_card = 3;                             /* even a stale cursor picks the top only */
    sol_key(&s, SOL_KEY_RETURN, 0);
    CHECK(sol_dragging(&s) && s.drag_index == 12 && s.drag_count == 1);
    sol_cancel_drag(&s);
    sol_free(&s);

    /* 3. Autoplay during a (keyboard) drag: refused */
    start(&s, &f, &r, 0, 1);
    board(&s, T(0), "AC", T(1), "2H", T(2), "3S", -1);
    CHECK(sol_begin_drag(&s, T(1), 0));
    CHECK_EQ(sol_autoplay(&s), 0);
    CHECK(pile_is(&s.board, T(0), "AC"));
    s.drag_kbd = 1;
    sol_key(&s, SOL_KEY_A, SOL_MOD_CTRL);
    CHECK(pile_is(&s.board, T(0), "AC"));
    sol_cancel_drag(&s);
    CHECK_EQ(sol_autoplay(&s), 1);
    CHECK(pile_is(&s.board, F(0), "AC"));
    sol_free(&s);
}

/* ---- double-click and autoplay (§7) ---------------------------------------------------------------- */

static void test_dblclick_autoplay(void)
{
    SolSession s;
    Fake f;
    Reg r;

    start(&s, &f, &r, 0, 1);
    board(&s, SOL_WASTE, "AD", T(0), "AC", T(1), "2C", T(2), "3C 2D", T(3), "#4C", T(4), "#AH",
          T(5), "9H 8S", F(1), "", -1);
    /* only the top card, only to a foundation, the leftmost that takes it */
    CHECK_EQ(sol_dblclick(&s, T(0), 0, 0), SOL_PRESS_DONE);
    CHECK(pile_is(&s.board, F(0), "AC"));
    CHECK_EQ(s.score, 10);
    CHECK_EQ(sol_dblclick(&s, T(2), 0, 0), SOL_PRESS_DRAG);   /* not the top: an ordinary press */
    CHECK(sol_dragging(&s));
    sol_cancel_drag(&s);
    CHECK_EQ(sol_dblclick(&s, T(5), 1, 0), SOL_PRESS_DRAG);   /* 8S has nowhere to go: a press */
    sol_cancel_drag(&s);
    /* a face-down top card: the first click turns it (+5), the double-click sends it home (+10) */
    CHECK_EQ(sol_press(&s, T(4), 0, 0), SOL_PRESS_DONE);
    CHECK_EQ(sol_dblclick(&s, T(4), 0, 0), SOL_PRESS_DONE);
    CHECK(pile_is(&s.board, F(1), "AH"));
    CHECK_EQ(s.score, 25);
    int h = s.nhist;
    /* right-click: every card that can go home goes, pass after pass, foundations skipped */
    CHECK_EQ(sol_autoplay(&s), 4);              /* AD (waste), 2C, 2D, then 3C */
    CHECK(pile_is(&s.board, F(0), "AC 2C 3C"));
    CHECK(pile_is(&s.board, F(2), "AD 2D"));
    CHECK(pile_is(&s.board, T(3), "#4C"));      /* never turned over */
    CHECK_EQ(s.score, 65);
    CHECK_EQ(s.nhist, h + 1);                   /* one undoable action */
    CHECK_EQ(s.hist[s.nhist - 1].nsteps, 4);
    CHECK_EQ(sol_autoplay(&s), 0);
    CHECK_EQ(s.nhist, h + 1);
    CHECK(sol_undo(&s));                        /* all four back, -2 */
    CHECK(pile_is(&s.board, SOL_WASTE, "AD") && pile_is(&s.board, T(2), "3C 2D") &&
          pile_is(&s.board, T(1), "2C"));
    CHECK_EQ(s.score, 23);
    CHECK(sol_redo(&s));
    CHECK(pile_is(&s.board, F(0), "AC 2C 3C") && pile_is(&s.board, F(2), "AD 2D"));
    CHECK_EQ(s.score, 63);
    /* Ctrl+A is the same; 'A' alone nothing */
    CHECK(sol_undo(&s));
    CHECK_EQ(sol_key(&s, SOL_KEY_A, 0), 1);
    CHECK(pile_is(&s.board, SOL_WASTE, "AD"));
    CHECK_EQ(sol_key(&s, SOL_KEY_A, SOL_MOD_CTRL), 1);
    CHECK(pile_is(&s.board, F(2), "AD 2D"));
    sol_free(&s);

    /* no safety rule: 2s and 3s go home even if the tableau might want them */
    start(&s, &f, &r, 0, 1);
    board(&s, F(0), "AH", F(1), "AS", T(0), "2H", T(1), "2S", T(2), "3H", T(3), "4C", -1);
    CHECK_EQ(sol_autoplay(&s), 3);
    CHECK(pile_is(&s.board, F(0), "AH 2H 3H") && pile_is(&s.board, F(1), "AS 2S"));
    sol_free(&s);
}

/* ---- undo / redo (extras) ----------------------------------------------------------------------- */

typedef struct Snap { SolBoard b; int fan, score, recycles; } Snap;

static void snap(const SolSession *s, Snap *p)
{
    p->b = s->board;
    p->fan = s->waste_fan;
    p->score = s->score;
    p->recycles = s->recycles;
}

static void test_undo_redo(void)
{
    SolSession s;
    Fake f;
    Reg r;
    for (int vegas = 0; vegas < 2; vegas++) {
        start(&s, &f, &r, vegas ? 0x1B : 0x0B, 28481);
        Snap st[64];
        int n = 0;
        snap(&s, &st[n++]);
        /* a scripted game: draws, moves, a turn-over, a recycle (via draining), autoplay */
        sol_press(&s, SOL_STOCK, 0, 0);
        snap(&s, &st[n++]);
        sol_press(&s, SOL_STOCK, 0, 0);
        snap(&s, &st[n++]);
        /* seed 28481: T5 top AS -> foundation */
        CHECK_EQ(sol_dblclick(&s, T(4), 4, 0), SOL_PRESS_DONE);
        snap(&s, &st[n++]);
        CHECK_EQ(sol_press(&s, T(4), 3, 0), SOL_PRESS_DONE);   /* turn #TC */
        snap(&s, &st[n++]);
        while (s.board.p[SOL_STOCK].n) { sol_press(&s, SOL_STOCK, 0, 0); snap(&s, &st[n++]); }
        CHECK_EQ(sol_press(&s, SOL_STOCK, -1, 0), SOL_PRESS_DONE);
        snap(&s, &st[n++]);
        sol_press(&s, SOL_STOCK, 0, 0);
        snap(&s, &st[n++]);
        int moved = sol_autoplay(&s);
        if (moved) snap(&s, &st[n++]);
        CHECK_EQ(s.nhist, n - 1);
        Snap end = st[n - 1];
        /* undo everything: each step restores the exact position, fan and recycle count; the score is
         * the one before the action, minus 2 in Standard (floored) */
        for (int k = n - 1; k >= 1; k--) {
            CHECK(sol_undo_enabled(&s));
            CHECK(sol_undo(&s));
            CHECK(same_state(&s, &st[k - 1].b, st[k - 1].fan, st[k - 1].recycles));
            int want = vegas ? st[k - 1].score : (st[k - 1].score - 2 < 0 ? 0 : st[k - 1].score - 2);
            CHECK_EQ(s.score, want);
        }
        CHECK(!sol_undo_enabled(&s) && !sol_undo(&s));
        SolBoard d;
        sol_deal_board(&d, 28481, NULL);
        CHECK(sol_board_equal(&s.board, &d));   /* back to the deal */
        CHECK_EQ(s.score, vegas ? -52 : 0);
        CHECK_EQ(s.nredo, n - 1);
        /* redo everything: the same positions again; Vegas the same scores */
        for (int k = 1; k < n; k++) {
            CHECK(sol_redo_enabled(&s));
            CHECK(sol_redo(&s));
            CHECK(same_state(&s, &st[k].b, st[k].fan, st[k].recycles));
            if (vegas) CHECK_EQ(s.score, st[k].score);
        }
        CHECK(!sol_redo_enabled(&s) && !sol_redo(&s));
        CHECK(same_state(&s, &end.b, end.fan, end.recycles));
        CHECK_EQ(s.nhist, n - 1);
        /* undo two, then a new action clears the redo stack */
        sol_command(&s, SOL_CMD_UNDO);
        sol_command(&s, SOL_CMD_UNDO);
        CHECK_EQ(s.nredo, 2);
        sol_press(&s, SOL_STOCK, 0, 0);
        CHECK_EQ(s.nredo, 0);
        CHECK(!sol_redo_enabled(&s));
        /* Redo via the command; a deal clears both */
        sol_command(&s, SOL_CMD_UNDO);
        CHECK(sol_redo_enabled(&s));
        sol_command(&s, SOL_CMD_REDO);
        CHECK(!sol_redo_enabled(&s));
        sol_command(&s, SOL_CMD_UNDO);
        sol_command(&s, SOL_CMD_DEAL);
        CHECK(!sol_undo_enabled(&s) && !sol_redo_enabled(&s));
        sol_free(&s);
    }

    /* Standard: Undo charges -2 for every action undone; Redo scores the action again from there */
    start(&s, &f, &r, 0, 1);
    board(&s, SOL_WASTE, "AS", T(0), "#3D 2H", T(1), "3S", -1);
    sol_dblclick(&s, SOL_WASTE, 0, 0);          /* +10 */
    sol_press(&s, T(0), 1, 0);                  /* drag 2H */
    sol_drop(&s, T(1));                         /* onto 3S: 0 */
    sol_press(&s, T(0), 0, 0);                  /* turn #3D: +5 */
    CHECK_EQ(s.score, 15);
    sol_undo(&s);                               /* 10 - 2 */
    CHECK_EQ(s.score, 8);
    sol_undo(&s);                               /* 10 - 2 */
    CHECK_EQ(s.score, 8);
    sol_undo(&s);                               /* 0 - 2 -> 0 */
    CHECK_EQ(s.score, 0);
    sol_redo(&s);
    sol_redo(&s);
    sol_redo(&s);
    CHECK_EQ(s.score, 15);
    CHECK(pile_is(&s.board, T(0), "3D"));
    /* the turn-over is undoable (XP: it killed Undo) and redoes face up */
    sol_undo(&s);
    CHECK(pile_is(&s.board, T(0), "#3D"));
    CHECK_EQ(s.score, 8);
    sol_redo(&s);
    CHECK(pile_is(&s.board, T(0), "3D"));
    CHECK_EQ(s.score, 13);
    sol_free(&s);
}

/* ---- keyboard (§9.1) ----------------------------------------------------------------------------- */

static void key(SolSession *s, int k, int mods) { CHECK_EQ(sol_key(s, k, mods), 1); }

static void test_keyboard(void)
{
    SolSession s;
    Fake f;
    Reg r;
    start(&s, &f, &r, 0, 27937);
    CHECK_EQ(s.kbd_pile, 0);
    CHECK_EQ(s.kbd_card, 23);
    /* Tab: the next pile of another class: stock -> waste -> foundation 1 -> tableau 1 -> stock */
    static const int tab[] = { 1, 2, 6, 0, 1 };
    for (int i = 0; i < 5; i++) {
        key(&s, SOL_KEY_TAB, 0);
        CHECK_EQ(s.kbd_pile, tab[i]);
    }
    CHECK_EQ(f.kpile, 1);                       /* the pointer follows (SetCursorPos) */
    CHECK_EQ(f.kcard, 0);
    key(&s, SOL_KEY_TAB, 0);
    key(&s, SOL_KEY_TAB, 0);
    CHECK_EQ(s.kbd_pile, 6);
    key(&s, SOL_KEY_TAB, SOL_MOD_SHIFT);        /* 6 -> 5 */
    CHECK_EQ(s.kbd_pile, 5);
    key(&s, SOL_KEY_TAB, SOL_MOD_SHIFT);        /* 5 -> 1 (2..4 are foundations too) */
    CHECK_EQ(s.kbd_pile, 1);
    /* arrows: wrapping; with Shift, piles of the same class skipped */
    key(&s, SOL_KEY_END, 0);
    CHECK_EQ(s.kbd_pile, 12);
    CHECK_EQ(s.kbd_card, 6);
    key(&s, SOL_KEY_RIGHT, 0);
    CHECK_EQ(s.kbd_pile, 0);
    key(&s, SOL_KEY_LEFT, 0);
    CHECK_EQ(s.kbd_pile, 12);
    key(&s, SOL_KEY_HOME, 0);
    CHECK_EQ(s.kbd_pile, 0);
    key(&s, SOL_KEY_LEFT, SOL_MOD_SHIFT);       /* stock -> the last tableau column */
    CHECK_EQ(s.kbd_pile, 12);
    key(&s, SOL_KEY_RIGHT, SOL_MOD_SHIFT);      /* tableau -> stock */
    CHECK_EQ(s.kbd_pile, 0);
    s.kbd_pile = 2;
    key(&s, SOL_KEY_RIGHT, SOL_MOD_SHIFT);      /* foundation 1 -> tableau 1 */
    CHECK_EQ(s.kbd_pile, 6);
    key(&s, SOL_KEY_LEFT, 0);
    CHECK_EQ(s.kbd_pile, 5);
    CHECK_EQ(s.kbd_card, 0);                    /* empty pile */
    CHECK_EQ(sol_key(&s, 'Q', 0), 0);           /* not ours */
    CHECK_EQ(sol_key(&s, 0x71 /* F2 */, 0), 0);
    sol_free(&s);

    /* Up / Down within the face-up run, clamped; a pile without face-up cards keeps the top */
    start(&s, &f, &r, 0, 1);
    board(&s, T(0), "#2C #9C 6H 5S 4D", T(1), "#KD", -1);
    s.kbd_pile = T(0);
    s.kbd_card = 4;
    static const int ud[] = { SOL_KEY_UP, SOL_KEY_UP, SOL_KEY_UP, SOL_KEY_DOWN, SOL_KEY_DOWN, SOL_KEY_DOWN };
    static const int want[] = { 3, 2, 2, 3, 4, 4 };
    for (int i = 0; i < 6; i++) {
        key(&s, ud[i], 0);
        CHECK_EQ(s.kbd_card, want[i]);
        CHECK_EQ(f.kcard, want[i]);
    }
    s.kbd_pile = T(1);
    s.kbd_card = 0;
    key(&s, SOL_KEY_UP, 0);
    CHECK_EQ(s.kbd_card, 0);
    /* Enter on a face-down top card turns it over; the clock starts */
    CHECK(!s.input);
    key(&s, SOL_KEY_RETURN, 0);
    CHECK(pile_is(&s.board, T(1), "KD"));
    CHECK(s.input && s.timer_on);
    /* Enter on 5S picks up 5S 4D; the cursor goes to the pile's top card; the stock and the waste are
     * skipped while dragging */
    s.kbd_pile = T(0);
    s.kbd_card = 4;
    key(&s, SOL_KEY_UP, 0);
    key(&s, SOL_KEY_SPACE, 0);
    CHECK(sol_dragging(&s) && s.drag_kbd);
    CHECK_EQ(s.drag_index, 3);
    CHECK_EQ(s.drag_count, 2);
    CHECK_EQ(s.kbd_card, 4);
    CHECK(f.kdrag);
    key(&s, SOL_KEY_HOME, 0);                   /* the stock cannot take the cursor now */
    CHECK_EQ(s.kbd_pile, T(0));
    s.kbd_pile = T(6);
    key(&s, SOL_KEY_RIGHT, 0);                  /* 12 -> (0, 1 skipped) -> 2 */
    CHECK_EQ(s.kbd_pile, 2);
    key(&s, SOL_KEY_LEFT, 0);                   /* 2 -> 12 */
    CHECK_EQ(s.kbd_pile, 12);
    key(&s, SOL_KEY_TAB, 0);                    /* 12 -> foundation 1 (stock, waste skipped) */
    CHECK_EQ(s.kbd_pile, 2);
    /* Enter over a pile that does not take them: back */
    key(&s, SOL_KEY_RETURN, 0);
    CHECK(!sol_dragging(&s));
    CHECK(pile_is(&s.board, T(0), "#2C #9C 6H 5S 4D"));
    CHECK_EQ(s.nhist, 1);                       /* only the turn-over */
    /* again, to 6D? there is none; onto the empty column: refused (not a king); Esc cancels */
    s.kbd_pile = T(0);
    s.kbd_card = 3;
    key(&s, SOL_KEY_RETURN, 0);
    CHECK(sol_dragging(&s));
    key(&s, SOL_KEY_ESCAPE, 0);
    CHECK(!sol_dragging(&s));
    CHECK_EQ(s.nhist, 1);
    /* KD (T2) onto the empty column T3 by keyboard */
    s.kbd_pile = T(1);
    s.kbd_card = 0;
    key(&s, SOL_KEY_RETURN, 0);
    CHECK(sol_dragging(&s));
    key(&s, SOL_KEY_RIGHT, 0);
    CHECK_EQ(s.kbd_pile, T(2));
    int nk = f.nkbd;
    key(&s, SOL_KEY_RETURN, 0);
    CHECK(!sol_dragging(&s));
    CHECK(pile_is(&s.board, T(2), "KD") && s.board.p[T(1)].n == 0);
    CHECK_EQ(f.nkbd, nk);                       /* a drop does not move the pointer (XP) */
    CHECK_EQ(s.kbd_card, 0);
    CHECK_EQ(s.nhist, 2);
    /* Enter on the stock draws; on an empty pile nothing */
    s.kbd_pile = SOL_STOCK;
    key(&s, SOL_KEY_RETURN, 0);
    CHECK_EQ(s.board.p[SOL_WASTE].n, 3);
    CHECK_EQ(s.kbd_card, s.board.p[SOL_STOCK].n - 1);
    s.kbd_pile = T(5);
    int h = s.nhist;
    key(&s, SOL_KEY_RETURN, 0);
    CHECK_EQ(s.nhist, h);
    CHECK(!sol_dragging(&s));
    /* Enter on the waste picks up its top card only */
    s.kbd_pile = SOL_WASTE;
    s.kbd_card = 0;
    key(&s, SOL_KEY_UP, 0);
    CHECK_EQ(s.kbd_card, 2);                    /* the waste: its top card only */
    key(&s, SOL_KEY_DOWN, 0);
    CHECK_EQ(s.kbd_card, 2);
    key(&s, SOL_KEY_RETURN, 0);
    CHECK(sol_dragging(&s) && s.drag_pile == SOL_WASTE && s.drag_count == 1);
    CHECK_EQ(s.kbd_pile, SOL_WASTE);            /* the waste cannot take the cursor while dragging */
    key(&s, SOL_KEY_ESCAPE, 0);
    /* the reported cursor is clamped to the pile */
    s.kbd_pile = T(0);
    s.kbd_card = 40;
    int p, c;
    sol_kbd_cursor(&s, &p, &c);
    CHECK(p == T(0) && c == 4);
    sol_free(&s);
}

/* ---- clock (§5) ---------------------------------------------------------------------------------- */

static void test_clock(void)
{
    SolSession s;
    Fake f;
    Reg r;
    start(&s, &f, &r, 0, 1);
    CHECK(!s.timer_on && f.timer_ms == 0);
    sol_key(&s, SOL_KEY_RIGHT, 0);              /* arrow keys do not start it */
    sol_timer(&s, SOL_TIMER_CLOCK);
    CHECK_EQ(s.ticks, 0);
    CHECK(!s.timer_on);
    CHECK_EQ(sol_press(&s, SOL_MISS, -1, 0), SOL_PRESS_NONE);   /* a press on the felt starts it */
    CHECK(s.timer_on && f.timer_ms == SOL_TICK_MS && sol_clock_running(&s));
    s.score = 10;
    for (int i = 0; i < 39; i++) sol_timer(&s, SOL_TIMER_CLOCK);
    CHECK_EQ(s.score, 10);
    CHECK_EQ(sol_seconds(&s), 9);
    sol_timer(&s, SOL_TIMER_CLOCK);             /* 40 ticks = 10 s: -2 */
    CHECK_EQ(s.score, 8);
    CHECK_EQ(sol_seconds(&s), 10);
    sol_timer(&s, 1234);                        /* other timers: ignored */
    CHECK_EQ(s.ticks, 40);
    /* minimized: paused */
    sol_set_minimized(&s, 1);
    CHECK(!s.timer_on && f.timer_ms == 0);
    sol_timer(&s, SOL_TIMER_CLOCK);
    CHECK_EQ(s.ticks, 40);
    sol_set_minimized(&s, 0);
    CHECK(s.timer_on && f.timer_ms == SOL_TICK_MS);
    /* saturation */
    s.ticks = SOL_TICKS_MAX - 1;
    sol_timer(&s, SOL_TIMER_CLOCK);
    sol_timer(&s, SOL_TIMER_CLOCK);
    CHECK_EQ(s.ticks, SOL_TICKS_MAX);
    CHECK_EQ(sol_seconds(&s), 8191);
    /* a new deal stops and resets it */
    sol_command(&s, SOL_CMD_DEAL);
    CHECK(!s.timer_on && s.ticks == 0 && !s.input);
    sol_free(&s);

    /* untimed: never counts, no penalty, no bonus */
    start(&s, &f, &r, 0x09, 1);
    sol_press(&s, SOL_MISS, -1, 0);
    CHECK(!s.timer_on);
    for (int i = 0; i < 100; i++) sol_timer(&s, SOL_TIMER_CLOCK);
    CHECK_EQ(s.ticks, 0);
    sol_free(&s);

    /* Vegas: the clock runs without a penalty */
    start(&s, &f, &r, 0x1B, 1);
    sol_press(&s, SOL_MISS, -1, 0);
    for (int i = 0; i < 400; i++) sol_timer(&s, SOL_TIMER_CLOCK);
    CHECK_EQ(s.ticks, 400);
    CHECK_EQ(s.score, -52);
    sol_free(&s);
}

/* ---- win (§8) ------------------------------------------------------------------------------------ */

static void near_win(SolSession *s)
{
    board(s, F(0), "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC QC", F(1), "AD 2D 3D 4D 5D 6D 7D 8D 9D TD JD QD KD",
          F(2), "AH 2H 3H 4H 5H 6H 7H 8H 9H TH JH QH KH", F(3), "AS 2S 3S 4S 5S 6S 7S 8S 9S TS JS QS KS",
          T(6), "KC", -1);
}

static void test_win(void)
{
    SolSession s;
    Fake f;
    Reg r;
    char buf[128];

    /* a natural win by drag, Standard, timed at 48 s: bonus 14560 */
    start(&s, &f, &r, 0, 1);
    near_win(&s);
    sol_press(&s, SOL_MISS, -1, 0);
    s.ticks = 48 * 4;
    s.score = 500;
    CHECK(sol_begin_drag(&s, T(6), 0));         /* something in the history and the redo stack */
    CHECK(sol_drop(&s, T(0)));
    sol_undo(&s);
    CHECK(sol_redo_enabled(&s));
    CHECK_EQ(sol_press(&s, T(6), 0, 0), SOL_PRESS_DRAG);
    sol_drag_over(&s, F(0));
    int inv = f.ninval;
    CHECK(sol_drop(&s, F(0)));
    CHECK(f.ninval > inv);                      /* the last card was drawn before the cascade */
    CHECK_EQ(f.ncascade, 1);
    CHECK(!f.cascade_dealt && f.cascade_visible && !f.cascade_forced);
    CHECK_EQ(f.cascade_hist, 0);
    CHECK_EQ(f.cascade_score, 500 - 2 + 10 + 14560);   /* (500 - 2 for the undo) */
    CHECK_STR(f.cascade_text, "Bonus: 14560  Press Esc or a mouse button to stop...");
    CHECK_STR(f.log, "cascade;dealagain;");
    CHECK(!s.dealt && s.won && !sol_board_visible(&s));
    CHECK(!s.timer_on);
    CHECK(!sol_undo_enabled(&s) && !sol_redo_enabled(&s));
    /* "No": the table stays frozen */
    CHECK_EQ(sol_press(&s, F(0), 12, 0), SOL_PRESS_NONE);
    CHECK_EQ(sol_autoplay(&s), 0);
    sol_command(&s, SOL_CMD_UNDO);
    sol_command(&s, SOL_CMD_FORCEWIN);
    CHECK_EQ(f.ncascade, 1);
    int t = s.ticks;
    sol_timer(&s, SOL_TIMER_CLOCK);
    CHECK_EQ(s.ticks, t);
    sol_command(&s, SOL_CMD_DEAL);
    CHECK(s.dealt && sol_board_visible(&s) && !s.won);
    sol_free(&s);

    /* "Yes" posts Deal (XP: PostMessage WM_COMMAND 1000); without post_command it deals at once */
    start(&s, &f, &r, 0, 1);
    SolSessionUI ui = fake_ui(&f, 1);
    s.ui = ui;
    f.deal_again = 1;
    near_win(&s);
    CHECK_EQ(sol_dblclick(&s, T(6), 0, 0), SOL_PRESS_DONE);   /* win by double-click */
    CHECK_EQ(f.posted, SOL_CMD_DEAL);
    CHECK(!s.dealt);
    sol_command(&s, f.posted);
    CHECK(s.dealt);
    s.ui.post_command = NULL;
    near_win(&s);
    unsigned seed = s.seed;
    CHECK_EQ(sol_autoplay(&s), 1);              /* win by autoplay */
    CHECK(s.dealt && s.seed != seed && s.board.p[SOL_STOCK].n == 24);
    CHECK_EQ(f.ncascade, 2);
    sol_free(&s);

    /* untimed Standard: "Bonus: 0"; Vegas and None: no bonus text */
    static const struct { uint32_t opts; const char *text; } wt[] = {
        { 0x09, "Bonus: 0  Press Esc or a mouse button to stop..." },
        { 0x1B, "Press Esc or a mouse button to stop..." },
        { 0x2B, "Press Esc or a mouse button to stop..." } };
    for (int i = 0; i < 3; i++) {
        start(&s, &f, &r, wt[i].opts, 1);
        near_win(&s);
        sol_press(&s, SOL_MISS, -1, 0);
        s.ticks = 400;
        int before = s.score;
        sol_autoplay(&s);
        CHECK_STR(f.cascade_text, wt[i].text);
        CHECK_EQ(s.score, before + (i == 1 ? 5 : i == 0 ? 10 : 0));
        sol_free(&s);
    }

    /* win through Redo (the undone move replaced by the winning one) */
    start(&s, &f, &r, 0, 1);
    near_win(&s);
    CHECK(sol_begin_drag(&s, T(6), 0));
    CHECK(sol_drop(&s, T(0)));
    sol_undo(&s);
    s.redo[0].dst = F(0);
    CHECK(sol_redo(&s));
    CHECK_EQ(f.ncascade, 1);
    CHECK(!s.dealt);
    sol_free(&s);

    /* Alt+Shift+2: foundations filled C, D, H, S without a repaint, the cascade from the origins;
     * Vegas credits nothing, Standard pays the bonus */
    start(&s, &f, &r, 0x1B, 27937);
    sol_press(&s, SOL_STOCK, 0, 0);
    inv = f.ninval;
    sol_command(&s, SOL_CMD_FORCEWIN);
    CHECK_EQ(f.ncascade, 1);
    CHECK(f.cascade_forced && !f.cascade_dealt);
    CHECK_EQ(f.cascade_score, -52);
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 13; i++) CHECK_EQ(s.board.p[F(k)].c[i], (i * 4 + k) | SOL_UP);
    for (int p = 0; p < SOL_NPILES; p++) if (!sol_is_found(p)) CHECK_EQ(s.board.p[p].n, 0);
    CHECK(sol_board_valid(&s.board, buf, sizeof buf));
    CHECK_EQ(f.ninval, inv + 1);                /* only the erase after the cascade */
    sol_free(&s);
    start(&s, &f, &r, 0, 27937);
    sol_press(&s, SOL_MISS, -1, 0);
    s.ticks = 100 * 4;
    sol_command(&s, SOL_CMD_FORCEWIN);
    CHECK_EQ(s.score, 7000);
    CHECK_STR(f.cascade_text, "Bonus: 7000  Press Esc or a mouse button to stop...");
    sol_free(&s);
    /* not before a deal; a drag is dropped first */
    start(&s, &f, &r, 0, -1);
    sol_command(&s, SOL_CMD_FORCEWIN);
    CHECK_EQ(f.ncascade, 0);
    sol_free(&s);
    start(&s, &f, &r, 0, 27937);
    CHECK_EQ(sol_press(&s, T(0), 0, 0), SOL_PRESS_DRAG);
    sol_command(&s, SOL_CMD_FORCEWIN);
    CHECK_EQ(f.ncascade, 1);
    CHECK(!sol_dragging(&s));
    sol_free(&s);
}

/* ---- Options, Deck, Vegas cumulative (§4.4, §10) -------------------------------------------------- */

static void test_options(void)
{
    SolSession s;
    Fake f;
    Reg r;
    start(&s, &f, &r, 0, 1);
    SolOptions o = s.opts;
    SolBoard b = s.board;
    unsigned seed = s.seed;
    /* Status bar / Outline / Cumulative: the game goes on; Options written at once */
    o.status_bar = 0;
    o.outline = 1;
    o.cumulative = 1;
    CHECK_EQ(sol_apply_options(&s, &o), 0);
    CHECK(sol_board_equal(&s.board, &b) && s.seed == seed);
    uint32_t v;
    CHECK(reg_get(&r, "Options", &v));
    CHECK_EQ(v, 0x4E);
    /* Draw: a new deal, score reset */
    s.score = 40;
    o.draw = 1;
    f.now = 777;
    CHECK_EQ(sol_apply_options(&s, &o), 1);
    CHECK_EQ(s.seed, 777u);
    CHECK_EQ(s.draw, 1);
    CHECK_EQ(s.score, 0);
    reg_get(&r, "Options", &v);
    CHECK_EQ(v, 0x46);
    /* Vegas: a new deal at -52; Cumulative carries the total over Deal and "Deal Again? Yes" ... */
    o.scoring = SOL_SCORING_VEGAS;
    o.draw = 3;
    CHECK_EQ(sol_apply_options(&s, &o), 1);
    CHECK_EQ(s.score, -52);
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.score, -104);
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.score, -156);
    f.deal_again = 1;
    near_win(&s);
    sol_autoplay(&s);                           /* +5, win, Yes -> deal: carried */
    CHECK_EQ(s.score, -156 + 5 - 52);
    /* ... but an Options redeal resets it */
    o.timed = 0;
    CHECK_EQ(sol_apply_options(&s, &o), 1);
    CHECK_EQ(s.score, -52);
    /* without Cumulative every deal starts at -52 */
    o.cumulative = 0;
    sol_apply_options(&s, &o);
    sol_command(&s, SOL_CMD_DEAL);
    CHECK_EQ(s.score, -52);
    /* odd values are normalised */
    o.draw = 7;
    o.scoring = 9;
    o.timed = 5;
    sol_apply_options(&s, &o);
    CHECK(s.opts.draw == 3 && s.opts.scoring == SOL_SCORING_STANDARD && s.opts.timed == 1);
    /* Deck: Back written as index + 1; a repaint only when it changed; never a redeal */
    seed = s.seed;
    int inv = f.ninval;
    sol_set_back(&s, 9);
    CHECK_EQ(s.back, 9);
    CHECK(reg_get(&r, "Back", &v));
    CHECK_EQ(v, 10);
    CHECK_EQ(f.ninval, inv + 1);
    sol_set_back(&s, 9);
    CHECK_EQ(f.ninval, inv + 1);
    sol_set_back(&s, 40);
    CHECK_EQ(s.back, 11);
    CHECK_EQ(s.seed, seed);
    sol_free(&s);
}

/* ---- review fixes ---------------------------------------------------------------------------------- */

/* Enter / Space during a drag is XP's MouseUp (KeyHit 0x1002F4C): a drop on the highlighted target,
 * which the UI tracks through sol_drag_over as the cards follow the pointer or the keyboard cursor; the
 * keyboard cursor's pile only without a UI. (Was: always the cursor's pile, so Space during a mouse drag
 * over a target sent the cards back.) */
static void test_key_drop_target(void)
{
    SolSession s;
    Fake f;
    Reg r;
    start(&s, &f, &r, 0, 1);
    board(&s, T(0), "#9C 4S", T(1), "5D", T(2), "5H", -1);
    s.kbd_pile = SOL_STOCK;
    s.kbd_card = 0;
    /* a mouse drag of 4S over 5H (the UI's target); Space drops it there, not on the cursor's pile */
    CHECK_EQ(sol_press(&s, T(0), 1, 0), SOL_PRESS_DRAG);
    CHECK_EQ(sol_key_drop_target(&s), -1);      /* no target tracked yet: the cursor's pile, the stock */
    CHECK_EQ(sol_drag_over(&s, T(2)), T(2));
    CHECK_EQ(sol_key_drop_target(&s), T(2));
    key(&s, SOL_KEY_SPACE, 0);
    CHECK(!sol_dragging(&s));
    CHECK(pile_is(&s.board, T(2), "5H 4S"));
    CHECK(pile_is(&s.board, T(0), "#9C"));
    CHECK_EQ(s.nhist, 1);
    /* a keyboard pick-up, the cursor over 5D; without a UI the cursor's pile takes it */
    CHECK(sol_undo(&s));
    s.kbd_pile = T(0);
    s.kbd_card = 1;
    key(&s, SOL_KEY_RETURN, 0);
    CHECK(sol_dragging(&s) && s.drag_kbd);
    key(&s, SOL_KEY_RIGHT, 0);
    CHECK_EQ(s.kbd_pile, T(1));
    CHECK_EQ(sol_key_drop_target(&s), T(1));
    /* ... but once the UI tracks the target, the cards' place counts: moved (by the mouse) over the
     * felt they go back, although the cursor's pile would take them */
    sol_drag_over(&s, -1);
    CHECK_EQ(sol_key_drop_target(&s), -1);
    key(&s, SOL_KEY_RETURN, 0);
    CHECK(!sol_dragging(&s));
    CHECK(pile_is(&s.board, T(0), "#9C 4S") && pile_is(&s.board, T(1), "5D"));
    /* a target that does not take the cards is none; a new drag forgets the tracked target */
    key(&s, SOL_KEY_LEFT, 0);
    CHECK_EQ(s.kbd_pile, T(0));
    key(&s, SOL_KEY_RETURN, 0);
    CHECK(sol_dragging(&s));
    sol_drag_over(&s, T(0));                    /* its own pile */
    CHECK_EQ(s.target, -1);
    CHECK_EQ(sol_key_drop_target(&s), -1);
    key(&s, SOL_KEY_ESCAPE, 0);
    key(&s, SOL_KEY_RETURN, 0);
    CHECK(sol_dragging(&s) && !s.target_ui);
    key(&s, SOL_KEY_RIGHT, 0);
    key(&s, SOL_KEY_RIGHT, 0);                  /* over 5H, no UI: the cursor's pile */
    key(&s, SOL_KEY_RETURN, 0);
    CHECK(pile_is(&s.board, T(2), "5H 4S"));
    sol_free(&s);
}

static void ticks(SolSession *s, int n)
{
    for (int i = 0; i < n; i++) sol_timer(s, SOL_TIMER_CLOCK);
}

/* Standard, timed: the first Undo after an action is XP's (the score from before it: the clock's
 * penalties since it are given back, then -2); a deeper Undo keeps the penalties the clock took after
 * the action it undoes. (Was: every Undo restored the score from before its action, so Undo all + Redo
 * all erased the time penalty of the whole game.) */
static void test_undo_clock(void)
{
    SolSession s;
    Fake f;
    Reg r;
    start(&s, &f, &r, 0, 1);
    board(&s, SOL_WASTE, "AS", T(0), "2S", T(1), "3S", -1);
    s.score = 100;
    sol_press(&s, SOL_MISS, -1, 0);             /* the clock starts */
    CHECK(s.timer_on);
    CHECK_EQ(sol_dblclick(&s, SOL_WASTE, 0, 0), SOL_PRESS_DONE);   /* AS home: 110 */
    ticks(&s, 40);                              /* 10 s: 108 */
    CHECK_EQ(sol_dblclick(&s, T(0), 0, 0), SOL_PRESS_DONE);        /* 2S: 118 */
    ticks(&s, 40);                              /* 116 */
    CHECK_EQ(sol_dblclick(&s, T(1), 0, 0), SOL_PRESS_DONE);        /* 3S: 126 */
    ticks(&s, 80);                              /* 122 */
    CHECK_EQ(s.score, 122);
    CHECK(sol_undo(&s));                        /* XP: 116 (the clock's -4 since 3S given back) - 2 */
    CHECK_EQ(s.score, 114);
    CHECK(sol_undo(&s));                        /* 108, less the -2 the clock took after 2S, - 2 */
    CHECK_EQ(s.score, 104);
    CHECK(sol_undo(&s));                        /* 100 - 4 - 2 */
    CHECK_EQ(s.score, 94);
    ticks(&s, 40);                              /* the clock runs on: 92 */
    CHECK(sol_redo(&s));
    CHECK(sol_redo(&s));
    CHECK(sol_redo(&s));
    CHECK_EQ(s.score, 122);                     /* 122 + 4 (XP's refund) - 2 (Undo) - 2 (clock) */
    CHECK(sol_is_won(&s.board) == 0);
    /* a fresh action: its Undo is XP's again */
    ticks(&s, 40);                              /* 120 */
    CHECK(sol_undo(&s));                        /* before the redone 3S: 112 - 2 */
    CHECK_EQ(s.score, 110);
    CHECK(sol_undo(&s));                        /* before the redone 2S: 102; the clock took nothing since */
    CHECK_EQ(s.score, 100);
    /* the floor: the kept penalties never take it below 0 */
    sol_command(&s, SOL_CMD_DEAL);
    board(&s, SOL_WASTE, "AS", T(0), "2S", -1);
    s.score = 0;
    sol_press(&s, SOL_MISS, -1, 0);
    sol_dblclick(&s, SOL_WASTE, 0, 0);          /* 10 */
    ticks(&s, 160);                             /* 2 */
    sol_dblclick(&s, T(0), 0, 0);               /* 12 */
    CHECK(sol_undo(&s));                        /* 2 - 2 */
    CHECK_EQ(s.score, 0);
    CHECK(sol_undo(&s));                        /* 0 - 8 -> 0, - 2 -> 0 */
    CHECK_EQ(s.score, 0);
    sol_free(&s);

    /* Vegas: the clock takes nothing, Undo restores the score exactly (unchanged) */
    start(&s, &f, &r, 0x1B, 1);
    board(&s, SOL_WASTE, "AS", T(0), "2S", -1);
    s.score = -52;
    sol_press(&s, SOL_MISS, -1, 0);
    sol_dblclick(&s, SOL_WASTE, 0, 0);
    ticks(&s, 80);
    sol_dblclick(&s, T(0), 0, 0);
    CHECK_EQ(s.score, -42);
    sol_undo(&s);
    sol_undo(&s);
    CHECK_EQ(s.score, -52);
    sol_free(&s);
}

int main(void)
{
    test_init_deal();
    test_stock();
    test_recycle_limits();
    test_move_scores();
    test_drag();
    test_bugs();
    test_dblclick_autoplay();
    test_undo_redo();
    test_keyboard();
    test_clock();
    test_win();
    test_options();
    test_key_drop_target();
    test_undo_clock();
    return test_summary("test_sol_session");
}
