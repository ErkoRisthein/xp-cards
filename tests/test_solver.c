/*
 * FreeCell HD — native tests of the solver (src/core/solver.c). Every solution is replayed through the
 * session (fcs_click, scripted MoveCol answers) and must win; the move set is compared with every click
 * pair the session accepts; UNSOLVABLE results are cross-checked by independent exhaustive searches.
 * The full 1..32000 sweep is `make solver-bench` (tests/solver_bench.c).
 */
#include "core/game.h"
#include "core/session.h"
#include "core/solver.h"
#include "solver_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (a), _b = (b); checks++; if (_a != _b) { fails++; \
    printf("FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

static uint64_t trng = 1;                 /* the tests' own generator (same on every platform) */
static void tsrand(unsigned seed) { trng = seed; }
static int trand(int n)
{
    trng = trng * 6364136223846793005u + 1442695040888963407u;
    return (int)((trng >> 33) % (uint64_t)n);
}

/* ---- board helpers ------------------------------------------------------------------------------ */

static Card C(const char *s)
{
    const char *ranks = "A23456789TJQK", *suits = "CDHS";
    return (int)(strchr(ranks, s[0]) - ranks) * 4 + (int)(strchr(suits, s[1]) - suits);
}

/* cols: 8 strings of cards deepest first ("KD QD 5D"); fcs: 4 cards or NULL; homes: top card per suit
 * C D H S ("9C" or NULL = empty); home slots 4..7 in that order. */
static void make_board(FcBoard *b, const char *const cols[8], const char *const fcs[4], const char *const homes[4])
{
    fc_board_clear(b);
    int home = 0, k = 0;
    for (int s = 0; s < 4; s++)
        if (homes && homes[s]) {
            Card c = C(homes[s]);
            b->home_rank[fc_suit(c)] = (int8_t)fc_rank(c);
            b->suit_home_slot[fc_suit(c)] = (int8_t)(4 + k);
            b->board[0][4 + k++] = c;
            home += fc_rank(c) + 1;
        }
    for (int i = 0; i < 4; i++) b->board[0][i] = fcs && fcs[i] ? C(fcs[i]) : FC_EMPTY;
    for (int c = 1; c <= 8; c++) {
        const char *p = cols[c - 1] ? cols[c - 1] : "";
        int i = 0;
        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;
            b->board[c][i++] = C(p);
            p += 2;
        }
    }
    b->cards_left = 52 - home;
}

/* A canonical key written independently of solver.c: home ranks, free cells sorted, columns sorted
 * by their whole contents. */
typedef struct Key { uint8_t k[72]; } Key;

static int cmp_col(const void *a, const void *b) { return memcmp(a, b, 25); }
static int cmp_u8(const void *a, const void *b) { return *(const uint8_t *)a - *(const uint8_t *)b; }

static void key_of(const FcBoard *b, Key *key)
{
    uint8_t cols[8][25], fc[4];
    memset(cols, 0xFF, sizeof cols);
    for (int c = 1; c <= 8; c++)
        for (int i = 0; i <= fc_last_index(b, c); i++) cols[c - 1][i] = (uint8_t)b->board[c][i];
    qsort(cols, 8, 25, cmp_col);
    for (int i = 0; i < 4; i++) fc[i] = b->board[0][i] == FC_EMPTY ? 0xFF : (uint8_t)b->board[0][i];
    qsort(fc, 4, 1, cmp_u8);
    memset(key, 0, sizeof *key);
    int p = 0;
    for (int s = 0; s < 4; s++) key->k[p++] = (uint8_t)(b->home_rank[s] + 1);
    for (int i = 0; i < 4; i++) key->k[p++] = fc[i];
    for (int c = 0; c < 8; c++)
        for (int i = 0; i < 25 && cols[c][i] != 0xFF; i++) key->k[p++] = (uint8_t)(cols[c][i] | (i == 0 ? 0x80 : 0));
}

static int key_eq(const Key *a, const Key *b) { return memcmp(a, b, sizeof *a) == 0; }

