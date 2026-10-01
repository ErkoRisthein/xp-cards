/*
 * Solitaire HD — native tests of the full-information solver (src/solitaire/solver.c) and the
 * winnable-seed table (src/solitaire/winnable_seeds.h), a fast sample: sol_solve_apply against the
 * session's draw / recycle / turn / drag rules; synthetic positions (won, a sure win, a dead end, invalid
 * boards); XP deals in all four rule cases solved and every solution replayed through the session API
 * (sol_press, sol_begin_drag, sol_drop) to a win, the boards compared after every action; the pass limit;
 * a solve from the middle of a game; the move filters and dead-end tests against the plain search;
 * determinism, cancellation and the node budget; the table's counts and lookups, and its verdicts
 * against the solver's on a sample. The full sweep is `make seed-tables`.
 */
#define SOL_SOLVER_TUNING                /* sol_solver_filters */
#include "sol_test.h"
#include "solitaire/solver.h"
#include "solitaire/winnable_seeds.h"

static const int case_draw[SOL_SEEDS_CASES] = { 1, 3, 1, 3 };
static const int case_scoring[SOL_SEEDS_CASES] = { SOL_SCORING_STANDARD, SOL_SCORING_STANDARD, SOL_SCORING_VEGAS,
                                                   SOL_SCORING_VEGAS };

/* ---- replaying a solution through the session ------------------------------------------------------ */

static int play_action(SolSession *s, const SolSolveMove *m)
{
    switch (m->kind) {
    case SOL_SM_DRAW:
    case SOL_SM_RECYCLE: return sol_press(s, SOL_STOCK, 0, 0) == SOL_PRESS_DONE;
    case SOL_SM_TURN:    return sol_press(s, m->src, m->index, 0) == SOL_PRESS_DONE;
    case SOL_SM_MOVE:    return sol_begin_drag(s, m->src, m->index) && sol_drop(s, m->dst);
    }
    return 0;
}

/* Play moves[from..n) on session s; after each the board must equal sol_solve_apply's, and the game must
 * be won by the last action exactly. Returns 1 if so (failures are printed). */
static int replay_on(SolSession *s, const SolSolveMove *m, int from, int n, const char *what)
{
    SolBoard shadow = s->board;
    for (int i = from; i < n; i++) {
        if (s->won || !s->dealt) { printf("  %s: won before action %d of %d\n", what, i, n); return 0; }
        if (!play_action(s, &m[i])) {
            printf("  %s: action %d (kind %d %d.%d -> %d, n %d) refused\n", what, i, m[i].kind, m[i].src, m[i].index,
                   m[i].dst, m[i].n);
            return 0;
        }
        if (!sol_solve_apply(&shadow, &m[i], s->draw) || !sol_board_equal(&shadow, &s->board)) {
            printf("  %s: boards differ after action %d\n", what, i);
            return 0;
        }
    }
    if (!s->won || s->forced_win) { printf("  %s: not won after %d actions\n", what, n); return 0; }
    return 1;
}

/* A session with the case's Options, dealt seed. */
static void session_for(SolSession *s, Fake *f, Reg *r, int scase, unsigned seed)
{
    SolOptions o;
    sol_options_unpack(&o, SOL_OPTIONS_DEFAULT);
    o.draw = case_draw[scase];
    o.scoring = case_scoring[scase];
    start(s, f, r, sol_options_pack(&o), (int)seed);
}

static int case_left(int scase) { return sol_solve_recycles_left(case_scoring[scase], case_draw[scase], 0); }

/* ---- sol_solve_apply and the pass limit ------------------------------------------------------------ */

static SolSolveMove mv(int kind, int src, int index, int dst, int n, int card)
{
    SolSolveMove m = { (uint8_t)kind, (uint8_t)src, (uint8_t)index, (uint8_t)dst, (uint8_t)n, (uint8_t)card, 0, 0 };
    return m;
}

