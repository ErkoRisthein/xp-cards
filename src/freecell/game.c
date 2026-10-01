/*
 * FreeCell HD — game rules primitives. Faithful to Windows XP FreeCell (docs/xp-reference/rules.md);
 * function names in comments are the reverse-engineered XP names.
 */
#include "game.h"
#include "session.h"   /* fc_step_apply / fc_step_unapply / fc_cheat_sweep (game.h extensions) */

#include <string.h>

/* ---- Deal (rules.md §1) ------------------------------------------------------------------------ */

void fc_board_clear(FcBoard *b)
{
    for (int c = 0; c < 9; c++)
        for (int i = 0; i < FC_COLLEN; i++)
            b->board[c][i] = FC_EMPTY;
    for (int s = 0; s < 4; s++) {
        b->home_rank[s] = -1;
        b->suit_home_slot[s] = -1;
    }
    b->cards_left = 0;
}

/* msvcrt rand(): the Microsoft LCG (§1.1). */
typedef struct { uint32_t seed; } MsRand;
static int ms_rand(MsRand *r)
{
    r->seed = r->seed * 214013u + 2531011u;
    return (int)((r->seed >> 16) & 0x7FFF);
}

void fc_deal(FcBoard *b, int n)
{
    fc_board_clear(b);
    if (n == -1) {                                   /* §1.4: fixed layout */
        for (int row = 0; row < 7; row++)
            for (int c = 1; c <= 4; c++) b->board[c][row] = row * 8 + (c - 1);
        for (int row = 0; row < 6; row++)
            for (int c = 5; c <= 8; c++) b->board[c][row] = 44 - row * 8 + (c - 5);
    } else if (n == -2) {
        for (int c = 1; c <= 4; c++) b->board[c][0] = 4 - c;          /* AS AH AD AC */
        int v = 51;
        for (int row = 1; row < 7; row++)
            for (int c = 1; c <= 4; c++) b->board[c][row] = v--;
        for (int row = 0; row < 6; row++)
            for (int c = 5; c <= 8; c++) b->board[c][row] = v--;
    } else {                                         /* §1.2: srand(n), row by row across columns */
        MsRand r = { (uint32_t)n };
        int deck[52], left = 52;
        for (int i = 0; i < 52; i++) deck[i] = i;
        for (int i = 0; i < 52; i++) {
            int j = ms_rand(&r) % left;
            b->board[1 + (i & 7)][i >> 3] = deck[j];
            deck[j] = deck[--left];
        }
    }
    b->cards_left = 52;
}

int fc_random_game_number(uint32_t time_seed)     /* RandomGameNumber, §1.6 */
{
    MsRand r = { time_seed };
    ms_rand(&r);
    ms_rand(&r);
    int v;
    do v = ms_rand(&r); while (v < 1 || v > FC_GAME_MAX);
    return v;
}

/* Extra: the same start (srand(t); rand(); rand();), then pairs of draws -> 30 bits, rejected above
 * the largest multiple of 1000000 (1073 * 10^6 < 2^30) so every game is equally likely. */
int fc_random_game_number_full(uint32_t time_seed)
{
    MsRand r = { time_seed };
    ms_rand(&r);
    ms_rand(&r);
    for (;;) {
        uint32_t hi = (uint32_t)ms_rand(&r), lo = (uint32_t)ms_rand(&r), v = hi << 15 | lo;
        if (v < 1073u * (uint32_t)FC_GAME_MAX) return (int)(v % (uint32_t)FC_GAME_MAX) + 1;
    }
}

/* ---- Predicates (§2) ------------------------------------------------------------------------- */

int fc_can_stack(Card src, Card dst)
{
    if (src == FC_EMPTY || dst == FC_EMPTY) return 0;
    return fc_rank(dst) - fc_rank(src) == 1 && fc_is_red(src) != fc_is_red(dst);
}

int fc_last_index(const FcBoard *b, int col)
{
    if (col < 0 || col > 8) return -1;
    int i = 0;
    while (i < FC_COLLEN && b->board[col][i] != FC_EMPTY) i++;
    return i - 1;
}

int fc_free_cells_empty(const FcBoard *b)
{
    int n = 0;
    for (int i = 0; i < 4; i++) n += b->board[0][i] == FC_EMPTY;
    return n;
}

int fc_empty_columns(const FcBoard *b)
{
    int n = 0;
    for (int c = 1; c <= 8; c++) n += b->board[c][0] == FC_EMPTY;
    return n;
}

int fc_capacity(int f, int e) { return (f + 1) * (e + 1); }   /* Capacity, §2.2 */

int fc_max_movable(const FcBoard *b) { return fc_capacity(fc_free_cells_empty(b), fc_empty_columns(b)); }