/* A plain hash set of keys (tests only). */
typedef struct KeySet { Key *keys; uint8_t *used; uint32_t cap, n; } KeySet;

static uint32_t key_hash(const Key *k)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof k->k; i++) h = (h ^ k->k[i]) * 16777619u;
    return h;
}

static void ks_init(KeySet *s, uint32_t cap)
{
    s->cap = cap;
    s->n = 0;
    s->keys = malloc(cap * sizeof *s->keys);
    s->used = calloc(cap, 1);
}

static void ks_free(KeySet *s) { free(s->keys); free(s->used); }

static int ks_add(KeySet *s, const Key *k)       /* 1 if new, 0 if known, -1 if full */
{
    uint32_t i = key_hash(k) % s->cap;
    while (s->used[i]) {
        if (key_eq(&s->keys[i], k)) return 0;
        i = (i + 1) % s->cap;
    }
    if (s->n * 4 >= s->cap * 3) return -1;
    s->used[i] = 1;
    s->keys[i] = *k;
    s->n++;
    return 1;
}

static int ks_has(const KeySet *s, const Key *k)
{
    uint32_t i = key_hash(k) % s->cap;
    while (s->used[i]) {
        if (key_eq(&s->keys[i], k)) return 1;
        i = (i + 1) % s->cap;
    }
    return 0;
}

/* ---- the session as the move oracle ---------------------------------------------------------------- */

typedef struct Oracle { FcSession s; ReplayUI r; int deal; } Oracle;

static void oracle_init(Oracle *o, int deal)
{
    replay_init(&o->s, &o->r);
    o->deal = deal;
    replay_deal(&o->s, &o->r, deal);
}

/* Every position one click pair (with either MoveCol answer) leads to from pos, played by the session
 * itself; positions equal to pos up to order are left out. Results go into boards[] (deduplicated by
 * key). Returns the count. */
static int session_successors(Oracle *o, const FcBoard *pos, int std, FcBoard *boards, int max)
{
    FcSession *s = &o->s;
    Key self, seen[256];
    int n = 0;
    key_of(pos, &self);
    for (int sc = 0; sc <= 8; sc++)
        for (int sp = 0; sp < (sc == 0 ? 4 : 1); sp++)
            for (int dc = 0; dc <= 8; dc++)
                for (int dp = 0; dp < (dc == 0 ? 8 : 1); dp++)
                    for (int ans = FCS_MOVECOL_COLUMN; ans >= FCS_MOVECOL_SINGLE; ans--) {
                        if (s->game_number == 0) replay_deal(s, &o->r, o->deal);
                        s->board = *pos;
                        s->extras.standard_supermove = std;
                        int src = sc == 0 ? sp : fc_last_index(pos, sc), dst = dc == 0 ? dp : fc_last_index(pos, dc);
                        if (src < 0 && sc != 0) break;
                        fcs_click(s, sc, src);
                        if (!s->sel) break;
                        o->r.answer = ans;
                        int nd = o->r.ndialog;
                        fcs_click(s, dc, dst);
                        int asked = o->r.ndialog != nd;
                        if (s->sel) fcs_click(s, FCS_MISS, 0);         /* (messages on: never kept) */
                        Key k;
                        key_of(&s->board, &k);
                        int known = key_eq(&k, &self);
                        for (int i = 0; i < n && !known; i++) known = key_eq(&seen[i], &k);
                        if (!known && n < max && n < 256) { seen[n] = k; boards[n++] = s->board; }
                        if (fcs_undo_enabled(s)) fcs_command(s, FCS_CMD_UNDO);   /* keep the history short */
                        if (!asked) break;
                    }
    return n;
}

/* Exhaustive search with the session as the move oracle (independent of solver.c). Returns the number
 * of positions reachable from pos (pos included), -1 if one of them is won, -2 if more than max. */