static void test_apply(void)
{
    SolBoard b;
    sol_board_clear(&b);
    set_pile(&b, SOL_STOCK, "#2C #3C #4C #5C");             /* 5C on top */
    set_pile(&b, SOL_TAB0, "#KH 6D");
    set_pile(&b, SOL_TAB0 + 1, "7S");
    set_pile(&b, SOL_FOUND0, "AC");
    SolBoard b0 = b;

    /* draw three: the packet turned over, the third card from the top ends on top */
    CHECK(!sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_DRAW, SOL_STOCK, 0, SOL_WASTE, 3, sol_card_id(C("5C")), 0, 0 }, 3));
    CHECK(sol_board_equal(&b, &b0));
    SolSolveMove d3 = mv(SOL_SM_DRAW, SOL_STOCK, 0, SOL_WASTE, 3, sol_card_id(C("3C")));
    CHECK(sol_solve_apply(&b, &d3, 3));
    CHECK(pile_is(&b, SOL_WASTE, "5C 4C 3C"));
    CHECK(pile_is(&b, SOL_STOCK, "#2C"));
    /* the last packet is smaller; then the stock is empty */
    CHECK(!sol_solve_apply(&b, &d3, 3));
    CHECK(sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_DRAW, SOL_STOCK, 0, SOL_WASTE, 1, sol_card_id(C("2C")), 0, 0 }, 3));
    CHECK(pile_is(&b, SOL_WASTE, "5C 4C 3C 2C"));
    CHECK(!sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_DRAW, SOL_STOCK, 0, SOL_WASTE, 1, sol_card_id(C("2C")), 0, 0 }, 3));
    /* recycle: the waste turned back in the same order; the card is the new stock top */
    CHECK(!sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_RECYCLE, SOL_STOCK, 0, SOL_STOCK, 4, sol_card_id(C("2C")), 0, 0 }, 3));
    CHECK(sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_RECYCLE, SOL_STOCK, 0, SOL_STOCK, 4, sol_card_id(C("5C")), 0, 0 }, 3));
    CHECK(sol_board_equal(&b, &b0));
    CHECK(!sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_RECYCLE, SOL_STOCK, 0, SOL_STOCK, 4, sol_card_id(C("5C")), 0, 0 }, 3));
    /* draw one */
    CHECK(sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_DRAW, SOL_STOCK, 0, SOL_WASTE, 1, sol_card_id(C("5C")), 0, 0 }, 1));
    CHECK(pile_is(&b, SOL_WASTE, "5C"));
    /* moves: n and card must match, the session's drop rules apply */
    CHECK(!sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_MOVE, SOL_WASTE, 0, SOL_TAB0 + 1, 1, sol_card_id(C("5C")), 0, 0 }, 1));
    CHECK(!sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_MOVE, SOL_WASTE, 0, SOL_TAB0, 1, sol_card_id(C("4C")), 0, 0 }, 1));
    CHECK(!sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_MOVE, SOL_TAB0, 1, SOL_TAB0 + 1, 1, sol_card_id(C("5C")), 0, 0 }, 1));
    CHECK(!sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_MOVE, SOL_TAB0, 1, SOL_TAB0 + 1, 2, sol_card_id(C("6D")), 0, 0 }, 1));
    CHECK(!sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_MOVE, SOL_TAB0, 0, SOL_TAB0 + 1, 2, sol_card_id(C("KH")), 0, 0 }, 1));
    CHECK(sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_MOVE, SOL_TAB0, 1, SOL_TAB0 + 1, 1, sol_card_id(C("6D")), 0, 0 }, 1));
    CHECK(pile_is(&b, SOL_TAB0 + 1, "7S 6D"));
    CHECK(sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_MOVE, SOL_WASTE, 0, SOL_TAB0 + 1, 1, sol_card_id(C("5C")), 0, 0 }, 1));
    CHECK(pile_is(&b, SOL_TAB0 + 1, "7S 6D 5C"));
    /* turn: the face-down top only; a face-up one (the auto-turn extra got there first) changes nothing */
    CHECK(!sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_TURN, SOL_TAB0, 0, SOL_TAB0, 1, sol_card_id(C("QH")), 0, 0 }, 1));
    CHECK(sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_TURN, SOL_TAB0, 0, SOL_TAB0, 1, sol_card_id(C("KH")), 0, 0 }, 1));
    CHECK(pile_is(&b, SOL_TAB0, "KH"));
    SolBoard b1 = b;
    CHECK(sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_TURN, SOL_TAB0, 0, SOL_TAB0, 1, sol_card_id(C("KH")), 0, 0 }, 1));
    CHECK(sol_board_equal(&b, &b1));
    CHECK(!sol_solve_apply(&b, &(SolSolveMove){ SOL_SM_TURN, SOL_WASTE, 0, SOL_WASTE, 1, 0, 0, 0 }, 1));
    CHECK(!sol_solve_apply(&b, &(SolSolveMove){ 9, 0, 0, 0, 0, 0, 0, 0 }, 1));

    /* XP's pass limit as recycles */
    CHECK_EQ(sol_solve_recycles_left(SOL_SCORING_STANDARD, 1, 5), -1);
    CHECK_EQ(sol_solve_recycles_left(SOL_SCORING_NONE, 3, 0), -1);
    CHECK_EQ(sol_solve_recycles_left(SOL_SCORING_VEGAS, 1, 0), 0);
    CHECK_EQ(sol_solve_recycles_left(SOL_SCORING_VEGAS, 3, 0), 2);
    CHECK_EQ(sol_solve_recycles_left(SOL_SCORING_VEGAS, 3, 2), 0);
    CHECK_EQ(sol_solve_recycles_left(SOL_SCORING_VEGAS, 3, 7), 0);
}