int fc_capacity_std(int f, int e)
{
    if (f < 0) f = 0;
    if (e < 0) e = 0;
    if (e > 20) e = 20;
    return (f + 1) << e;
}

int fc_max_movable_rule(const FcBoard *b, int standard)
{
    return standard ? fc_capacity_std(fc_free_cells_empty(b), fc_empty_columns(b)) : fc_max_movable(b);
}

int fc_max_to_empty(const FcBoard *b, int standard)
{
    int f = fc_free_cells_empty(b);
    return standard ? fc_capacity_std(f, fc_empty_columns(b) - 1) : f + 1;
}

int fc_cards_to_move(const FcBoard *b, int s, int d)          /* CardsToMove, §2.3 */
{
    if (s == d) return 1;
    int i = fc_last_index(b, s);
    if (i < 0) return 0;
    if (b->board[d][0] == FC_EMPTY) {                          /* ordered run at the bottom */
        int n = 0;
        while (i > 0 && fc_can_stack(b->board[s][i], b->board[s][i - 1])) { i--; n++; }
        return n + 1;
    }
    Card dst = b->board[d][fc_last_index(b, d)], cur = b->board[s][i];
    int n = 1;
    if (fc_can_stack(cur, dst)) return 1;
    while (i != 0) {
        if (!fc_can_stack(cur, b->board[s][i - 1])) return 0;
        cur = b->board[s][--i];
        n++;
        if (fc_can_stack(cur, dst)) return n;
    }
    return 0;
}

int fc_safe_to_autoplay(const FcBoard *b, Card c, int cheat_win)   /* SafeToAutoplay, §3 */
{
    if (c == FC_EMPTY) return 0;
    if (cheat_win) return 1;
    int r = fc_rank(c), s = fc_suit(c);
    if (r == 0) return 1;
    if (r == 1) return b->home_rank[s] == 0;
    if (b->home_rank[s] != r - 1) return 0;
    int a, o;
    if (fc_is_red(c)) { a = b->home_rank[0]; o = b->home_rank[3]; }
    else              { a = b->home_rank[1]; o = b->home_rank[2]; }
    return a != -1 && o != -1 && a >= r - 1 && o >= r - 1;
}

/* ---- Single-card steps ------------------------------------------------------------------------- */

/* Remove the card at (col,pos) of a tableau column, closing the gap (only the cheat removes from
 * the middle; normal moves take the exposed card). */
static Card col_remove(FcBoard *b, int col, int pos)
{
    Card c = b->board[col][pos];
    for (int i = pos; i < FC_COLLEN - 1; i++) b->board[col][i] = b->board[col][i + 1];
    b->board[col][FC_COLLEN - 1] = FC_EMPTY;
    return c;
}

static void col_insert(FcBoard *b, int col, int pos, Card c)
{
    for (int i = FC_COLLEN - 1; i > pos; i--) b->board[col][i] = b->board[col][i - 1];
    b->board[col][pos] = c;
}

void fc_step_apply(FcBoard *b, const FcStep *st)
{
    Card c = st->card;
    if (st->src_col == 0) b->board[0][st->src_pos] = FC_EMPTY;
    else col_remove(b, st->src_col, st->src_pos);
    if (st->dst_col == 0) {
        b->board[0][st->dst_pos] = c;
        if (st->dst_pos >= 4) {                                /* home: QueueMove 0x1004F51 / 0x1004FB9 */
            b->home_rank[fc_suit(c)] = (int8_t)fc_rank(c);
            if (fc_rank(c) == 0) b->suit_home_slot[fc_suit(c)] = st->dst_pos;
            b->cards_left--;
        }
    } else {
        col_insert(b, st->dst_col, st->dst_pos, c);
    }
}

void fc_step_unapply(FcBoard *b, const FcStep *st)      /* Undo, 0x1003FCD, one step */
{
    Card c = st->card;
    if (st->dst_col == 0) {
        if (st->dst_pos >= 4) {
            int r = fc_rank(c), s = fc_suit(c);
            b->home_rank[s] = (int8_t)(r - 1);
            b->board[0][st->dst_pos] = r > 0 ? c - 4 : FC_EMPTY;
            if (r == 0) b->suit_home_slot[s] = -1;          /* an undone ace frees its slot */
            b->cards_left++;
        } else {
            b->board[0][st->dst_pos] = FC_EMPTY;
        }
    } else {
        col_remove(b, st->dst_col, st->dst_pos);
    }
    if (st->src_col == 0) b->board[0][st->src_pos] = c;
    else col_insert(b, st->src_col, st->src_pos, c);
}

static void push_step(FcBoard *b, FcAction *a, int sc, int sp, int dc, int dp)
{
    FcStep st = { (int8_t)sc, (int8_t)sp, (int8_t)dc, (int8_t)dp, b->board[sc][sp] };
    fc_step_apply(b, &st);
    if (a && a->nsteps < FC_MAX_STEPS) a->steps[a->nsteps++] = st;
}