static int oracle_reachable(Oracle *o, const FcBoard *pos, int std, int max)
{
    FcBoard *queue = malloc((size_t)max * sizeof *queue), next[256];
    KeySet seen;
    ks_init(&seen, (uint32_t)max * 2 + 7);
    int head = 0, tail = 0, result = 0;
    Key k;
    key_of(pos, &k);
    ks_add(&seen, &k);
    queue[tail++] = *pos;
    while (head < tail) {
        FcBoard cur = queue[head++];
        int n = session_successors(o, &cur, std, next, 256);
        for (int i = 0; i < n; i++) {
            if (next[i].cards_left == 0) { result = -1; goto out; }
            key_of(&next[i], &k);
            int a = ks_add(&seen, &k);
            if (a < 0 || (a > 0 && tail >= max)) { result = -2; goto out; }
            if (a > 0) queue[tail++] = next[i];
        }
    }
    result = tail;
out:
    ks_free(&seen);
    free(queue);
    return result;
}

/* Exhaustive search over fc_solve_moves / fc_solve_play with the test's own keys (checks the solver's
 * canonical states and search, not its move generator). Same results as oracle_reachable. */
static int moves_reachable(const FcBoard *pos, int std, int max)
{
    FcBoard *stack = malloc((size_t)max * sizeof *stack);
    FcSolveMove mv[FC_SOLVE_MAX_MOVES];
    KeySet seen;
    ks_init(&seen, (uint32_t)max * 2 + 7);
    int top = 0, result;
    Key k;
    key_of(pos, &k);
    ks_add(&seen, &k);
    stack[top++] = *pos;
    for (;;) {
        if (top == 0) { result = (int)seen.n; break; }
        FcBoard cur = stack[--top];
        int n = fc_solve_moves(&cur, std, mv), stop = 0;
        for (int i = 0; i < n && !stop; i++) {
            FcBoard b = cur;
            if (!fc_solve_play(&b, &mv[i], std)) { result = -3; stop = 1; break; }
            if (b.cards_left == 0) { result = -1; stop = 1; break; }
            key_of(&b, &k);
            int a = ks_add(&seen, &k);
            if (a < 0 || (a > 0 && top >= max)) { result = -2; stop = 1; break; }
            if (a > 0) stack[top++] = b;
        }
        if (stop) break;
    }
    ks_free(&seen);
    free(stack);
    return result;
}

/* ---- tests ------------------------------------------------------------------------------------------ */

static int replay_ok(FcSession *s, ReplayUI *r, const FcSolveResult *res, int std, int deal)
{
    int at;
    const char *err = replay_moves(s, r, res->moves, res->nmoves, std, 1, &at);
    if (err) printf("  deal %d: replay failed at move %d of %d: %s\n", deal, at, res->nmoves, err);
    return err == NULL;
}

static void test_sample_deals(FcSolver *sv, int std, int count, int step, int first)
{
    FcSession s;
    ReplayUI r;
    replay_init(&s, &r);
    int solved = 0, replayed = 0, dlg_col = 0, dlg_single = 0, kinds[5] = { 0 };
    long nodes = 0, len = 0;
    for (int i = 0; i < count; i++) {
        int d = first + i * step;
        FcBoard b;
        fc_deal(&b, d);
        FcSolveResult res;
        fc_solve(sv, &b, std, NULL, &res);
        if (res.status != FC_SOLVE_SOLVED) {
            printf("  deal %d (%s rule): status %d after %u nodes\n", d, std ? "standard" : "XP", res.status, res.nodes);
            continue;
        }
        solved++;
        nodes += res.nodes;
        len += res.nmoves;
        for (int k = 0; k < res.nmoves; k++) {
            dlg_col += res.moves[k].movecol == FCS_MOVECOL_COLUMN;
            dlg_single += res.moves[k].movecol == FCS_MOVECOL_SINGLE;
            kinds[res.moves[k].kind]++;
        }
        replay_deal(&s, &r, d);
        replayed += replay_ok(&s, &r, &res, std, d);
    }
    printf("  %s rule: %d/%d deals solved and replayed to a win (%d), %ld nodes, %ld moves\n",
           std ? "standard" : "XP", solved, count, replayed, nodes, len);
    CHECK_EQ(solved, count);
    CHECK_EQ(replayed, solved);
    CHECK(dlg_col > 0);                        /* (test_moves_match_session covers "single card") */
    (void)dlg_single;
    CHECK(kinds[FC_SM_HOME] > 0 && kinds[FC_SM_FREECELL] > 0 && kinds[FC_SM_COLUMN] > 0 && kinds[FC_SM_EMPTY] > 0);
    fcs_free(&s);
}