/* ---- synthetic positions ------------------------------------------------------------------------- */

static void test_synthetic(SolSolver *sv)
{
    SolSolveResult r;
    SolBoard b;

    /* won: nothing to do */
    sol_board_clear(&b);
    set_pile(&b, SOL_FOUND0, "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC QC KC");
    set_pile(&b, SOL_FOUND0 + 1, "AD 2D 3D 4D 5D 6D 7D 8D 9D TD JD QD KD");
    set_pile(&b, SOL_FOUND0 + 2, "AH 2H 3H 4H 5H 6H 7H 8H 9H TH JH QH KH");
    set_pile(&b, SOL_FOUND0 + 3, "AS 2S 3S 4S 5S 6S 7S 8S 9S TS JS QS KS");
    CHECK_EQ(sol_solve(sv, &b, 1, -1, NULL, &r), SOL_SOLVE_SOLVED);
    CHECK_EQ(r.nmoves, 0);

    /* a sure win: kings, queens and jacks out, one buried under a face-down card, the rest in the
     * stock (draw three, Vegas: two recycles) */
    sol_board_clear(&b);
    set_pile(&b, SOL_FOUND0, "AC 2C 3C 4C 5C 6C 7C 8C 9C TC");
    set_pile(&b, SOL_FOUND0 + 1, "AD 2D 3D 4D 5D 6D 7D 8D 9D TD");
    set_pile(&b, SOL_FOUND0 + 2, "AH 2H 3H 4H 5H 6H 7H 8H 9H TH");
    set_pile(&b, SOL_FOUND0 + 3, "AS 2S 3S 4S 5S 6S 7S 8S 9S TS");
    set_pile(&b, SOL_TAB0 + 2, "#JC KD");
    set_pile(&b, SOL_TAB0 + 4, "KS QH");
    set_pile(&b, SOL_STOCK, "#QS #JS #KC #JD #QD #JH #KH #QC");
    SolBoard start = b;
    CHECK_EQ(sol_solve(sv, &b, 3, 2, NULL, &r), SOL_SOLVE_SOLVED);
    CHECK(sol_board_equal(&b, &start));                     /* the caller's board is not touched */
    int turns = 0, forced = 0, recycles = 0;
    for (int i = 0; i < r.nmoves; i++) {
        CHECK(sol_solve_apply(&b, &r.moves[i], 3));
        turns += r.moves[i].kind == SOL_SM_TURN;
        recycles += r.moves[i].kind == SOL_SM_RECYCLE;
        forced += r.moves[i].forced;
    }
    CHECK(sol_is_won(&b));
    CHECK_EQ(turns, 1);
    CHECK(recycles <= 2);
    CHECK(forced > 0);

    /* draw one with a single pass: still a win (each card has a place to go when it comes up) */
    b = start;
    CHECK_EQ(sol_solve(sv, &b, 1, 0, NULL, &r), SOL_SOLVE_SOLVED);
    for (int i = 0; i < r.nmoves; i++) CHECK(sol_solve_apply(&b, &r.moves[i], 1));
    CHECK(sol_is_won(&b));
    for (int i = 0; i < r.nmoves; i++) CHECK(r.moves[i].kind != SOL_SM_RECYCLE);

    /* a dead end: black suits home, the red aces face down under red cards (red never goes on red) */
    sol_board_clear(&b);
    set_pile(&b, SOL_FOUND0, "AC 2C 3C 4C 5C 6C 7C 8C 9C TC JC QC KC");
    set_pile(&b, SOL_FOUND0 + 1, "AS 2S 3S 4S 5S 6S 7S 8S 9S TS JS QS KS");
    set_pile(&b, SOL_TAB0, "#AD #AH 3D");
    set_pile(&b, SOL_TAB0 + 1, "#2D #2H #4D 3H");
    set_pile(&b, SOL_TAB0 + 2, "#5D #4H 6D");
    set_pile(&b, SOL_TAB0 + 3, "#5H #7D 6H");
    set_pile(&b, SOL_TAB0 + 4, "#8D #7H 8H");
    set_pile(&b, SOL_TAB0 + 5, "#9D #9H #TD TH");
    set_pile(&b, SOL_TAB0 + 6, "#JD #JH #QD #QH #KD KH");
    CHECK(sol_board_valid(&b, NULL, 0));
    CHECK_EQ(sol_solve(sv, &b, 1, -1, NULL, &r), SOL_SOLVE_UNSOLVABLE);
    CHECK_EQ(r.nmoves, 0);
    CHECK(r.moves == NULL);

    /* not Klondike positions */
    SolBoard bad = b;
    bad.p[SOL_TAB0].c[0] = C("#3D");                        /* 3D twice */
    CHECK_EQ(sol_solve(sv, &bad, 1, -1, NULL, &r), SOL_SOLVE_INVALID);
    CHECK_EQ(sol_solve(sv, &b, 3, 8, NULL, &r), SOL_SOLVE_INVALID);   /* recycle budgets 0..7 */
    sol_board_clear(&bad);
    set_pile(&bad, SOL_TAB0, "KS");
    fill_stock(&bad);                                       /* a 51-card stock */
    CHECK_EQ(sol_solve(sv, &bad, 3, -1, NULL, &r), SOL_SOLVE_INVALID);
    CHECK_EQ(sol_solve(NULL, &b, 1, -1, NULL, &r), SOL_SOLVE_GAVE_UP);

    /* one follow-up that needs two preparing moves (a mid-game position of random play, draw one,
     * unlimited passes): 4S can go home only once 3S (the waste's top) is home and 3D is off it (onto
     * 4C); neither preparing move lets 4S follow at once, so the move filters generate neither and
     * their search ends without a win. That is no proof: the check over every move finds the win
     * (the "can't be won" warning was shown for a winnable game). */
    sol_board_clear(&b);
    set_pile(&b, SOL_STOCK, "#7C #5D #6D #QS");
    set_pile(&b, SOL_WASTE, "QC KC 9S 2D JC KD 3S");
    set_pile(&b, SOL_FOUND0, "AS 2S");
    set_pile(&b, SOL_FOUND0 + 1, "AC 2C");
    set_pile(&b, SOL_FOUND0 + 3, "AH 2H");
    set_pile(&b, SOL_TAB0, "KS QD JS");
    set_pile(&b, SOL_TAB0 + 1, "9C 8H 7S 6H 5C 4D");
    set_pile(&b, SOL_TAB0 + 2, "JH TC");
    set_pile(&b, SOL_TAB0 + 3, "#3C #5S 4C");
    set_pile(&b, SOL_TAB0 + 4, "#TH #AD #3H QH");
    set_pile(&b, SOL_TAB0 + 5, "#7H #9H #8D 6C");
    set_pile(&b, SOL_TAB0 + 6, "#TD #4H #KH #8S JD TS 9D 8C 7D 6S 5H 4S 3D");
    CHECK(sol_board_valid(&b, NULL, 0));
    CHECK_EQ(sol_solve(sv, &b, 1, -1, NULL, &r), SOL_SOLVE_SOLVED);
    for (int i = 0; i < r.nmoves; i++) CHECK(sol_solve_apply(&b, &r.moves[i], 1));
    CHECK(sol_is_won(&b));
}