/* ---- Building actions (§2.6, §3, §4.2) ------------------------------------------------------- */

void fc_action_begin(FcAction *a, const FcBoard *b)
{
    a->before = *b;
    a->counted = 0;
    a->nsteps = 0;
}

/* QueueMove: tableau source = its exposed card (src_pos ignored, as XP); tableau destination = on top
 * of its exposed card (dst_pos ignored); top-row positions are slots 0..7. A move to itself (XP logs
 * it to mean "deselect") changes nothing and is not logged. Home sources are refused (§2.1). */
void fc_queue(FcBoard *b, FcAction *a, int sc, int sp, int dc, int dp)
{
    if (sc < 0 || sc > 8 || dc < 0 || dc > 8) return;
    if (sc == 0) {
        if (sp < 0 || sp > 3 || b->board[0][sp] == FC_EMPTY) return;
        if (dc == 0 && dp == sp) return;
    } else {
        sp = fc_last_index(b, sc);
        if (sp < 0 || sc == dc) return;
    }
    if (dc == 0) {
        if (dp < 0 || dp > 7) return;
    } else {
        dp = fc_last_index(b, dc) + 1;
        if (dp >= FC_COLLEN) return;
    }
    push_step(b, a, sc, sp, dc, dp);
}

void fc_move_run_via_free_cells(FcBoard *b, FcAction *a, int s, int d)   /* 0x100530A */
{
    int fc[4], nf = 0;
    for (int i = 0; i < 4; i++)
        if (b->board[0][i] == FC_EMPTY) fc[nf++] = i;
    int k = (s && d) ? fc_cards_to_move(b, s, d) : 1;
    if (k > nf + 1) k = nf + 1;
    for (int i = 0; i < k - 1; i++) fc_queue(b, a, s, 0, 0, fc[i]);       /* park */
    fc_queue(b, a, s, 0, d, 0);                                         /* base card */
    for (int i = k - 2; i >= 0; i--) fc_queue(b, a, 0, fc[i], d, 0);   /* unpark */
}

void fc_supermove(FcBoard *b, FcAction *a, int s, int d)                 /* SuperMove, 0x1005397 */
{
    int f = fc_free_cells_empty(b), n = fc_cards_to_move(b, s, d);
    if (n <= f + 1) { fc_move_run_via_free_cells(b, a, s, d); return; }
    int ec[8], ne = 0, j = 0;
    for (int c = 1; c <= 8; c++)
        if (b->board[c][0] == FC_EMPTY) ec[ne++] = c;
    do {
        if (j >= ne) return;          /* caller checks n <= capacity; never taken */
        fc_move_run_via_free_cells(b, a, s, ec[j++]);
        n -= f + 1;
    } while (n > f + 1);
    fc_move_run_via_free_cells(b, a, s, d);
    while (--j >= 0) fc_move_run_via_free_cells(b, a, ec[j], d);
}

/* ---- Standard supermove (extra) ------------------------------------------------------------- */

/* The bottom n cards of s onto d through the empty free cells only (n <= free cells + 1). */
static void move_via_free_cells_n(FcBoard *b, FcAction *a, int s, int d, int n)
{
    int fc[4], nf = 0;
    for (int i = 0; i < 4; i++)
        if (b->board[0][i] == FC_EMPTY) fc[nf++] = i;
    if (n > nf + 1) n = nf + 1;
    for (int i = 0; i < n - 1; i++) fc_queue(b, a, s, 0, 0, fc[i]);       /* park */
    fc_queue(b, a, s, 0, d, 0);                                         /* base card */
    for (int i = n - 2; i >= 0; i--) fc_queue(b, a, 0, fc[i], d, 0);   /* unpark */
}

/* Move n cards s -> d with f free cells and the empty columns ec[0..ne-1] as helpers. With one helper
 * column t: x cards go to t (using the other helpers), the remaining n - x go to d, then t's x cards
 * follow onto d. x is as small as possible (the cards parked on t move twice), and a helper that is
 * not needed is not used, so the step count stays low. */
static void move_std(FcBoard *b, FcAction *a, int s, int d, int n, const int *ec, int ne, int f)
{
    if (n <= f + 1 || ne <= 0) {
        move_via_free_cells_n(b, a, s, d, n);
        return;
    }
    int rest = fc_capacity_std(f, ne - 1);
    if (n <= rest) {                                     /* the last helper is not needed */
        move_std(b, a, s, d, n, ec, ne - 1, f);
        return;
    }
    int t = ec[0], x = n - rest;
    move_std(b, a, s, t, x, ec + 1, ne - 1, f);
    move_std(b, a, s, d, n - x, ec + 1, ne - 1, f);
    move_std(b, a, t, d, x, ec + 1, ne - 1, f);
}