static void test_unsolvable_deal(FcSolver *sv)
{
    FcBoard b;
    fc_deal(&b, 11982);
    FcSolveResult res;
    for (int std = 0; std <= 1; std++) {
        fc_solve(sv, &b, std, NULL, &res);
        CHECK(res.status == FC_SOLVE_UNSOLVABLE || res.status == FC_SOLVE_GAVE_UP);
        CHECK(res.status != FC_SOLVE_SOLVED);
        printf("  deal 11982 (%s rule): %s after %u nodes\n", std ? "standard" : "XP",
               res.status == FC_SOLVE_UNSOLVABLE ? "unsolvable (proven)" : "gave up", res.nodes);
        if (res.status == FC_SOLVE_UNSOLVABLE)   /* the same reachable set, counted independently */
            CHECK_EQ(moves_reachable(&b, std, 400000), res.nodes);
    }
}

static void test_constructed_unsolvable(FcSolver *sv)
{
    /* No legal move at all (the session would show "you lose"): every free cell holds a king, no column
     * is empty, exposed tens and queens never stack, nines are buried. */
    static const char *const cols0[8] = { "9C TC", "9S TS", "9D TD", "9H TH", "JC QC", "JS QS", "JD QD", "JH QH" };
    static const char *const fcs0[4] = { "KC", "KD", "KH", "KS" };
    static const char *const homes8[4] = { "8C", "8D", "8H", "8S" };
    /* Full free cells and a few moves that all run dead (found by a random search, confirmed below). */
    static const char *const cols1[8] = { "6D 5C 8H JD", "JH 9S 5D TS", "QS 8D 6C 8C", "7D 8S 6S 7C",
                                          "6H 9D 7S TD", "QC TC JC 9C", "9H 5H QD JS", "5S KD 7H TH" };
    static const char *const fcs1[4] = { "QH", "KH", "KC", "KS" };
    static const char *const cols2[8] = { "JH QD 5S QC", "6H 5D 7C 7S", "8D 6C JS JC", "QS 6S TS KH",
                                          "5C 9D 8H JD", "TD 8S KD 8C", "TH 5H 9C 6D", "7D TC 9H 7H" };
    static const char *const fcs2[4] = { "KS", "QH", "9S", "KC" };
    static const char *const homes4[4] = { "4C", "4D", "4H", "4S" };
    struct { const char *const *cols, *const *fcs, *const *homes; int moves; } pos[3] = {
        { cols0, fcs0, homes8, 0 }, { cols1, fcs1, homes4, 5 }, { cols2, fcs2, homes4, 4 } };
    Oracle o;
    oracle_init(&o, 1);
    for (int i = 0; i < 3; i++) {
        FcBoard b;
        make_board(&b, pos[i].cols, pos[i].fcs, pos[i].homes);
        FcSolveMove mv[FC_SOLVE_MAX_MOVES];
        CHECK_EQ(fc_solve_moves(&b, 0, mv), pos[i].moves);
        for (int std = 0; std <= 1; std++) {
            FcSolveResult res;
            fc_solve(sv, &b, std, NULL, &res);
            CHECK_EQ(res.status, FC_SOLVE_UNSOLVABLE);
            int oracle = oracle_reachable(&o, &b, std, 2000);
            CHECK_EQ(oracle, res.nodes);       /* the session finds exactly as many positions, none won */
            CHECK_EQ(moves_reachable(&b, std, 2000), res.nodes);
            if (i == 0) CHECK_EQ(res.expanded, 1);
        }
    }
    fcs_free(&o.s);
}