/* ---- XP deals, through the session --------------------------------------------------------------- */

static int solved_by_case[SOL_SEEDS_CASES], unsolvable_by_case[SOL_SEEDS_CASES];

static void test_deals(SolSolver *sv)
{
    static const unsigned seeds[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 11, 12, 13, 16, 17, 19, 20, 21, 24, 25, 27,
                                      28172, 27937, 28481, 32767, 37 };
    const int nseeds = (int)(sizeof seeds / sizeof seeds[0]);
    for (int c = 0; c < SOL_SEEDS_CASES; c++)
        for (int k = 0; k < nseeds; k++) {
            unsigned seed = seeds[k];
            SolBoard b;
            sol_deal_board(&b, seed, NULL);
            SolSolveResult r;
            int st = sol_solve(sv, &b, case_draw[c], case_left(c), NULL, &r);
            CHECK(st == SOL_SOLVE_SOLVED || st == SOL_SOLVE_UNSOLVABLE || st == SOL_SOLVE_GAVE_UP);
            /* the table never contradicts the solver */
            int t = sol_seed_status(c, seed);
            if (st == SOL_SOLVE_SOLVED) CHECK(t != SOL_SEED_UNSOLVABLE);
            if (st == SOL_SOLVE_UNSOLVABLE) CHECK(t != SOL_SEED_WINNABLE);
            if (st == SOL_SOLVE_UNSOLVABLE) unsolvable_by_case[c]++;
            if (st != SOL_SOLVE_SOLVED) continue;
            solved_by_case[c]++;
            CHECK(r.nmoves > 52 && r.moves != NULL);
            int recycles = 0;
            for (int i = 0; i < r.nmoves; i++) recycles += r.moves[i].kind == SOL_SM_RECYCLE;
            if (case_scoring[c] == SOL_SCORING_VEGAS) CHECK(recycles <= case_draw[c] - 1);
            SolSession s;
            Fake f;
            Reg reg;
            char what[64];
            snprintf(what, sizeof what, "case %d seed %u", c, seed);
            session_for(&s, &f, &reg, c, seed);
            CHECK(replay_on(&s, r.moves, 0, r.nmoves, what));
            CHECK_EQ(f.ncascade, 1);
            if (case_scoring[c] == SOL_SCORING_VEGAS) CHECK_EQ(s.score, -52 + 52 * 5);
            sol_free(&s);
        }
    /* with this small budget (60000 nodes) most of these deals are solved in the unlimited cases (18 and
     * 18 of 25 when written), fewer with Vegas's passes (3 and 8, and 11 and 5 proven unsolvable); #37
     * is a quick proof in draw one */
    CHECK(solved_by_case[SOL_SEEDS_DRAW1] >= 15);
    CHECK(solved_by_case[SOL_SEEDS_DRAW3] >= 15);
    CHECK(solved_by_case[SOL_SEEDS_DRAW1_VEGAS] >= 2);
    CHECK(solved_by_case[SOL_SEEDS_DRAW3_VEGAS] >= 6);
    CHECK(unsolvable_by_case[SOL_SEEDS_DRAW1] >= 1);
    CHECK(unsolvable_by_case[SOL_SEEDS_DRAW1_VEGAS] >= 5);
    printf("  sample solved per case: %d %d %d %d, unsolvable: %d %d %d %d\n", solved_by_case[0], solved_by_case[1],
           solved_by_case[2], solved_by_case[3], unsolvable_by_case[0], unsolvable_by_case[1],
           unsolvable_by_case[2], unsolvable_by_case[3]);
}