int fc_move_cards_std(FcBoard *b, FcAction *a, int s, int d, int n)
{
    if (s < 1 || s > 8 || d < 1 || d > 8 || s == d || n < 1) return 0;
    int last = fc_last_index(b, s), run = 1;
    if (last < 0) return 0;
    while (last - run >= 0 && fc_can_stack(b->board[s][last - run + 1], b->board[s][last - run])) run++;
    if (n > run) return 0;                                /* not an ordered run */
    int dl = fc_last_index(b, d);
    if (dl >= 0 && !fc_can_stack(b->board[s][last - n + 1], b->board[d][dl])) return 0;
    if (dl + n >= FC_COLLEN) return 0;
    int ec[8], ne = 0, f = fc_free_cells_empty(b);
    for (int c = 1; c <= 8; c++)
        if (c != s && c != d && b->board[c][0] == FC_EMPTY) ec[ne++] = c;
    if (n > fc_capacity_std(f, ne)) return 0;
    move_std(b, a, s, d, n, ec, ne, f);
    return 1;
}

int fc_home_slot_for(FcBoard *b, int suit)                               /* HomeSlot */
{
    if (b->suit_home_slot[suit] == -1) {
        for (int i = 4; i < 8; i++)
            if (b->board[0][i] == FC_EMPTY) { b->suit_home_slot[suit] = (int8_t)i; break; }
    }
    return b->suit_home_slot[suit];
}

/* Ctrl+Shift+F10 "win" (§10), fixed: XP throws every exposed card onto its suit's home slot in any
 * order. Here every card still goes home, but in rank order (A..K per suit), taking each card from
 * wherever it lies (a buried card is pulled out of its column, which closes up), so the home piles
 * stay consistent and undo/replay work like any other action. At most 52 steps. */
void fc_cheat_sweep(FcBoard *b, FcAction *a)
{
    for (int r = 0; r < 13; r++)
        for (int s = 0; s < 4; s++) {
            if (b->home_rank[s] >= r) continue;
            Card want = r * 4 + s;
            for (int c = 0; c <= 8; c++) {
                int lim = c == 0 ? 4 : FC_COLLEN, p;
                for (p = 0; p < lim && b->board[c][p] != want; p++) {}
                if (p < lim) { push_step(b, a, c, p, 0, fc_home_slot_for(b, s)); break; }
            }
        }
}

void fc_autoplay(FcBoard *b, FcAction *a, int cheat_win)                 /* Autoplay, 0x10039F1 */
{
    if (cheat_win) { fc_cheat_sweep(b, a); return; }
    int moved;
    do {
        moved = 0;
        for (int i = 0; i < 4; i++) {
            Card c = b->board[0][i];
            if (fc_safe_to_autoplay(b, c, 0)) {
                fc_queue(b, a, 0, i, 0, fc_home_slot_for(b, fc_suit(c)));
                moved = 1;
            }
        }
        for (int col = 1; col <= 8; col++) {
            int last = fc_last_index(b, col);
            if (last < 0) continue;
            Card c = b->board[col][last];
            if (fc_safe_to_autoplay(b, c, 0)) {
                fc_queue(b, a, col, last, 0, fc_home_slot_for(b, fc_suit(c)));
                moved = 1;
            }
        }
    } while (moved);
}

void fc_undo_action(FcBoard *b, const FcAction *a) { *b = a->before; }

/* ---- Game over (§6) ---------------------------------------------------------------------------- */

int fc_count_moves_xp(const FcBoard *b)                                  /* CheckNoMoves, 0x100423C */
{
    for (int i = 0; i < 4; i++) if (b->board[0][i] == FC_EMPTY) return 2;
    for (int c = 1; c <= 8; c++) if (b->board[c][0] == FC_EMPTY) return 2;
    int m = 0;
    Card bot[9];
    for (int c = 1; c <= 8; c++) {
        bot[c] = b->board[c][fc_last_index(b, c)];
        if (fc_rank(bot[c]) == 0) m++;                                   /* ace: double-counted */
        if (b->home_rank[fc_suit(bot[c])] == fc_rank(bot[c]) - 1) m++;
    }
    for (int i = 0; i < 4; i++) {
        Card f = b->board[0][i];
        if (b->home_rank[fc_suit(f)] == fc_rank(f) - 1) m++;
    }
    for (int i = 0; i < 4; i++)
        for (int c = 1; c <= 8; c++) m += fc_can_stack(b->board[0][i], bot[c]);
    for (int d = 1; d <= 8; d++)
        for (int s = 1; s <= 8; s++) m += s != d && fc_can_stack(bot[s], bot[d]);
    return m > 1 ? 2 : m;
}

int fc_is_won(const FcBoard *b) { return b->cards_left == 0; }