/* fc_solve_moves against the session: from random positions (fresh deals, points along solutions, a
 * few random moves off them; both rules), the set of positions the session's click pairs reach must
 * equal the set the solver's moves reach, and each solver move must replay through fcs_click exactly. */
static void test_moves_match_session(FcSolver *sv)
{
    Oracle o;
    oracle_init(&o, 1);
    ReplayUI *r = &o.r;
    FcSolveMove mv[FC_SOLVE_MAX_MOVES];
    FcBoard succ[256];
    int positions = 0, total = 0, bad = 0, lost = 0, answers[2] = { 0, 0 };
    tsrand(7);
    for (int t = 0; t < 160; t++) {
        int std = t & 1, deal = 1 + trand(32000), wander = trand(4);
        FcBoard b;
        fc_deal(&b, deal);
        FcSolveResult res;
        if (t % 8 != 0 && fc_solve(sv, &b, std, NULL, &res) == FC_SOLVE_SOLVED) {
            int k = trand(res.nmoves);
            for (int i = 0; i < k; i++) fc_solve_play(&b, &res.moves[i], std);
        }
        for (int k = 0; k < wander && b.cards_left > 0; k++) {
            int n = fc_solve_moves(&b, std, mv);
            if (n == 0) break;
            fc_solve_play(&b, &mv[trand(n)], std);
        }
        if (b.cards_left == 0) continue;
        int n = fc_solve_moves(&b, std, mv);
        KeySet mine;
        ks_init(&mine, 1024);
        Key self, k;
        key_of(&b, &self);
        for (int i = 0; i < n; i++) {
            FcBoard c = b;
            CHECK(fc_solve_play(&c, &mv[i], std));
            key_of(&c, &k);
            CHECK(!key_eq(&k, &self));
            ks_add(&mine, &k);
            /* the move through the session from this very position */
            if (o.s.game_number == 0) replay_deal(&o.s, r, deal);
            o.s.board = b;
            int at;
            const char *err = replay_moves(&o.s, r, &mv[i], 1, std, 0, &at);
            if (mv[i].movecol != FC_SM_NODIALOG) answers[mv[i].movecol == FCS_MOVECOL_COLUMN]++;
            if (err && !strcmp(err, "game lost (no more legal moves)") && fc_count_moves_xp(&c) == 0) {
                lost++;                          /* a legal move into a dead end */
                err = NULL;
            }
            if (err) {
                bad++;
                printf("  deal %d move %d (kind %d): %s\n", deal, i, mv[i].kind, err);
            }
            if (fcs_undo_enabled(&o.s)) fcs_command(&o.s, FCS_CMD_UNDO);
        }
        int ns = session_successors(&o, &b, std, succ, 256), missing = 0;
        for (int i = 0; i < ns; i++) {
            key_of(&succ[i], &k);
            missing += !ks_has(&mine, &k);
        }
        CHECK_EQ(missing, 0);
        CHECK_EQ(ns, (int)mine.n);
        ks_free(&mine);
        positions++;
        total += n;
    }
    CHECK_EQ(bad, 0);
    CHECK(answers[0] > 0 && answers[1] > 0);  /* both MoveCol answers were replayed */
    printf("  %d positions, %d solver moves (%d into a lost position, MoveCol answers: %d single, %d column), "
           "all matching the session's click pairs\n", positions, total, lost, answers[0], answers[1]);
    fcs_free(&o.s);
}

/* Mid-game: solve, play part of the solution through the session, solve again from the session's
 * board (as Hint would), finish with the new solution. Also from random playouts. */