/* From the middle of a game: follow a solution part of the way through the session, then solve the
 * session's board (its recycles count against the Vegas limit) and finish with the new solution. */
static void test_midgame(SolSolver *sv)
{
    for (int c = 0; c < SOL_SEEDS_CASES; c++) {
        static const unsigned candidates[] = { 1, 0, 5, 7, 101, 109 };
        unsigned seed = 0;
        SolBoard b;
        SolSolveResult r;
        int found = 0;
        for (int k = 0; k < 6 && !found; k++) {
            seed = candidates[k];
            sol_deal_board(&b, seed, NULL);
            found = sol_solve(sv, &b, case_draw[c], case_left(c), NULL, &r) == SOL_SOLVE_SOLVED;
        }
        CHECK(found);
        if (!found) continue;
        int half = r.nmoves / 2;
        SolSolveMove *first = malloc(sizeof *first * (size_t)r.nmoves);
        memcpy(first, r.moves, sizeof *first * (size_t)r.nmoves);
        SolSession s;
        Fake f;
        Reg reg;
        session_for(&s, &f, &reg, c, seed);
        for (int i = 0; i < half; i++) CHECK(play_action(&s, &first[i]));
        int left = sol_solve_recycles_left(s.opts.scoring, s.draw, s.recycles);
        CHECK_EQ(sol_solve(sv, &s.board, s.draw, left, NULL, &r), SOL_SOLVE_SOLVED);
        CHECK(replay_on(&s, r.moves, 0, r.nmoves, "midgame"));
        sol_free(&s);
        free(first);
    }
    /* a Vegas draw-three game that has used its three passes */
    SolSession s;
    Fake f;
    Reg reg;
    session_for(&s, &f, &reg, SOL_SEEDS_DRAW3_VEGAS, 1);
    for (int pass = 0; pass < 3; pass++) {
        while (s.board.p[SOL_STOCK].n) sol_press(&s, SOL_STOCK, 0, 0);
        if (pass < 2) CHECK_EQ(sol_press(&s, SOL_STOCK, 0, 0), SOL_PRESS_DONE);   /* recycle */
    }
    CHECK_EQ(sol_press(&s, SOL_STOCK, 0, 0), SOL_PRESS_NONE);
    CHECK_EQ(s.recycles, 2);
    CHECK_EQ(sol_stock_symbol(&s), SOL_STOCK_X);
    SolSolveResult r;
    int st = sol_solve(sv, &s.board, 3, sol_solve_recycles_left(s.opts.scoring, 3, s.recycles), NULL, &r);
    CHECK(st == SOL_SOLVE_SOLVED || st == SOL_SOLVE_UNSOLVABLE);
    if (st == SOL_SOLVE_SOLVED) {
        for (int i = 0; i < r.nmoves; i++) CHECK(r.moves[i].kind != SOL_SM_RECYCLE);
        CHECK(replay_on(&s, r.moves, 0, r.nmoves, "no passes left"));
    }
    sol_free(&s);
}

