/*
 * FreeCell HD — native tests of the rules primitives (src/core/game.c) against
 * docs/xp-reference/rules.md (deals, capacity, CardsToMove, MoveCol, supermove, autoplay, no-moves, undo).
 */
#include "core/game.h"
#include "core/session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (a), _b = (b); checks++; if (_a != _b) { fails++; \
    printf("FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

/* ---- helpers ------------------------------------------------------------------------------------ */

static Card C(const char *s)
{
    const char *ranks = "A23456789TJQK", *suits = "CDHS";
    return (int)(strchr(ranks, s[0]) - ranks) * 4 + (int)(strchr(suits, s[1]) - suits);
}

static const char *N(Card c)
{
    static char buf[8][3];
    static int k;
    char *b = buf[k++ & 7];
    if (c == FC_EMPTY) return "--";
    b[0] = "A23456789TJQK"[fc_rank(c)];
    b[1] = "CDHS"[fc_suit(c)];
    b[2] = 0;
    return b;
}

static void set_col(FcBoard *b, int col, const char *cards)
{
    int i = 0;
    for (int k = 0; k < FC_COLLEN; k++) b->board[col][k] = FC_EMPTY;
    while (*cards) {
        while (*cards == ' ') cards++;
        if (!*cards) break;
        b->board[col][i++] = C(cards);
        cards += 2;
    }
}

static void set_fc(FcBoard *b, int i, const char *card) { b->board[0][i] = card ? C(card) : FC_EMPTY; }

static void set_home(FcBoard *b, int slot, const char *card)
{
    Card c = C(card);
    b->board[0][slot] = c;
    b->home_rank[fc_suit(c)] = (int8_t)fc_rank(c);
    b->suit_home_slot[fc_suit(c)] = (int8_t)slot;
}

static void fix_cards_left(FcBoard *b)
{
    int n = 0;
    for (int i = 0; i < 4; i++) n += b->board[0][i] != FC_EMPTY;
    for (int c = 1; c <= 8; c++) n += fc_last_index(b, c) + 1;
    b->cards_left = n;
}

/* Rows of a deal table (rules.md §1.3/§1.4) -> check board columns. */
static void check_deal_rows(const FcBoard *b, const char *const rows[7], const char *what)
{
    int bad = 0;
    for (int r = 0; r < 7; r++) {
        const char *p = rows[r];
        for (int c = 1; c <= 8; c++) {
            while (*p == ' ') p++;
            Card want = *p ? C(p) : FC_EMPTY;
            if (*p) p += 2;
            if (b->board[c][r] != want) {
                bad++;
                printf("  %s: col %d row %d = %s, expected %s\n", what, c, r, N(b->board[c][r]), N(want));
            }
        }
    }
    for (int c = 1; c <= 8; c++) bad += b->board[c][7] != FC_EMPTY;
    checks++;
    if (bad) { fails++; printf("FAIL deal %s\n", what); }
}

/* Every step must be a legal single-card move: exposed source, empty free cell / empty column /
 * CanStack / proper home destination. Applies the steps to *b. */
static int steps_legal(FcBoard *b, const FcAction *a)
{
    for (int i = 0; i < a->nsteps; i++) {
        const FcStep *st = &a->steps[i];
        Card c = st->card;
        if (st->src_col == 0) {
            if (st->src_pos > 3 || b->board[0][st->src_pos] != c) return 0;
        } else if (fc_last_index(b, st->src_col) != st->src_pos || b->board[st->src_col][st->src_pos] != c) {
            return 0;
        }
        if (st->dst_col == 0 && st->dst_pos < 4) {
            if (b->board[0][st->dst_pos] != FC_EMPTY) return 0;
        } else if (st->dst_col == 0) {
            Card h = b->board[0][st->dst_pos];
            if (h == FC_EMPTY ? fc_rank(c) != 0 : (fc_suit(h) != fc_suit(c) || fc_rank(h) + 1 != fc_rank(c)))
                return 0;
        } else {
            int l = fc_last_index(b, st->dst_col);
            if (st->dst_pos != l + 1) return 0;
            if (l >= 0 && !fc_can_stack(c, b->board[st->dst_col][l])) return 0;
        }
        fc_step_apply(b, st);
    }
    return 1;
}

static int board_eq(const FcBoard *x, const FcBoard *y) { return memcmp(x, y, sizeof *x) == 0; }

/* ---- tests ------------------------------------------------------------------------------------- */

static void test_deals(void)
{
    static const char *const g1[7] = {
        "JD 2D 9H JC 5D 7H 7C 5H", "KD KC 9S 5S AD QC KH 3H", "2S KS 9D QD JS AS AH 3C",
        "4C 5C TS QH 4H AC 4D 7S", "3S TD 4S TH 8H 2C JH 7D", "6D 8S 8D QS 6C 3D 8C TC",
        "6S 9C 2H 6H" };
    static const char *const g617[7] = {
        "7D AD 5C 3S 5S 8C 2D AH", "TD 7S QD AC 6D 8H AS KH", "TH QC 3H 9D 6S 8D 3D TC",
        "KD 5H 9S 3C 8S 7H 4D JS", "4C QS 9C 9H 7C 6H 2C 2S", "4S TS 2H 5D JC 6C JH QH",
        "JD KS KC 4H" };
    static const char *const gm1[7] = {
        "AC AD AH AS QC QD QH QS", "3C 3D 3H 3S TC TD TH TS", "5C 5D 5H 5S 8C 8D 8H 8S",
        "7C 7D 7H 7S 6C 6D 6H 6S", "9C 9D 9H 9S 4C 4D 4H 4S", "JC JD JH JS 2C 2D 2H 2S",
        "KC KD KH KS" };
    static const char *const gm2[7] = {
        "AS AH AD AC 7S 7H 7D 7C", "KS KH KD KC 6S 6H 6D 6C", "QS QH QD QC 5S 5H 5D 5C",
        "JS JH JD JC 4S 4H 4D 4C", "TS TH TD TC 3S 3H 3D 3C", "9S 9H 9D 9C 2S 2H 2D 2C",
        "8S 8H 8D 8C" };
    FcBoard b;
    fc_deal(&b, 1);    check_deal_rows(&b, g1, "#1");
    fc_deal(&b, 617);  check_deal_rows(&b, g617, "#617");
    fc_deal(&b, -1);   check_deal_rows(&b, gm1, "#-1");
    fc_deal(&b, -2);   check_deal_rows(&b, gm2, "#-2");
    CHECK_EQ(b.cards_left, 52);
    for (int i = 0; i < 8; i++) CHECK_EQ(b.board[0][i], FC_EMPTY);
    for (int s = 0; s < 4; s++) { CHECK_EQ(b.home_rank[s], -1); CHECK_EQ(b.suit_home_slot[s], -1); }

    /* every deal 1..1000000 sample: 52 distinct cards, 7/7/7/7/6/6/6/6 */
    for (int n = 1; n <= 1000000; n += 9973) {
        int seen[52] = { 0 }, ok = 1;
        fc_deal(&b, n);
        for (int c = 1; c <= 8; c++) {
            ok &= fc_last_index(&b, c) == (c <= 4 ? 6 : 5);
            for (int i = 0; i <= fc_last_index(&b, c); i++) seen[b.board[c][i]]++;
        }
        for (int i = 0; i < 52; i++) ok &= seen[i] == 1;
        CHECK(ok);
    }
    fc_board_clear(&b);
    CHECK_EQ(b.cards_left, 0);
    CHECK_EQ(fc_last_index(&b, 3), -1);
}

static void test_random_number(void)
{
    /* msvcrt rand() after srand(1): 41, 18467, 6334 -> the third value (Python reference). */
    CHECK_EQ(fc_random_game_number(1), 6334);
    CHECK_EQ(fc_random_game_number(0), 21238);
    CHECK_EQ(fc_random_game_number(2), 24198);
    CHECK_EQ(fc_random_game_number(1700000000u), 22663);
    CHECK_EQ(fc_random_game_number(0xFFFFFFFFu), 3374);
    CHECK_EQ(fc_random_game_number(1646), 26907);     /* third rand() is 0 -> skipped */
    int ok = 1;
    for (uint32_t t = 1600000000u; t < 1600020000u; t++) {
        int n = fc_random_game_number(t);
        ok &= n >= 1 && n <= 32767;
    }
    CHECK(ok);
}

static void test_predicates(void)
{
    CHECK(fc_can_stack(C("9H"), C("TC")));
    CHECK(fc_can_stack(C("9S"), C("TD")));
    CHECK(!fc_can_stack(C("9C"), C("TS")));
    CHECK(!fc_can_stack(C("9H"), C("JC")));
    CHECK(!fc_can_stack(C("TC"), C("9H")));
    CHECK(!fc_can_stack(FC_EMPTY, C("2D")));
    CHECK(!fc_can_stack(C("KD"), FC_EMPTY));
    CHECK_EQ(fc_capacity(0, 0), 1);
    CHECK_EQ(fc_capacity(4, 0), 5);
    CHECK_EQ(fc_capacity(1, 2), 6);
    CHECK_EQ(fc_capacity(1, 1), 4);
    CHECK_EQ(fc_capacity(3, 1), 8);
    CHECK_EQ(fc_capacity(4, 7), 40);
}

/* The Wine-verified capacity table (§2.2): 7-card run 9H..3H onto TC. */
static void capacity_board(FcBoard *b, int f, int e)
{
    static const char *const fill[4] = { "KS", "KH", "KD", "KC" };
    static const char *const cols[6] = { "QS", "QH", "QD", "QC", "JS", "JH" };
    fc_board_clear(b);
    set_col(b, 1, "9H 8S 7H 6S 5H 4S 3H");
    set_col(b, 2, "TC");
    for (int i = 0; i < 4 - f; i++) set_fc(b, i, fill[i]);
    for (int c = 3; c <= 8 - e; c++) set_col(b, c, cols[c - 3]);
    fix_cards_left(b);
}

static void test_capacity_table(void)
{
    static const struct { int f, e, cap; } t[] = { { 1, 2, 6 }, { 1, 1, 4 }, { 4, 0, 5 }, { 3, 1, 8 } };
    for (int i = 0; i < 4; i++) {
        FcBoard b;
        capacity_board(&b, t[i].f, t[i].e);
        CHECK_EQ(fc_free_cells_empty(&b), t[i].f);
        CHECK_EQ(fc_empty_columns(&b), t[i].e);
        CHECK_EQ(fc_max_movable(&b), t[i].cap);
        CHECK_EQ(fc_cards_to_move(&b, 1, 2), 7);
    }
    /* f=3, e=1: capacity 8, logged as 19 single-card steps, each legal */
    FcBoard b, chk;
    FcAction a;
    capacity_board(&b, 3, 1);
    chk = b;
    fc_action_begin(&a, &b);
    fc_supermove(&b, &a, 1, 2);
    CHECK_EQ(a.nsteps, 19);
    CHECK(steps_legal(&chk, &a));
    CHECK(board_eq(&chk, &b));
    CHECK_EQ(fc_last_index(&b, 1), -1);
    CHECK_EQ(fc_last_index(&b, 2), 7);
    CHECK_EQ(b.board[2][7], C("3H"));
    CHECK_EQ(fc_free_cells_empty(&b), 3);
    /* undo restores exactly, both at once and step by step */
    FcBoard u = b;
    fc_undo_action(&u, &a);
    CHECK(board_eq(&u, &a.before));
    u = b;
    for (int i = a.nsteps - 1; i >= 0; i--) fc_step_unapply(&u, &a.steps[i]);
    CHECK(board_eq(&u, &a.before));
}

/* The same table through the controller: the Wine-verified MessageBox texts (missing space fixed). */
static char g_msg[200];
static int g_msg_id;
static void on_message(void *ctx, int id, const char *text) { g_msg_id = id; snprintf(g_msg, sizeof g_msg, "%s", text); }

static void test_capacity_messages(void)
{
    static const struct { int f, e; const char *msg; } t[] = {
        { 1, 2, "That move requires moving 7 cards. You only have enough free space to move 6." },
        { 1, 1, "That move requires moving 7 cards. You only have enough free space to move 4." },
        { 4, 0, "That move requires moving 7 cards. You only have enough free space to move 5." } };
    for (int i = 0; i < 3; i++) {
        FcSessionUI ui = { 0 };
        ui.message = on_message;
        FcSession s;
        fcs_init(&s, &ui, NULL);
        s.game_number = 1; s.in_progress = 1; s.dealt = 1;
        capacity_board(&s.board, t[i].f, t[i].e);
        g_msg[0] = 0; g_msg_id = 0;
        fcs_click(&s, 1, 0);
        fcs_click(&s, 2, 0);
        CHECK_EQ(g_msg_id, 307);
        CHECK(strcmp(g_msg, t[i].msg) == 0);
        CHECK_EQ(fc_last_index(&s.board, 1), 6);           /* nothing moved */
        fcs_free(&s);
    }
}

static void test_cards_to_move(void)
{
    FcBoard b;
    fc_board_clear(&b);
    set_col(&b, 1, "KS 9H 8S 7H 6S");
    set_col(&b, 2, "TC");
    set_col(&b, 3, "7C");
    set_col(&b, 4, "8D");
    set_col(&b, 5, "QH 5D");
    set_col(&b, 6, "2C 9C");
    CHECK_EQ(fc_cards_to_move(&b, 1, 1), 1);       /* to itself */
    CHECK_EQ(fc_cards_to_move(&b, 1, 2), 4);       /* forced: 9H onto TC */
    CHECK_EQ(fc_cards_to_move(&b, 1, 3), 0);       /* nothing in 9H..6S fits on 7C */
    CHECK_EQ(fc_cards_to_move(&b, 1, 4), 0);       /* 7H onto 8D: same colour */
    CHECK_EQ(fc_cards_to_move(&b, 1, 5), 0);       /* nothing fits on 5D */
    CHECK_EQ(fc_cards_to_move(&b, 1, 7), 4);       /* empty: the ordered run 9H..6S */
    CHECK_EQ(fc_cards_to_move(&b, 6, 7), 1);       /* 9C on 2C: run of 1 */
    CHECK_EQ(fc_cards_to_move(&b, 6, 2), 0);       /* 9C onto TC: same colour, run broken */
    CHECK_EQ(fc_cards_to_move(&b, 8, 2), 0);       /* empty source */
    set_col(&b, 4, "8C");
    CHECK_EQ(fc_cards_to_move(&b, 1, 4), 2);       /* 7H onto 8C */
}

/* Wine-verified MoveCol table (§2.5): "Move column" moves min(run, free+1) via free cells only. */
static void test_move_column(void)
{
    static const struct { int f, e, moved; } t[] = { { 3, 1, 4 }, { 1, 2, 2 }, { 0, 1, 1 }, { 4, 1, 5 } };
    for (int i = 0; i < 4; i++) {
        FcBoard b, chk;
        FcAction a;
        capacity_board(&b, t[i].f, t[i].e);
        int dst = 8;
        CHECK_EQ(b.board[dst][0], FC_EMPTY);
        chk = b;
        fc_action_begin(&a, &b);
        fc_move_run_via_free_cells(&b, &a, 1, dst);
        CHECK(steps_legal(&chk, &a));
        CHECK_EQ(fc_last_index(&b, dst) + 1, t[i].moved);
        CHECK_EQ(fc_free_cells_empty(&b), t[i].f);
        CHECK_EQ(a.nsteps, 2 * t[i].moved - 1);
    }
    FcBoard b;
    FcAction a;
    capacity_board(&b, 3, 1);
    fc_action_begin(&a, &b);
    fc_move_run_via_free_cells(&b, &a, 1, 8);
    CHECK_EQ(b.board[8][0], C("6S"));
    CHECK_EQ(b.board[8][3], C("3H"));
}

/* Supermove: random positions, every n <= capacity succeeds with legal single-card steps only. */
static void test_supermove_random(void)
{
    srand(12345);
    int tested = 0;
    for (int iter = 0; iter < 4000; iter++) {
        FcBoard b;
        fc_board_clear(&b);
        int used[52] = { 0 };
        /* source run in column 1: descending ranks from `top`, alternating colours */
        int top = 2 + rand() % 11, len = 1 + rand() % (top + 1), red = rand() % 2;
        for (int i = 0; i < len; i++) {
            int s = (red ^ (i & 1)) ? 1 + rand() % 2 : (rand() % 2 ? 3 : 0);
            b.board[1][i] = (top - i) * 4 + s;
            used[b.board[1][i]] = 1;
        }
        /* destination: a card the k-th run card fits on */
        int k = rand() % len;
        Card fit = b.board[1][len - 1 - k];
        Card dst = FC_EMPTY;
        for (int s = 0; s < 4 && dst == FC_EMPTY; s++) {
            Card d = (fc_rank(fit) + 1) * 4 + s;
            if (fc_rank(fit) < 12 && !used[d] && fc_can_stack(fit, d)) dst = d;
        }
        if (dst == FC_EMPTY) continue;
        b.board[2][0] = dst;
        used[dst] = 1;
        /* random free cells and empty columns, fillers are unused kings/queens etc. */
        int f = rand() % 5, e = rand() % 7, fill = 51;
        for (int c = 0; c < 4 - f; c++) {
            while (used[fill]) fill--;
            b.board[0][c] = fill; used[fill] = 1;
        }
        for (int c = 3; c <= 8 - e; c++) {
            while (used[fill]) fill--;
            b.board[c][0] = fill; used[fill] = 1;
        }
        fix_cards_left(&b);
        int n = fc_cards_to_move(&b, 1, 2);
        CHECK_EQ(n, k + 1);
        if (n > fc_max_movable(&b)) continue;
        FcBoard chk = b, before = b;
        FcAction a;
        fc_action_begin(&a, &b);
        fc_supermove(&b, &a, 1, 2);
        int legal = steps_legal(&chk, &a);
        CHECK(legal);
        CHECK(board_eq(&chk, &b));
        CHECK_EQ(fc_last_index(&b, 2), n);                      /* dst card + n moved */
        CHECK_EQ(fc_last_index(&b, 1), len - 1 - n);
        CHECK_EQ(fc_free_cells_empty(&b), f);                   /* everything unparked */
        CHECK_EQ(fc_empty_columns(&b), e + (n == len));
        for (int j = a.nsteps - 1; j >= 0; j--) fc_step_unapply(&b, &a.steps[j]);
        CHECK(board_eq(&b, &before));
        tested++;
    }
    CHECK(tested > 1000);
}

static void test_autoplay(void)
{
    FcBoard b;
    /* rule cases (§3) */
    fc_board_clear(&b);
    CHECK(fc_safe_to_autoplay(&b, C("AH"), 0));
    CHECK(!fc_safe_to_autoplay(&b, C("2H"), 0));
    CHECK(!fc_safe_to_autoplay(&b, FC_EMPTY, 0));
    CHECK(fc_safe_to_autoplay(&b, C("KS"), 1));               /* cheat win: everything */
    set_home(&b, 4, "AH");
    CHECK(fc_safe_to_autoplay(&b, C("2H"), 0));
    set_home(&b, 4, "2H");
    CHECK(!fc_safe_to_autoplay(&b, C("3H"), 0));              /* black foundations missing */
    set_home(&b, 5, "AC");
    set_home(&b, 6, "2S");
    CHECK(!fc_safe_to_autoplay(&b, C("3H"), 0));              /* clubs at A < 2 */
    set_home(&b, 5, "2C");
    CHECK(fc_safe_to_autoplay(&b, C("3H"), 0));
    CHECK(!fc_safe_to_autoplay(&b, C("3C"), 0));              /* diamonds missing */
    set_home(&b, 7, "AD");
    CHECK(!fc_safe_to_autoplay(&b, C("3C"), 0));              /* diamonds at A < 2 */
    set_home(&b, 7, "2D");
    CHECK(fc_safe_to_autoplay(&b, C("3C"), 0));
    CHECK(!fc_safe_to_autoplay(&b, C("4C"), 0));              /* own foundation not at 3 */

    /* Wine-verified cascade: homes C3 D2 H4 S3, bottoms 3D 4C 5H 4S, 5D in a free cell ->
     * 3D, 4C, 4S, then 5H go home; 5D stays. */
    fc_board_clear(&b);
    set_home(&b, 4, "3C");
    set_home(&b, 5, "2D");
    set_home(&b, 6, "4H");
    set_home(&b, 7, "3S");
    set_col(&b, 1, "KS 3D");
    set_col(&b, 2, "KH 4C");
    set_col(&b, 3, "KD 5H");
    set_col(&b, 4, "KC 4S");
    set_fc(&b, 0, "5D");
    fix_cards_left(&b);
    FcAction a;
    FcBoard chk = b;
    fc_action_begin(&a, &b);
    fc_autoplay(&b, &a, 0);
    CHECK_EQ(a.nsteps, 4);
    CHECK_EQ(a.steps[0].card, C("3D"));
    CHECK_EQ(a.steps[1].card, C("4C"));
    CHECK_EQ(a.steps[2].card, C("4S"));
    CHECK_EQ(a.steps[3].card, C("5H"));
    CHECK_EQ(b.board[0][0], C("5D"));
    CHECK_EQ(b.cards_left, 5);
    CHECK(steps_legal(&chk, &a));
    CHECK_EQ(a.steps[3].dst_pos, 6);                          /* hearts' own slot */

    /* home slots: aces go to the leftmost empty home slot; a manual AH in slot 7 keeps 2H there */
    fc_board_clear(&b);
    set_col(&b, 1, "KS AD");
    set_col(&b, 2, "KH AC");
    set_col(&b, 3, "2H");
    set_home(&b, 7, "AH");
    fix_cards_left(&b);
    fc_action_begin(&a, &b);
    fc_autoplay(&b, &a, 0);
    CHECK_EQ(a.nsteps, 3);
    CHECK_EQ(b.board[0][4], C("AD"));
    CHECK_EQ(b.board[0][5], C("AC"));
    CHECK_EQ(b.board[0][7], C("2H"));
    CHECK_EQ(b.suit_home_slot[1], 4);
    CHECK_EQ(b.suit_home_slot[0], 5);
    CHECK_EQ(b.suit_home_slot[2], 7);
    /* undoing an ace frees its slot again */
    FcBoard u = b;
    for (int i = a.nsteps - 1; i >= 0; i--) fc_step_unapply(&u, &a.steps[i]);
    CHECK(board_eq(&u, &a.before));
    CHECK_EQ(u.suit_home_slot[1], -1);
    CHECK_EQ(u.suit_home_slot[0], -1);
    CHECK_EQ(fc_home_slot_for(&b, 3), 6);                     /* spades get the remaining slot */
    CHECK_EQ(b.suit_home_slot[3], 6);

    /* not after the deal: game 14 has AC at the bottom of column 8 (rules.md §3) */
    fc_deal(&b, 14);
    CHECK_EQ(b.board[8][fc_last_index(&b, 8)], C("AC"));
    CHECK_EQ(b.cards_left, 52);
}

static void test_cheat_sweep(void)
{
    for (int n = 1; n <= 40; n++) {
        FcBoard b, chk;
        FcAction a;
        fc_deal(&b, n * 977);
        set_home(&b, 4, "AS");                                 /* a partly played position */
        for (int c = 1; c <= 8; c++)
            for (int i = 0; i < FC_COLLEN; i++)
                if (b.board[c][i] == C("AS")) { for (int k = i; k < FC_COLLEN - 1; k++) b.board[c][k] = b.board[c][k + 1]; }
        fix_cards_left(&b);
        b.cards_left = 51;
        chk = b;
        fc_action_begin(&a, &b);
        fc_autoplay(&b, &a, 1);
        CHECK_EQ(a.nsteps, 51);
        CHECK_EQ(b.cards_left, 0);
        for (int s = 0; s < 4; s++) CHECK_EQ(b.home_rank[s], 12);
        /* every step goes to its suit's home with the next rank: piles stay consistent */
        int ok = 1;
        for (int i = 0; i < a.nsteps; i++) {
            const FcStep *st = &a.steps[i];
            Card h = chk.board[0][st->dst_pos];
            ok &= st->dst_col == 0 && st->dst_pos >= 4;
            ok &= h == FC_EMPTY ? fc_rank(st->card) == 0 : st->card == h + 4;
            fc_step_apply(&chk, st);
        }
        CHECK(ok);
        CHECK(board_eq(&chk, &b));
        for (int i = a.nsteps - 1; i >= 0; i--) fc_step_unapply(&b, &a.steps[i]);
        CHECK(board_eq(&b, &a.before));
    }
}

static void no_moves_board(FcBoard *b, const char *bottom8)
{
    static const char *const tops[8] = { "4H", "6H", "8H", "TH", "4D", "6D", "8D", "TD" };
    fc_board_clear(b);
    set_fc(b, 0, "KS"); set_fc(b, 1, "KC"); set_fc(b, 2, "KH"); set_fc(b, 3, "KD");
    char col[16];
    const char *p = bottom8;
    for (int c = 1; c <= 8; c++) {
        while (*p == ' ') p++;
        snprintf(col, sizeof col, "%s %.2s", tops[c - 1], p);
        p += 2;
        set_col(b, c, col);
    }
    fix_cards_left(b);
}

static void test_no_moves(void)
{
    FcBoard b;
    no_moves_board(&b, "3S 5S 7S 9S JS 3C 5C 7C");
    CHECK_EQ(fc_count_moves_xp(&b), 0);
    no_moves_board(&b, "3S 5S 7S 9S JS 3C 5C QD");             /* JS onto QD only */
    CHECK_EQ(fc_count_moves_xp(&b), 1);
    no_moves_board(&b, "3S 5S 7S 9S JS 3C 5C TD");             /* TD onto JS, 9S onto TD */
    CHECK_EQ(fc_count_moves_xp(&b), 2);
    no_moves_board(&b, "3S 5S 7S 9S JS 3C 5C AH");             /* an ace counts twice */
    CHECK_EQ(fc_count_moves_xp(&b), 2);
    no_moves_board(&b, "3S 5S 7S 9S JS 3C 5C 7C");
    set_home(&b, 4, "6C");                                     /* 7C can go home */
    CHECK_EQ(fc_count_moves_xp(&b), 1);
    no_moves_board(&b, "3S 5S 7S 9S JS 3C 5C 7C");
    set_home(&b, 4, "QS");                                     /* free-cell KS can go home */
    CHECK_EQ(fc_count_moves_xp(&b), 1);
    no_moves_board(&b, "3S 5S 7S 9S JS 3C 5C 7C");
    set_fc(&b, 0, NULL);                                       /* an empty free cell */
    CHECK_EQ(fc_count_moves_xp(&b), 2);
    no_moves_board(&b, "3S 5S 7S 9S JS 3C 5C 7C");
    set_col(&b, 5, "");                                        /* an empty column */
    CHECK_EQ(fc_count_moves_xp(&b), 2);
    CHECK(!fc_is_won(&b));
    b.cards_left = 0;
    CHECK(fc_is_won(&b));
}

static void test_queue(void)
{
    FcBoard b;
    FcAction a;
    fc_board_clear(&b);
    set_col(&b, 1, "KS QH");
    set_fc(&b, 0, "5D");
    set_home(&b, 4, "AH");
    set_col(&b, 2, "2H");
    fix_cards_left(&b);
    FcBoard start = b;
    fc_action_begin(&a, &b);
    fc_queue(&b, &a, 1, 0, 1, 0);          /* to itself: nothing, not logged */
    fc_queue(&b, &a, 0, 0, 0, 0);
    fc_queue(&b, &a, 0, 4, 0, 1);          /* never from home */
    fc_queue(&b, &a, 3, 0, 0, 1);          /* empty source */
    CHECK_EQ(a.nsteps, 0);
    CHECK(board_eq(&b, &start));
    fc_queue(&b, &a, 0, 0, 0, 2);          /* free cell -> free cell */
    fc_queue(&b, &a, 2, 0, 0, 4);          /* 2H home */
    fc_queue(&b, &a, 1, 7, 3, 9);          /* QH -> empty column 3 (positions computed) */
    CHECK_EQ(a.nsteps, 3);
    CHECK_EQ(b.board[0][2], C("5D"));
    CHECK_EQ(b.board[0][4], C("2H"));
    CHECK_EQ(b.home_rank[2], 1);
    CHECK_EQ(b.cards_left, 3);
    CHECK_EQ(a.steps[2].src_pos, 1);
    CHECK_EQ(a.steps[2].dst_pos, 0);
    CHECK_EQ(b.board[3][0], C("QH"));
    fc_undo_action(&b, &a);
    CHECK(board_eq(&b, &start));
}

/* ---- Standard supermove rule (extra) ---------------------------------------------------------- */

static void test_capacity_std_table(void)
{
    for (int f = 0; f <= 4; f++)
        for (int e = 0; e <= 7; e++) {
            CHECK_EQ(fc_capacity_std(f, e), (f + 1) << e);
            CHECK_EQ(fc_capacity(f, e), (f + 1) * (e + 1));                  /* XP's, unchanged */
        }
    /* spot values: the classic table */
    CHECK_EQ(fc_capacity_std(4, 0), 5);
    CHECK_EQ(fc_capacity_std(4, 1), 10);
    CHECK_EQ(fc_capacity_std(1, 2), 8);
    CHECK_EQ(fc_capacity_std(0, 7), 128);
    CHECK_EQ(fc_capacity_std(4, 7), 640);
    CHECK_EQ(fc_capacity_std(3, -1), 4);                                     /* clamped */
    /* the board queries: non-empty destination counts every empty column, an empty one the others */
    for (int f = 0; f <= 4; f++)
        for (int e = 0; e <= 6; e++) {
            FcBoard b;
            capacity_board(&b, f, e);
            CHECK_EQ(fc_max_movable_rule(&b, 0), (f + 1) * (e + 1));
            CHECK_EQ(fc_max_movable_rule(&b, 1), (f + 1) << e);
            if (e >= 1) {
                CHECK_EQ(fc_max_to_empty(&b, 0), f + 1);
                CHECK_EQ(fc_max_to_empty(&b, 1), (f + 1) << (e - 1));
            }
        }
}

/* Every (f, e, n) with a non-empty and with an empty destination, empty columns in random places,
 * the run alone in its column or below other cards: every generated step is a legal single-card move,
 * the final board is exactly the expected one (the run moved, nothing else changed: free cells and
 * helper columns empty again), the step count stays small, undo restores, and n above the capacity
 * is refused without a step. */
static void test_supermove_std_exhaustive(void)
{
    static const char *const run12[12] = { "QH", "JS", "TH", "9S", "8H", "7S", "6H", "5S", "4H", "3S", "2H", "AS" };
    static const char *const pool[] = { "KS", "KH", "KD", "KC", "QS", "QD", "QC", "JH", "JD", "JC", "TS", "TD" };
    srand(2468);
    int tested = 0, refused = 0, maxsteps = 0;
    for (int empty_dst = 0; empty_dst <= 1; empty_dst++)
        for (int f = 0; f <= 4; f++)
            for (int e = 0; e <= 6; e++)          /* e = empty columns other than src/dst */
                for (int whole = 0; whole <= 1; whole++)
                    for (int n = 1; n <= 12; n++) {
                        int cap = fc_capacity_std(f, e), len = whole ? n : 12;
                        if (!empty_dst && n == 12) continue;     /* no card above a Q run's head */
                        /* columns: a random permutation gives src, dst, the empty ones, fillers */
                        int perm[8];
                        for (int i = 0; i < 8; i++) perm[i] = i + 1;
                        for (int i = 7; i > 0; i--) { int j = rand() % (i + 1), t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
                        int src = perm[0], dst = perm[1];
                        FcBoard b;
                        fc_board_clear(&b);
                        int used[52] = { 0 };
                        for (int i = 0; i < len; i++) {
                            b.board[src][i] = C(run12[12 - len + i]);
                            used[b.board[src][i]] = 1;
                        }
                        if (!empty_dst) {              /* the card the n-th run card from the bottom fits on */
                            Card head = b.board[src][len - n];
                            Card d = (fc_rank(head) + 1) * 4 + (fc_is_red(head) ? 0 : 1);   /* C or D */
                            b.board[dst][0] = d;
                            used[d] = 1;
                        }
                        int k = 0;
                        for (int i = 0; i < 4 - f; i++) {
                            while (used[C(pool[k])]) k++;
                            b.board[0][i] = C(pool[k]); used[C(pool[k])] = 1;
                        }
                        for (int i = 2 + e; i < 8; i++) {     /* fillers; perm[2 .. 2+e-1] stay empty */
                            while (used[C(pool[k])]) k++;
                            b.board[perm[i]][0] = C(pool[k]); used[C(pool[k])] = 1;
                        }
                        fix_cards_left(&b);
                        CHECK_EQ(fc_empty_columns(&b), e + empty_dst);
                        if (!empty_dst) CHECK_EQ(fc_cards_to_move(&b, src, dst), n);
                        FcBoard expect = b, chk = b, before = b;
                        int dl = fc_last_index(&b, dst) + 1;
                        for (int i = 0; i < n; i++) {
                            expect.board[dst][dl + i] = b.board[src][len - n + i];
                            expect.board[src][len - n + i] = FC_EMPTY;
                        }
                        FcAction a;
                        fc_action_begin(&a, &b);
                        int ok = fc_move_cards_std(&b, &a, src, dst, n);
                        if (n > cap) {
                            CHECK_EQ(ok, 0);
                            CHECK_EQ(a.nsteps, 0);
                            CHECK(board_eq(&b, &before));
                            refused++;
                            continue;
                        }
                        CHECK_EQ(ok, 1);
                        int legal = steps_legal(&chk, &a);
                        CHECK(legal);
                        CHECK(board_eq(&chk, &b));
                        CHECK(board_eq(&b, &expect));
                        if (!legal || !board_eq(&b, &expect))
                            printf("  std supermove f=%d e=%d n=%d len=%d empty_dst=%d src=%d dst=%d\n", f, e, n, len,
                                   empty_dst, src, dst);
                        CHECK(a.nsteps >= 2 * n - 1 || n == 1);
                        CHECK(a.nsteps < FC_MAX_STEPS);
                        if (a.nsteps > maxsteps) maxsteps = a.nsteps;
                        if (n <= f + 1) CHECK_EQ(a.nsteps, 2 * n - 1);   /* free cells suffice */
                        for (int j = a.nsteps - 1; j >= 0; j--) fc_step_unapply(&b, &a.steps[j]);
                        CHECK(board_eq(&b, &before));
                        tested++;
                    }
    printf("standard supermove: %d moves checked step by step (max %d steps), %d over capacity refused\n",
           tested, maxsteps, refused);
    CHECK(tested > 1000);
    CHECK(refused > 100);

    /* refusals: not an ordered run, a destination that does not fit, bad columns */
    FcBoard b;
    FcAction a;
    fc_board_clear(&b);
    set_col(&b, 1, "9H 8S 7S");
    set_col(&b, 2, "TC");
    fix_cards_left(&b);
    fc_action_begin(&a, &b);
    CHECK_EQ(fc_move_cards_std(&b, &a, 1, 3, 2), 0);       /* 8S 7S is not a run */
    CHECK_EQ(fc_move_cards_std(&b, &a, 1, 2, 1), 0);       /* 7S does not fit on TC */
    CHECK_EQ(fc_move_cards_std(&b, &a, 1, 1, 1), 0);
    CHECK_EQ(fc_move_cards_std(&b, &a, 0, 2, 1), 0);
    CHECK_EQ(fc_move_cards_std(&b, &a, 4, 2, 1), 0);       /* empty source */
    CHECK_EQ(fc_move_cards_std(&b, &a, 1, 3, 0), 0);
    CHECK_EQ(a.nsteps, 0);
    CHECK_EQ(fc_move_cards_std(&b, &a, 1, 3, 1), 1);       /* 7S alone to an empty column */
    CHECK_EQ(a.nsteps, 1);
}

/* The full New Game range: 1..1000000, deterministic per seed. */
static void test_random_number_full(void)
{
    int big = 0, ok = 1;
    for (uint32_t t = 1600000000u; t < 1600020000u; t++) {
        int n = fc_random_game_number_full(t);
        ok &= n >= 1 && n <= 1000000;
        big += n > 32767;
    }
    CHECK(ok);
    CHECK(big > 19000);
    CHECK_EQ(fc_random_game_number_full(12345), fc_random_game_number_full(12345));
    CHECK(fc_random_game_number_full(1) != fc_random_game_number_full(2));
}

int main(void)
{
    test_deals();
    test_random_number();
    test_predicates();
    test_capacity_table();
    test_capacity_messages();
    test_cards_to_move();
    test_move_column();
    test_supermove_random();
    test_autoplay();
    test_cheat_sweep();
    test_no_moves();
    test_queue();
    test_capacity_std_table();
    test_supermove_std_exhaustive();
    test_random_number_full();
    printf("test_game: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