static void test_midgame(FcSolver *sv)
{
    FcSession s;
    ReplayUI r;
    replay_init(&s, &r);
    static const int deals[] = { 1, 617, 1941, 6000, 31465 };
    for (int i = 0; i < 5; i++) {
        for (int std = 0; std <= 1; std++) {
            replay_deal(&s, &r, deals[i]);
            FcSolveResult res;
            fc_solve(sv, &s.board, std, NULL, &res);
            CHECK_EQ(res.status, FC_SOLVE_SOLVED);
            int half = res.nmoves / 2, at;
            FcSolveMove first[256];
            memcpy(first, res.moves, (size_t)half * sizeof *first);
            CHECK(replay_moves(&s, &r, first, half, std, 0, &at) == NULL);
            fc_solve(sv, &s.board, std, NULL, &res);
            CHECK_EQ(res.status, FC_SOLVE_SOLVED);
            CHECK(replay_ok(&s, &r, &res, std, deals[i]));
        }
    }
    FcSolveMove mv[FC_SOLVE_MAX_MOVES];
    int solved = 0, unsolvable = 0, other = 0;
    tsrand(11);
    for (int t = 0; t < 30; t++) {
        int std = t & 1, deal = 1 + trand(32000), steps = 5 + trand(40), at;
        replay_deal(&s, &r, deal);
        for (int k = 0; k < steps && s.board.cards_left > 0 && s.game_number; k++) {
            int n = fc_solve_moves(&s.board, std, mv);
            if (n == 0) break;
            replay_moves(&s, &r, &mv[trand(n)], 1, std, 0, &at);
        }
        if (!s.game_number) continue;            /* the playout won or lost */
        FcSolveResult res;
        fc_solve(sv, &s.board, std, NULL, &res);
        if (res.status == FC_SOLVE_SOLVED) {
            solved++;
            CHECK(replay_ok(&s, &r, &res, std, deal));
        } else if (res.status == FC_SOLVE_UNSOLVABLE) {
            unsolvable++;
        } else {
            other++;
        }
    }
    printf("  random mid-game positions: %d solved and replayed, %d proven unsolvable, %d given up\n", solved,
           unsolvable, other);
    CHECK(solved > 0);
    fcs_free(&s);
}

static void test_sure_win(void)
{
    FcBoard b;
    FcSolveMove mv[52];
    int n = -1;
    fc_deal(&b, 1);
    CHECK_EQ(fc_sure_win(&b, mv, &n), 0);

    /* Autoplay alone stops (the tens are not safe while the reds are at 4), moving any card that can go
     * home finishes: TC home exposes 5D, autoplay takes the diamonds to TD, TS does the rest. */
    static const char *const cols[8] = { "KD QD JD TD 9D 8D 7D 6D 5D TC", "KH QH JH TH 9H 8H 7H 6H 5H TS",
                                         "KC QC JC", "KS QS JS" };
    static const char *const homes[4] = { "9C", "4D", "4H", "9S" };
    make_board(&b, cols, NULL, homes);
    FcBoard t = b;
    fc_autoplay(&t, NULL, 0);
    CHECK_EQ(t.cards_left, b.cards_left);       /* stable: no autoplay */
    CHECK_EQ(fc_sure_win(&b, mv, &n), 1);
    CHECK_EQ(n, 2);
    CHECK_EQ(mv[0].kind, FC_SM_HOME);
    CHECK_EQ(mv[0].card, C("TC"));
    CHECK_EQ(fc_sure_win(&b, NULL, NULL), 1);
    FcSession s;
    ReplayUI r;
    replay_init(&s, &r);
    replay_deal(&s, &r, 1);
    s.board = b;
    int at;
    CHECK(replay_moves(&s, &r, mv, n, 0, 1, &at) == NULL);

    /* QC above JC: never a sure win (the moves go as far as they can) */
    static const char *const cols2[8] = { "KD QD JD TD 9D 8D 7D 6D 5D TC", "KH QH JH TH 9H 8H 7H 6H 5H TS",
                                          "KC JC QC", "KS QS JS" };
    make_board(&b, cols2, NULL, homes);
    CHECK_EQ(fc_sure_win(&b, mv, &n), 0);
    CHECK(n > 0);
    for (int i = 0; i < n; i++) CHECK(fc_solve_play(&b, &mv[i], 0));
    CHECK(b.cards_left > 0);

    /* free-cell cards count too */
    static const char *const cols3[8] = { "KC QC JC", "KS QS JS", "KD QD JD", "KH QH JH" };
    static const char *const fcs3[4] = { "TC", "TS", NULL, NULL };
    static const char *const homes3[4] = { "9C", "TD", "TH", "9S" };
    make_board(&b, cols3, fcs3, homes3);
    CHECK_EQ(fc_sure_win(&b, mv, &n), 1);
    fcs_free(&s);
}