/* The move filters and dead-end tests (solver.h) never change a verdict: deals that both searches decide
 * within a small budget, among them proofs of every kind. */
static void test_filters(void)
{
    static const struct { int scase; unsigned seed; int status; } known[] = {
        { 0, 37, SOL_SOLVE_UNSOLVABLE }, { 0, 55, SOL_SOLVE_UNSOLVABLE }, { 1, 40, SOL_SOLVE_UNSOLVABLE },
        { 1, 115, SOL_SOLVE_UNSOLVABLE }, { 1, 106, SOL_SOLVE_UNSOLVABLE }, { 2, 6, SOL_SOLVE_UNSOLVABLE },
        { 2, 55, SOL_SOLVE_UNSOLVABLE }, { 2, 38, SOL_SOLVE_UNSOLVABLE }, { 2, 8, SOL_SOLVE_UNSOLVABLE },
        { 3, 8, SOL_SOLVE_UNSOLVABLE }, { 3, 55, SOL_SOLVE_UNSOLVABLE }, { 3, 31, SOL_SOLVE_UNSOLVABLE },
        { 3, 6, SOL_SOLVE_UNSOLVABLE }, { 0, 3, SOL_SOLVE_SOLVED }, { 1, 3, SOL_SOLVE_SOLVED },
        { 2, 62, SOL_SOLVE_SOLVED }, { 3, 5, SOL_SOLVE_SOLVED },
    };
    SolSolver *filtered = sol_solver_new(20000), *plain = sol_solver_new(20000);
    CHECK(filtered && plain);
    if (!filtered || !plain) { sol_solver_free(filtered); sol_solver_free(plain); return; }
    sol_solver_filters(plain, 0);
    for (size_t k = 0; k < sizeof known / sizeof known[0]; k++) {
        SolBoard b;
        SolSolveResult r1, r2;
        int c = known[k].scase;
        sol_deal_board(&b, known[k].seed, NULL);
        CHECK_EQ(sol_solve(filtered, &b, case_draw[c], case_left(c), NULL, &r1), known[k].status);
        CHECK_EQ(sol_solve(plain, &b, case_draw[c], case_left(c), NULL, &r2), known[k].status);
        if (known[k].status == SOL_SOLVE_UNSOLVABLE) CHECK(r1.nodes <= r2.nodes);   /* a subset */
    }
    /* and wherever the plain search decides a deal within the budget */
    int compared = 0;
    for (int c = 0; c < SOL_SEEDS_CASES; c++)
        for (unsigned seed = 100; seed < 110; seed++) {
            SolBoard b;
            SolSolveResult r1, r2;
            sol_deal_board(&b, seed, NULL);
            if (sol_solve(plain, &b, case_draw[c], case_left(c), NULL, &r2) == SOL_SOLVE_GAVE_UP) continue;
            sol_solve(filtered, &b, case_draw[c], case_left(c), NULL, &r1);
            CHECK_EQ(r1.status, r2.status);
            compared++;
        }
    CHECK(compared >= 10);
    sol_solver_free(filtered);
    sol_solver_free(plain);
}

/* ---- determinism, cancellation, budget, memory ------------------------------------------------------ */

static void test_engine(SolSolver *sv)
{
    SolBoard b;
    sol_deal_board(&b, 8, NULL);                            /* draw three: about 4000 nodes */
    SolSolveResult r1, r2;
    CHECK_EQ(sol_solve(sv, &b, 3, -1, NULL, &r1), SOL_SOLVE_SOLVED);
    int n = r1.nmoves;
    SolSolveMove *copy = malloc(sizeof *copy * (size_t)n);
    memcpy(copy, r1.moves, sizeof *copy * (size_t)n);
    uint32_t nodes = r1.nodes;
    SolSolver *other = sol_solver_new(0);
    CHECK(other != NULL);
    CHECK_EQ(sol_solve(other, &b, 3, -1, NULL, &r2), SOL_SOLVE_SOLVED);
    CHECK_EQ(r2.nmoves, n);
    CHECK_EQ(r2.nodes, nodes);
    CHECK(!memcmp(r2.moves, copy, sizeof *copy * (size_t)n));
    CHECK_EQ(sol_solve(sv, &b, 3, -1, NULL, &r1), SOL_SOLVE_SOLVED);
    CHECK(r1.nmoves == n && !memcmp(r1.moves, copy, sizeof *copy * (size_t)n));
    free(copy);

    volatile int cancel = 1;
    sol_deal_board(&b, 10, NULL);
    CHECK_EQ(sol_solve(other, &b, 1, -1, &cancel, &r2), SOL_SOLVE_CANCELLED);
    CHECK_EQ(r2.expanded, 0);
    cancel = 0;

    SolSolver *tiny = sol_solver_new(16);
    CHECK(tiny != NULL);
    CHECK_EQ(sol_solve(tiny, &b, 1, -1, &cancel, &r2), SOL_SOLVE_GAVE_UP);
    CHECK(r2.nodes <= 16);
    CHECK(sol_solver_memory(tiny) < sol_solver_memory(other));
    CHECK(sol_solver_memory(other) >= (size_t)SOL_SOLVER_DEFAULT_NODES * SOL_SOLVER_NODE_BYTES);
    CHECK(sol_solver_memory(other) < (size_t)SOL_SOLVER_DEFAULT_NODES * 72);
    sol_solver_free(tiny);
    sol_solver_free(other);
    sol_solver_free(NULL);
    CHECK_EQ(sol_solver_memory(NULL), 0);
}