static void test_api(void)
{
    FcSolver *sv = fc_solver_new(64);
    FcBoard b;
    FcSolveResult res, res2;
    fc_deal(&b, 1);
    CHECK_EQ(fc_solve(sv, &b, 0, NULL, &res), FC_SOLVE_GAVE_UP);   /* tiny budget */
    CHECK_EQ(res.nodes, 64);
    CHECK_EQ(res.nmoves, 0);
    volatile int cancel = 1;
    CHECK_EQ(fc_solve(sv, &b, 0, &cancel, &res), FC_SOLVE_CANCELLED);
    CHECK_EQ(res.expanded, 0);
    fc_solver_free(sv);

    sv = fc_solver_new(0);                     /* default budget */
    CHECK(fc_solver_memory(sv) <= 32u << 20);
    cancel = 0;
    CHECK_EQ(fc_solve(sv, &b, 0, &cancel, &res), FC_SOLVE_SOLVED);   /* reusable, cancel flag clear */
    FcSolveMove copy[512];
    int n = res.nmoves;
    memcpy(copy, res.moves, (size_t)n * sizeof *copy);
    FcSolver *sv2 = fc_solver_new(0);
    fc_solve(sv2, &b, 0, NULL, &res2);         /* deterministic */
    CHECK_EQ(res2.nmoves, n);
    CHECK(memcmp(res2.moves, copy, (size_t)n * sizeof *copy) == 0);
    fc_solver_free(sv2);
    FcBoard w = b;                             /* fc_solve_play follows fc_solve's moves */
    for (int i = 0; i < n; i++) CHECK(fc_solve_play(&w, &copy[i], 0));
    CHECK_EQ(w.cards_left, 0);
    CHECK_EQ(fc_solve(sv, &w, 0, NULL, &res), FC_SOLVE_SOLVED);       /* already won */
    CHECK_EQ(res.nmoves, 0);

    FcSolveMove m = copy[0];                   /* fc_solve_play refuses what the session would not do */
    FcBoard e = b;
    m.src_col = 0; m.src_pos = 2;              /* empty free cell */
    CHECK(!fc_solve_play(&e, &m, 0));
    CHECK(memcmp(&e, &b, sizeof b) == 0);

    /* a fresh deal with an ace at the bottom: the first move may be the bare autoplay click */
    fc_deal(&b, 14);
    FcSolveMove mv[FC_SOLVE_MAX_MOVES];
    int k = fc_solve_moves(&b, 0, mv), ap = -1;
    for (int i = 0; i < k; i++)
        if (mv[i].kind == FC_SM_AUTOPLAY) { CHECK_EQ(ap, -1); ap = i; }
    CHECK(ap >= 0);
    if (ap >= 0) {                             /* select + click again: the session autoplays, uncounted */
        FcSession s;
        ReplayUI r;
        replay_init(&s, &r);
        replay_deal(&s, &r, 14);
        int at;
        CHECK(replay_moves(&s, &r, &mv[ap], 1, 0, 0, &at) == NULL);
        CHECK(s.board.cards_left < 52);
        CHECK_EQ(s.moves, 0);
        fcs_free(&s);
    }
    fc_solver_free(sv);
}

int main(void)
{
    FcSolver *sv = fc_solver_new(0);
    if (!sv) { printf("out of memory\n"); return 1; }
    test_api();
    test_sure_win();
    test_constructed_unsolvable(sv);
    test_moves_match_session(sv);
    test_sample_deals(sv, 0, 300, 107, 1);     /* deals 1, 108, ..., 31994 */
    test_sample_deals(sv, 1, 100, 317, 50);    /* standard supermove rule */
    test_unsolvable_deal(sv);
    test_midgame(sv);
    fc_solver_free(sv);
    printf("test_solver: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