/* ---- the table ---------------------------------------------------------------------------------- */

static void test_table(void)
{
    CHECK_EQ(sol_seeds_case(1, SOL_SCORING_STANDARD), SOL_SEEDS_DRAW1);
    CHECK_EQ(sol_seeds_case(1, SOL_SCORING_NONE), SOL_SEEDS_DRAW1);
    CHECK_EQ(sol_seeds_case(3, SOL_SCORING_NONE), SOL_SEEDS_DRAW3);
    CHECK_EQ(sol_seeds_case(1, SOL_SCORING_VEGAS), SOL_SEEDS_DRAW1_VEGAS);
    CHECK_EQ(sol_seeds_case(3, SOL_SCORING_VEGAS), SOL_SEEDS_DRAW3_VEGAS);
    CHECK_EQ(sol_seed_status(-1, 0), SOL_SEED_UNKNOWN);
    CHECK_EQ(sol_seed_status(SOL_SEEDS_CASES, 0), SOL_SEED_UNKNOWN);
    for (int c = 0; c < SOL_SEEDS_CASES; c++) {
        uint32_t cnt[4] = { 0, 0, 0, 0 };
        for (unsigned s = 0; s < SOL_SEEDS; s++) cnt[sol_seed_status(c, s)]++;
        CHECK_EQ(cnt[3], 0);
        for (int k = 0; k < 3; k++) CHECK_EQ(cnt[k], sol_seed_counts[c][k]);
        CHECK_EQ(sol_seed_status(c, 5 + SOL_SEEDS), sol_seed_status(c, 5));
        /* the next winnable seed: at or after, wrapping; every seed in between is not winnable */
        if (!cnt[SOL_SEED_WINNABLE]) {
            CHECK_EQ(sol_seed_next_winnable(c, 1234), 1234);
            continue;
        }
        unsigned probes[] = { 0, 1, 12345, SOL_SEEDS - 1, SOL_SEEDS + 7 };
        for (int k = 0; k < 5; k++) {
            unsigned p = probes[k] & (SOL_SEEDS - 1), w = sol_seed_next_winnable(c, probes[k]);
            CHECK(w < SOL_SEEDS);
            CHECK_EQ(sol_seed_status(c, w), SOL_SEED_WINNABLE);
            for (unsigned s = p; s != w; s = (s + 1) & (SOL_SEEDS - 1)) {
                if (sol_seed_status(c, s) == SOL_SEED_WINNABLE) { CHECK(0); break; }
            }
        }
    }
    printf("  table: %s\n", sol_seed_table_info);
    for (int c = 0; c < SOL_SEEDS_CASES; c++)
        printf("  table case %d: %u winnable, %u unsolvable, %u unknown\n", c, sol_seed_counts[c][SOL_SEED_WINNABLE],
               sol_seed_counts[c][SOL_SEED_UNSOLVABLE], sol_seed_counts[c][SOL_SEED_UNKNOWN]);
}

int main(void)
{
    SolSolver *sv = sol_solver_new(60000);
    CHECK(sv != NULL);
    if (!sv) return test_summary("test_sol_solver");
    test_apply();
    test_synthetic(sv);
    test_deals(sv);
    test_midgame(sv);
    test_filters();
    test_engine(sv);
    test_table();
    sol_solver_free(sv);
    return test_summary("test_sol_solver");
}
