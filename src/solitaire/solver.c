/*
 * Solitaire HD — full-information Klondike solver: weighted best-first search over macro moves (see
 * solver.h).
 *
 * A position is an St: the columns (face-down cards as a count of the start column's, the face-up run),
 * the cards home per suit and the talon (the start's stock + waste sequence as a bit set, the waste
 * size, the recycles used). The search decodes canonical keys into St (columns in sorted order) and
 * plays candidate moves on copies. The solution is built by replaying the path's moves, found by card
 * identity, on an St in the board's real column order with a recorder attached: every primitive then
 * also emits the session actions (draws, recycles, turns, drags) and plays them on a real SolBoard
 * through sol_solve_apply, so the solution is checked move by move and must end in a won board.
 * Integer only; nothing is allocated after sol_solver_new except the solution buffers.
 */
#include "solver.h"

#include <stdlib.h>
#include <string.h>

#define NCOL      7
#define MAXTAL    24              /* stock + waste: 24 cards after the deal, never more */
#define MAXUP     13              /* a face-up run: K..A */
#define MAXFD     7               /* face-down cards per column (XP deals at most 6) */
#define MAXREC    7               /* recycle budgets 0..7 fit the key */
#define KEY_WORDS 8
#define NIL       0xFFFFFFFFu
#define SLOT_INDEX 0x00FFFFFFu    /* hash table slot: node index + 1; the rest a hash fingerprint */
#define PQ_SIZE   8192            /* priorities 0..PQ_SIZE-1 (clamped) */
#define MAX_CAND  256

typedef struct St {
    uint8_t  nd[NCOL];            /* face-down cards: the bottom nd of the start's column org */
    uint8_t  org[NCOL];
    uint8_t  nup[NCOL];           /* the face-up run, bottom first (card ids 0..51) */
    uint8_t  up[NCOL][MAXUP];
    uint8_t  home[4];             /* cards home per suit (the rank of the next card) */
    uint32_t mask;                /* talon: the start sequence's positions still in it */
    uint8_t  k;                   /* talon cards (bits in mask) */
    uint8_t  w;                   /* of them in the waste: the first w */
    uint8_t  rec;                 /* recycles used since the start (limited passes) */
    uint8_t  fdn;                 /* face-down cards in all columns */
} St;

typedef struct Node {
    uint32_t key[KEY_WORDS];
    uint32_t hash;
    uint32_t parent;              /* NIL for the root */
    uint32_t move;                /* packed move from the parent (PM below) */
    uint32_t next;                /* open-list bucket link */
    uint16_t g;                   /* solver moves from the root (saturating) */
    uint16_t spare;
} Node;

/* A move by card identity, so it means the same in every column order: bits 0-1 kind, 2-7 the card
 * (talon card; base of a tableau run; foundation card coming down), 8-13 the destination column's top
 * card (DEST_COL), 14-15 the destination. */
enum { PM_TALON = 1, PM_TAB = 2, PM_DOWN = 3 };
enum { DEST_HOME = 0, DEST_COL = 1, DEST_EMPTY = 2 };
#define PM(kind, card, dcard, dest) \
    ((uint32_t)(kind) | (uint32_t)(card) << 2 | (uint32_t)(dcard) << 8 | (uint32_t)(dest) << 14)
#define PM_KIND(m)  ((int)((m) & 3u))
#define PM_CARD(m)  ((int)(((m) >> 2) & 63u))
#define PM_DCARD(m) ((int)(((m) >> 8) & 63u))
#define PM_DEST(m)  ((int)(((m) >> 14) & 3u))

/* A candidate move on a decoded St. */
enum { C_TAB_HOME = 1, C_TALON = 2, C_TAB = 3, C_DOWN = 4 };
typedef struct Cand {
    uint8_t  kind;
    uint8_t  a;                   /* TAB_HOME, TAB: column; TALON: talon index; DOWN: suit */
    uint8_t  b;                   /* TAB: run index; TALON: 1 = a recycle is needed */
    int8_t   dest;                /* -1 = foundation, else the column */
    uint32_t pm;
} Cand;

/* Heuristic weights: priority = W_G * g + h(state), see heuristic(). Tuned on deals 100..499: draw one
 * with unlimited passes, then the other cases. */
enum { W_FD, W_DEPTH, W_HOME, W_TAL, W_G, W_COUNT };
static const int default_w[2][W_COUNT] = { { 3, 0, 1, 0, 2 }, { 6, 1, 2, 1, 1 } };

struct SolSolver {
    uint32_t  cap;                /* node pool size = node budget */
    uint32_t  mask;               /* hash table slots - 1 */
    Node     *nodes;
    uint32_t *table;              /* bits 0-23 node index + 1 (0 = free slot), 24-31 the hash's top byte */
    uint32_t *bucket;             /* PQ_SIZE list heads */
    uint32_t  nnodes, pmin;
    int       w[W_COUNT];
    int       tuned;              /* w set by sol_solver_tune, else default_w for the rules */
    int       unfiltered;         /* sol_solver_filters(sv, 0): every move, no dead-end test (checks) */
    int       all_moves;          /* the current search generates every legal move (a proof's check) */
    /* the current search */
    int       draw;               /* 1 or 3 */
    int       limit;              /* recycles allowed from the start, -1 = unlimited */
    uint8_t   fd[NCOL][MAXFD];    /* the start's face-down cards per column, bottom first */
    uint8_t   tal[MAXTAL];        /* the start's talon: waste bottom..top, then the stock top..bottom */
    int       ntal;
    /* the solution */
    uint32_t *pm;
    int       pm_cap;
    SolSolveMove *path;
    int       path_cap;
};

/* ---- Cards --------------------------------------------------------------------------------------- */

static inline int rank_of(int x) { return x >> 2; }
static inline int is_red(int suit) { return suit == 1 || suit == 2; }
static inline int stacks(int x, int y)        /* x may go onto y in the tableau */
{
    return rank_of(y) == rank_of(x) + 1 && is_red(x & 3) != is_red(y & 3);
}
static inline int playable(const St *s, int x) { return rank_of(x) == s->home[x & 3]; }

/* The safe-move rule (solver.h): aces and twos; else both opposite-colour suits home to rank r - 1 and
 * the other suit of the colour to rank r - 2 (home[] counts cards, so "rank q home" is home >= q + 1). */
static int safe(const St *s, int x)
{
    int r = rank_of(x), su = x & 3;
    if (r <= 1) return 1;
    int o1 = is_red(su) ? 0 : 1, o2 = is_red(su) ? 3 : 2;
    return s->home[o1] >= r && s->home[o2] >= r && s->home[3 - su] >= r - 1;
}

static inline int home_total(const St *s) { return s->home[0] + s->home[1] + s->home[2] + s->home[3]; }

/* ---- Talon --------------------------------------------------------------------------------------- */

/* The talon cards left, in sequence order (waste bottom first), and their start positions. */
static int talon_cards(const SolSolver *sv, uint32_t mask, uint8_t *card, uint8_t *pos)
{
    int k = 0;
    for (int i = 0; i < sv->ntal; i++)
        if (mask >> i & 1u) {
            card[k] = sv->tal[i];
            if (pos) pos[k] = (uint8_t)i;
            k++;
        }
    return k;
}

/* cost[j] for the talon cards: 0 = the waste top now or after some draws, 1 = only after a recycle (and
 * draws), 2 = unreachable. Draws move the waste size p by `draw` (the last packet may be smaller); the
 * card on top is then number p - 1. A recycle needs the stock empty (p = k), allows p = 0 again. */
static void talon_reach(const SolSolver *sv, const St *s, uint8_t *cost)
{
    int k = s->k, p = s->w, d = sv->draw;
    memset(cost, 2, (size_t)k);
    for (;;) {
        if (p > 0) cost[p - 1] = 0;
        if (p >= k) break;
        p = p + d < k ? p + d : k;
    }
    if (k > 0 && (sv->limit < 0 || s->rec < sv->limit)) {
        p = 0;
        do {
            p = p + d < k ? p + d : k;
            if (cost[p - 1] == 2) cost[p - 1] = 1;
        } while (p < k);
    }
}

/* ---- Recorder: the session actions of a replayed path ------------------------------------------- */

typedef struct Rec {
    SolSolver *sv;
    SolBoard   b;                 /* the real board, in step with the St */
    int        n;                 /* actions so far (in sv->path) */
    int        recycles;
    int        fail;
} Rec;

static void emit(Rec *r, int kind, int src, int index, int dst, int n, int card, int forced)
{
    SolSolver *sv = r->sv;
    if (r->fail) return;
    if (src < 0 || index < 0 || dst < 0 || n < 0) { r->fail = 1; return; }
    SolSolveMove m = { (uint8_t)kind, (uint8_t)src, (uint8_t)index, (uint8_t)dst, (uint8_t)n, (uint8_t)card,
                       (uint8_t)forced, 0 };
    if (!sol_solve_apply(&r->b, &m, sv->draw)) { r->fail = 1; return; }
    if (r->n == sv->path_cap) {
        int cap = sv->path_cap ? sv->path_cap * 2 : 512;
        SolSolveMove *p = realloc(sv->path, (size_t)cap * sizeof *p);
        if (!p) { r->fail = 1; return; }
        sv->path = p;
        sv->path_cap = cap;
    }
    sv->path[r->n++] = m;
}

/* The foundation pile of x's suit, or the leftmost empty one; -1 if none. */
static int found_pile(const SolBoard *b, int x)
{
    int empty = -1;
    for (int f = SOL_FOUND0; f < SOL_FOUND0 + 4; f++) {
        const SolPile *p = &b->p[f];
        if (p->n && sol_suit(p->c[0]) == (x & 3)) return f;
        if (!p->n && empty < 0) empty = f;
    }
    return empty;
}

/* Draw (and recycle) until card x is the waste top: the first time it gets there. */
static void bring_to_top(Rec *r, int x)
{
    for (int guard = 0; guard < 2 * MAXTAL + 4 && !r->fail; guard++) {
        const SolPile *w = &r->b.p[SOL_WASTE], *st = &r->b.p[SOL_STOCK];
        if (w->n && sol_card_id(w->c[w->n - 1]) == x) return;
        if (st->n) {
            int n = st->n < r->sv->draw ? st->n : r->sv->draw;
            emit(r, SOL_SM_DRAW, SOL_STOCK, 0, SOL_WASTE, n, sol_card_id(st->c[st->n - n]), 0);
        } else if (w->n) {
            if (r->sv->limit >= 0 && ++r->recycles > r->sv->limit) break;
            emit(r, SOL_SM_RECYCLE, SOL_STOCK, 0, SOL_STOCK, w->n, sol_card_id(w->c[0]), 0);
        } else {
            break;
        }
    }
    r->fail = 1;
}

/* ---- Primitives (r: also emit the actions; NULL in the search) ----------------------------------- */

static void st_turn(const SolSolver *sv, St *s, int c, Rec *r)
{
    int x = sv->fd[s->org[c]][--s->nd[c]];
    s->fdn--;
    s->up[c][0] = (uint8_t)x;
    s->nup[c] = 1;
    if (r) emit(r, SOL_SM_TURN, SOL_TAB0 + c, r->b.p[SOL_TAB0 + c].n - 1, SOL_TAB0 + c, 1, x, 0);
}

static void exposed(const SolSolver *sv, St *s, int c, Rec *r)
{
    if (!s->nup[c] && s->nd[c]) st_turn(sv, s, c, r);
}

static void st_tab_home(const SolSolver *sv, St *s, int c, Rec *r, int forced)
{
    int x = s->up[c][--s->nup[c]];
    s->home[x & 3]++;
    if (r) emit(r, SOL_SM_MOVE, SOL_TAB0 + c, r->b.p[SOL_TAB0 + c].n - 1, found_pile(&r->b, x), 1, x, forced);
    exposed(sv, s, c, r);
}

static void st_tab_tab(const SolSolver *sv, St *s, int c, int i, int d, Rec *r)
{
    int n = s->nup[c] - i, x = s->up[c][i];
    memcpy(&s->up[d][s->nup[d]], &s->up[c][i], (size_t)n);
    s->nup[d] = (uint8_t)(s->nup[d] + n);
    s->nup[c] = (uint8_t)i;
    if (r) emit(r, SOL_SM_MOVE, SOL_TAB0 + c, r->b.p[SOL_TAB0 + c].n - n, SOL_TAB0 + d, n, x, 0);
    exposed(sv, s, c, r);
}

static void st_down(St *s, int suit, int d, Rec *r)
{
    int x = (s->home[suit] - 1) * 4 + suit;
    s->home[suit]--;
    s->up[d][s->nup[d]++] = (uint8_t)x;
    if (r) {
        int f = found_pile(&r->b, x);
        emit(r, SOL_SM_MOVE, f, f >= 0 ? r->b.p[f].n - 1 : 0, SOL_TAB0 + d, 1, x, 0);
    }
}

/* Talon card number j (cost from talon_reach) to the foundation (dest < 0) or column dest. The waste
 * then holds the j cards before it. */
static void st_talon(const SolSolver *sv, St *s, int j, int cost, int dest, Rec *r, int forced)
{
    uint8_t card[MAXTAL], pos[MAXTAL];
    talon_cards(sv, s->mask, card, pos);
    int x = card[j];
    if (r) bring_to_top(r, x);
    s->mask &= ~(1u << pos[j]);
    s->k--;
    s->w = (uint8_t)j;
    if (cost == 1 && sv->limit >= 0) s->rec++;
    if (dest < 0) {
        s->home[x & 3]++;
        if (r) emit(r, SOL_SM_MOVE, SOL_WASTE, r->b.p[SOL_WASTE].n - 1, found_pile(&r->b, x), 1, x, forced);
    } else {
        s->up[dest][s->nup[dest]++] = (uint8_t)x;
        if (r) emit(r, SOL_SM_MOVE, SOL_WASTE, r->b.p[SOL_WASTE].n - 1, SOL_TAB0 + dest, 1, x, 0);
    }
}

/* The safe foundation moves, to the fixpoint (the result does not depend on the order: playing a safe
 * card keeps every other safe card playable and safe). */
static void st_auto(const SolSolver *sv, St *s, Rec *r)
{
    int moved;
    do {
        moved = 0;
        for (int c = 0; c < NCOL; c++)
            while (s->nup[c]) {
                int x = s->up[c][s->nup[c] - 1];
                if (!playable(s, x) || !safe(s, x)) break;
                st_tab_home(sv, s, c, r, 1);
                moved = 1;
            }
        if (sv->draw == 1 && s->k) {
            /* draw one: the waste top; with unlimited passes any talon card */
            uint8_t card[MAXTAL];
            int k = talon_cards(sv, s->mask, card, NULL);
            int lo = sv->limit < 0 ? 0 : s->w - 1, hi = sv->limit < 0 ? k - 1 : s->w - 1;
            for (int j = lo < 0 ? 0 : lo; j <= hi; j++)
                if (playable(s, card[j]) && safe(s, card[j])) {
                    st_talon(sv, s, j, 0, -1, r, 1);
                    moved = 1;
                    break;
                }
        }
    } while (moved);
}

/* Won without a search: every card face up and nothing left to dig out of the talon (an empty talon,
 * or draw one with unlimited passes, where every talon card can be reached at any time). The lowest
 * card not home is then always a column top or reachable, and playable. */
static int st_finished(const SolSolver *sv, const St *s)
{
    return s->fdn == 0 && (s->k == 0 || (sv->draw == 1 && sv->limit < 0));
}

static void st_finish(const SolSolver *sv, St *s, Rec *r)
{
    while (home_total(s) < 52 && !r->fail) {
        int x = -1, done = 0;
        for (int su = 0; su < 4; su++)
            if (s->home[su] < 13 && (x < 0 || s->home[su] < rank_of(x))) x = s->home[su] * 4 + su;
        for (int c = 0; c < NCOL && !done; c++)
            if (s->nup[c] && s->up[c][s->nup[c] - 1] == x) { st_tab_home(sv, s, c, r, 0); done = 1; }
        if (!done && s->k) {
            uint8_t card[MAXTAL];
            int k = talon_cards(sv, s->mask, card, NULL);
            for (int j = 0; j < k && !done; j++)
                if (card[j] == x) { st_talon(sv, s, j, 0, -1, r, 0); done = 1; }
        }
        if (!done) r->fail = 1;
    }
}

/* ---- Dead ends ------------------------------------------------------------------------------------
 * A card of rank 2..Q leaves a pile it is stuck in (a column under face-down or other cards, the waste
 * when no recycle is left: then it is a stack) only to its foundation, after the next lower card of its
 * suit, or onto one of the two cards it fits on (kings are left out: an empty column may come). When
 * that lower card lies below it in the same pile, and so do both of those cards, unless they are home
 * and safe (a winning line never needs a safe card back down: solver.h), it can never leave, and neither
 * can the cards under it: the position is lost. */

/* where[x]: pile (0..6 columns, 7 the waste) * 32 + height from the bottom, or 0xFF. */
static int below(const uint8_t *where, int z, int pile, int height)
{
    return where[z] != 0xFF && where[z] >> 5 == pile && (where[z] & 31) < height;
}

static int card_dead(const St *s, const uint8_t *where, int y)
{
    int r = rank_of(y), pile = where[y] >> 5, height = where[y] & 31;
    if (r == 0 || r == SOL_KING || height == 0 || !below(where, y - 4, pile, height)) return 0;
    int base = (r + 1) * 4, red = is_red(y & 3);
    for (int k = 0; k < 2; k++) {
        int p = base + (red ? (k ? 3 : 0) : (k ? 2 : 1));
        int home = s->home[p & 3] > rank_of(p);
        if (home ? !safe(s, p) : !below(where, p, pile, height)) return 0;
    }
    return 1;
}

/* The columns: every face-down card and the card on them. Only the start needs this: face-down cards
 * never move, and a column's bottom face-up card is always one that was face down there (or dealt). */
static int columns_dead(const SolSolver *sv, const St *s)
{
    uint8_t where[52];
    memset(where, 0xFF, sizeof where);
    for (int c = 0; c < NCOL; c++) {
        for (int i = 0; i < s->nd[c]; i++) where[sv->fd[s->org[c]][i]] = (uint8_t)(c << 5 | i);
        for (int i = 0; i < s->nup[c]; i++) where[s->up[c][i]] = (uint8_t)(c << 5 | (s->nd[c] + i));
    }
    for (int c = 0; c < NCOL; c++) {
        for (int i = 1; i < s->nd[c]; i++)
            if (card_dead(s, where, sv->fd[s->org[c]][i])) return 1;
        if (s->nd[c] && s->nup[c] && card_dead(s, where, s->up[c][0])) return 1;
    }
    return 0;
}

/* The waste once no recycle is left. */
static int waste_dead(const SolSolver *sv, const St *s)
{
    if (sv->limit < 0 || s->rec < sv->limit || s->w < 2) return 0;
    uint8_t where[52], card[MAXTAL];
    memset(where, 0xFF, sizeof where);
    talon_cards(sv, s->mask, card, NULL);
    for (int j = 0; j < s->w; j++) where[card[j]] = (uint8_t)(NCOL << 5 | j);
    for (int j = 1; j < s->w; j++)
        if (card_dead(s, where, card[j])) return 1;
    return 0;
}

/* ---- Canonical key -------------------------------------------------------------------------------
 * Words 0..6: the columns, sorted descending (empty columns = 0, last). A column word: bits 29-31 the
 * face-down count, 26-28 their start column (0 when none), 22-25 the run length (>= 1: exposed cards
 * are turned at once), 16-21 the run's base card, then bit 16 - i = suit bit (suit >> 1) of run card i
 * (its rank and colour follow from the base); bits 0-3 of words 0..3 then hold the cards home per suit
 * (redundant: the cards found nowhere else, kept to save the decoding). Word 7: the talon bit set
 * (0-23), the waste size (24-28; 0 with draw one and unlimited passes), the recycles used (29-31; 0 when
 * unlimited). */
static uint32_t col_word(const St *s, int c)
{
    int n = s->nup[c];
    if (!n) return 0;
    uint32_t w = (uint32_t)n << 22 | (uint32_t)s->up[c][0] << 16;
    if (s->nd[c]) w |= (uint32_t)s->nd[c] << 29 | (uint32_t)s->org[c] << 26;
    for (int i = 1; i < n; i++) w |= (uint32_t)(s->up[c][i] >> 1 & 1) << (16 - i);
    return w;
}

static void encode(const SolSolver *sv, const St *s, uint32_t *key)
{
    for (int c = 0; c < NCOL; c++) {
        uint32_t w = col_word(s, c);
        int i = c;
        while (i > 0 && key[i - 1] < w) { key[i] = key[i - 1]; i--; }
        key[i] = w;
    }
    for (int su = 0; su < 4; su++) key[su] |= s->home[su];
    uint32_t waste = sv->draw == 1 && sv->limit < 0 ? 0 : s->w;
    uint32_t rec = sv->limit < 0 ? 0 : s->rec;
    key[7] = s->mask | waste << 24 | rec << 29;
}

static void decode(const SolSolver *sv, const uint32_t *key, St *s)
{
    s->fdn = 0;
    for (int c = 0; c < NCOL; c++) {
        uint32_t w = key[c];
        int n = (int)(w >> 22 & 15u);
        s->nd[c] = (uint8_t)(w >> 29);
        s->org[c] = (uint8_t)(w >> 26 & 7u);
        s->nup[c] = (uint8_t)n;
        s->fdn = (uint8_t)(s->fdn + s->nd[c]);
        if (c < 4) s->home[c] = (uint8_t)(w & 15u);
        if (!n) continue;
        int base = (int)(w >> 16 & 63u), r = rank_of(base), red = is_red(base & 3);
        s->up[c][0] = (uint8_t)base;
        for (int i = 1; i < n; i++) {
            int bit = (int)(w >> (16 - i) & 1u), cred = red ^ (i & 1);
            s->up[c][i] = (uint8_t)((r - i) * 4 + (cred ? (bit ? 2 : 1) : (bit ? 3 : 0)));
        }
    }
    s->mask = key[7] & 0xFFFFFFu;
    s->w = (uint8_t)(key[7] >> 24 & 31u);
    s->rec = (uint8_t)(key[7] >> 29);
    uint32_t m = s->mask;                 /* population count, integer only */
    m = m - (m >> 1 & 0x55555555u);
    m = (m & 0x33333333u) + (m >> 2 & 0x33333333u);
    s->k = (uint8_t)(((m + (m >> 4)) & 0x0F0F0F0Fu) * 0x01010101u >> 24);
}

static uint32_t key_hash(const uint32_t *key)
{
    uint32_t h = 0x811C9DC5u;
    for (int i = 0; i < KEY_WORDS; i++) {
        h = (h ^ key[i]) * 0x9E3779B1u;
        h ^= h >> 15;
    }
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

/* ---- The start --------------------------------------------------------------------------------- */

/* The start position in the board's own column order; also records the face-down cards and the talon
 * sequence in sv. 0 if the board does not fit (SOL_SOLVE_INVALID). */
static int st_from_board(SolSolver *sv, const SolBoard *b, St *s)
{
    if (!sol_board_valid(b, NULL, 0)) return 0;
    memset(s, 0, sizeof *s);
    for (int c = 0; c < NCOL; c++) {
        const SolPile *p = &b->p[SOL_TAB0 + c];
        int nd = 0;
        while (nd < p->n && !sol_is_up(p->c[nd])) nd++;
        if (nd > MAXFD || p->n - nd > MAXUP) return 0;
        for (int i = 0; i < nd; i++) sv->fd[c][i] = (uint8_t)sol_card_id(p->c[i]);
        for (int i = nd; i < p->n; i++) s->up[c][i - nd] = (uint8_t)sol_card_id(p->c[i]);
        s->nd[c] = (uint8_t)nd;
        s->org[c] = (uint8_t)c;
        s->nup[c] = (uint8_t)(p->n - nd);
        s->fdn = (uint8_t)(s->fdn + nd);
    }
    for (int f = SOL_FOUND0; f < SOL_FOUND0 + 4; f++) {
        const SolPile *p = &b->p[f];
        if (!p->n) continue;
        if (s->home[sol_suit(p->c[0])]) return 0;
        s->home[sol_suit(p->c[0])] = p->n;
    }
    const SolPile *w = &b->p[SOL_WASTE], *st = &b->p[SOL_STOCK];
    if (w->n + st->n > MAXTAL) return 0;
    sv->ntal = 0;
    for (int i = 0; i < w->n; i++) sv->tal[sv->ntal++] = (uint8_t)sol_card_id(w->c[i]);
    for (int i = st->n; i-- > 0;) sv->tal[sv->ntal++] = (uint8_t)sol_card_id(st->c[i]);
    s->mask = sv->ntal ? 0xFFFFFFu >> (MAXTAL - sv->ntal) : 0;
    s->k = (uint8_t)sv->ntal;
    s->w = w->n;
    s->rec = 0;
    return 1;
}

/* ---- Moves --------------------------------------------------------------------------------------- */

static inline int twin(int x) { return (x & ~3) | (3 - (x & 3)); }   /* same rank and colour */

/* Where every card is, for the move filters. */
#define LOC_NONE 0xFF
#define JDEPTH   3                /* justification depth; deeper questions answer yes */
typedef struct Where {
    uint8_t loc[52];              /* face up in the tableau: column * 16 + index, else LOC_NONE */
    uint8_t reach[52];            /* a talon card that draws (and a recycle) can bring to the waste top */
    int     nempty;               /* empty columns */
    int     king_free;            /* a king could move into an empty column: one face up that is not
                                     the bottom card of its column, reachable, or on a foundation */
    int     talon_free;           /* draw one, unlimited passes: talon cards can wait */
} Where;

static int lands(const St *s, const Where *wh, int y, int nempty, int depth);

/* Either card that fits on x (one rank lower, the other colour) lands() on it. */
static int child_lands(const St *s, const Where *wh, int x, int nempty, int depth)
{
    if (rank_of(x) == 0) return 0;
    int base = (rank_of(x) - 1) * 4, red = is_red(x & 3);
    return lands(s, wh, base + (red ? 0 : 1), nempty, depth) || lands(s, wh, base + (red ? 3 : 2), nempty, depth);
}

/* The next card of x's suit could go home right after x: a column top or reachable. */
static int next_ready(const St *s, const Where *wh, int x)
{
    if (rank_of(x) == SOL_KING) return 0;
    int y = x + 4, l = wh->loc[y];
    return wh->reach[y] || (l != LOC_NONE && (l & 15) == s->nup[l >> 4] - 1);
}

/* Card number i of column c could go home, with a reason, once the cards on it are gone: it is
 * playable and safe; or not safe, and its going home uncovers a face-down card, empties the column
 * while none is empty (nempty) and a king could move in, lets the next card of the suit follow, or
 * exposes the card q under it for q to go home (with a reason) or for x's twin to land on q. */
static int home_ok(const St *s, const Where *wh, int c, int i, int nempty, int depth)
{
    int x = s->up[c][i];
    if (!playable(s, x)) return 0;
    if (safe(s, x) || depth >= JDEPTH || next_ready(s, wh, x)) return 1;
    if (i == 0) return s->nd[c] || (nempty == 0 && wh->king_free);
    return home_ok(s, wh, c, i - 1, nempty, depth + 1) || lands(s, wh, twin(x), nempty, depth + 1);
}

/* Could card y be moved, with a reason, onto a card that the move being considered exposes or places?
 * The move that lands y must itself make progress or prepare some at once (the postponement argument
 * below, one level deeper; questions deeper than JDEPTH answer yes, which only costs search time):
 *   y reachable in the talon: yes (with draw one and unlimited passes: a card lands on y in turn);
 *   y on top of a foundation: not when safe (it never comes down), else a card lands on y in turn;
 *   y the bottom card of a column: when that uncovers a face-down card, or empties the column while
 *     none is empty (nempty: after the move considered) and a king could move in;
 *   y on a card z in a run: moving y exposes z: when z goes home (home_ok), or y's twin lands on z;
 *   y face down, or a foundation card under another: no. */
static int lands(const St *s, const Where *wh, int y, int nempty, int depth)
{
    if (depth >= JDEPTH) return wh->reach[y] || s->home[y & 3] == rank_of(y) + 1 || wh->loc[y] != LOC_NONE;
    if (wh->reach[y]) return !wh->talon_free || child_lands(s, wh, y, nempty, depth + 1);
    if (s->home[y & 3] == rank_of(y) + 1) return !safe(s, y) && child_lands(s, wh, y, nempty, depth + 1);
    if (wh->loc[y] == LOC_NONE) return 0;
    int c = wh->loc[y] >> 4, i = wh->loc[y] & 15;
    if (i == 0) return s->nd[c] || (nempty == 0 && wh->king_free);
    return home_ok(s, wh, c, i - 1, nempty, depth + 1) || lands(s, wh, twin(y), nempty, depth + 1);
}

/* Every move from s that a winning line may need next (the safe ones are already played). A move that
 * only prepares something is generated only when that something can follow at once: any winning line
 * can be reordered so that it does, by postponing the preparing move until just before the first move
 * that relies on it (nothing in between can depend on it). The preparing moves and what they wait for:
 *   - a partial run, base X on the face-up card P, to the other card P' that takes it: P goes home, or
 *     X's twin X' (the other card of X's rank and colour; X' cannot lie on P or P') lands on P (lands);
 *     P moving on its own is the same as moving P with the run on it, which is generated;
 *   - a whole run off a column without face-down cards (the column empties): only while no column is
 *     empty and a king could move in;
 *   - a card home that is not safe, from the top of a run (exposing the face-up card Q under it) or the
 *     bottom of a column without face-down cards: Q goes home, X' lands on Q, the column is needed for
 *     a king (as above), or the next card of the suit can follow it home; with draw one and unlimited
 *     passes also from the talon: the next card of the suit can follow;
 *   - a foundation card back to the tableau (never a safe one: it would go straight back), and with draw
 *     one and unlimited passes a talon card to the tableau (any talon card can be played at any time):
 *     a card that fits on it lands there.
 * With draw three or a pass limit the talon position matters, and every talon move is generated. Runs
 * that uncover a face-down card are always generated. */
static int generate(const SolSolver *sv, const St *s, Cand *out)
{
    Where wh;
    uint8_t card[MAXTAL], cost[MAXTAL];
    int n = 0, empty = -1, k = 0;
    int talon_free = sv->draw == 1 && sv->limit < 0, f = !sv->unfiltered && !sv->all_moves;
    wh.talon_free = talon_free;
    memset(wh.loc, LOC_NONE, sizeof wh.loc);
    memset(wh.reach, 0, sizeof wh.reach);
    wh.nempty = 0;
    wh.king_free = 0;
    for (int c = NCOL; c-- > 0;)
        if (!s->nup[c] && !s->nd[c]) { empty = c; wh.nempty++; }
    for (int c = 0; c < NCOL; c++)
        for (int i = 0; i < s->nup[c]; i++) {
            int x = s->up[c][i];
            wh.loc[x] = (uint8_t)(c << 4 | i);
            if (rank_of(x) == SOL_KING && (i > 0 || s->nd[c])) wh.king_free = 1;
        }
    if (s->k) {
        k = talon_cards(sv, s->mask, card, NULL);
        talon_reach(sv, s, cost);
        for (int j = 0; j < k; j++)
            if (cost[j] < 2) {
                wh.reach[card[j]] = 1;
                if (rank_of(card[j]) == SOL_KING) wh.king_free = 1;
            }
    }
    for (int su = 0; su < 4; su++)
        if (s->home[su] == 13) wh.king_free = 1;

    /* tableau tops to the foundations */
    for (int c = 0; c < NCOL; c++) {
        int i = s->nup[c] - 1;
        if (i < 0) continue;
        int x = s->up[c][i];
        if (f ? !home_ok(s, &wh, c, i, wh.nempty, 0) : !playable(s, x)) continue;
        out[n++] = (Cand){ C_TAB_HOME, (uint8_t)c, 0, -1, PM(PM_TAB, x, 0, DEST_HOME) };
    }
    /* talon cards to the foundations and the tableau */
    for (int j = 0; j < k; j++) {
        if (cost[j] == 2) continue;
        int x = card[j];
        if (playable(s, x) && (!f || !talon_free || next_ready(s, &wh, x)))
            out[n++] = (Cand){ C_TALON, (uint8_t)j, cost[j], -1, PM(PM_TALON, x, 0, DEST_HOME) };
        if (rank_of(x) == SOL_KING) {
            if (empty >= 0 && (!f || !talon_free || child_lands(s, &wh, x, wh.nempty - 1, 0)))
                out[n++] = (Cand){ C_TALON, (uint8_t)j, cost[j], (int8_t)empty, PM(PM_TALON, x, 0, DEST_EMPTY) };
            continue;
        }
        if (f && talon_free && !child_lands(s, &wh, x, wh.nempty, 0)) continue;
        for (int d = 0; d < NCOL; d++)
            if (s->nup[d] && stacks(x, s->up[d][s->nup[d] - 1]))
                out[n++] = (Cand){ C_TALON, (uint8_t)j, cost[j], (int8_t)d,
                                   PM(PM_TALON, x, s->up[d][s->nup[d] - 1], DEST_COL) };
    }
    /* runs to other columns */
    for (int c = 0; c < NCOL; c++)
        for (int i = 0; i < s->nup[c]; i++) {
            int x = s->up[c][i];
            if (i > 0) {                                  /* partial: P = up[i - 1] is exposed */
                if (f && !home_ok(s, &wh, c, i - 1, wh.nempty, 1) && !lands(s, &wh, twin(x), wh.nempty, 1)) continue;
            } else if (!s->nd[c]) {                       /* the column empties */
                if (rank_of(x) == SOL_KING || (f && (wh.nempty || !wh.king_free))) continue;
            }
            if (rank_of(x) == SOL_KING) {
                if (empty >= 0)
                    out[n++] = (Cand){ C_TAB, (uint8_t)c, (uint8_t)i, (int8_t)empty, PM(PM_TAB, x, 0, DEST_EMPTY) };
                continue;
            }
            for (int d = 0; d < NCOL; d++)
                if (d != c && s->nup[d] && stacks(x, s->up[d][s->nup[d] - 1]))
                    out[n++] = (Cand){ C_TAB, (uint8_t)c, (uint8_t)i, (int8_t)d,
                                       PM(PM_TAB, x, s->up[d][s->nup[d] - 1], DEST_COL) };
        }
    /* foundation tops back to the tableau */
    for (int su = 0; su < 4; su++) {
        if (!s->home[su]) continue;
        int x = (s->home[su] - 1) * 4 + su;
        if (safe(s, x)) continue;
        if (rank_of(x) == SOL_KING) {
            if (empty >= 0 && (!f || child_lands(s, &wh, x, wh.nempty - 1, 0)))
                out[n++] = (Cand){ C_DOWN, (uint8_t)su, 0, (int8_t)empty, PM(PM_DOWN, x, 0, DEST_EMPTY) };
            continue;
        }
        if (f && !child_lands(s, &wh, x, wh.nempty, 0)) continue;
        for (int d = 0; d < NCOL; d++)
            if (s->nup[d] && stacks(x, s->up[d][s->nup[d] - 1]))
                out[n++] = (Cand){ C_DOWN, (uint8_t)su, 0, (int8_t)d, PM(PM_DOWN, x, s->up[d][s->nup[d] - 1], DEST_COL) };
    }
    return n;
}

static void apply(const SolSolver *sv, St *s, const Cand *c, Rec *r)
{
    switch (c->kind) {
    case C_TAB_HOME: st_tab_home(sv, s, c->a, r, 0); break;
    case C_TALON:    st_talon(sv, s, c->a, c->b, c->dest, r, 0); break;
    case C_TAB:      st_tab_tab(sv, s, c->a, c->b, c->dest, r); break;
    case C_DOWN:     st_down(s, c->a, c->dest, r); break;
    }
    st_auto(sv, s, r);
}

/* A packed move as a candidate on s (any column order). 0 if it does not fit. */
static int resolve(const SolSolver *sv, const St *s, uint32_t pm, Cand *c)
{
    int x = PM_CARD(pm), dest = PM_DEST(pm), d = -1;
    if (dest == DEST_COL) {
        for (int i = 0; i < NCOL && d < 0; i++)
            if (s->nup[i] && s->up[i][s->nup[i] - 1] == PM_DCARD(pm)) d = i;
    } else if (dest == DEST_EMPTY) {
        for (int i = NCOL; i-- > 0;)
            if (!s->nup[i] && !s->nd[i]) d = i;
    }
    if (dest != DEST_HOME && d < 0) return 0;
    memset(c, 0, sizeof *c);
    c->dest = (int8_t)d;
    c->pm = pm;
    switch (PM_KIND(pm)) {
    case PM_TALON: {
        uint8_t card[MAXTAL], cost[MAXTAL];
        int k = talon_cards(sv, s->mask, card, NULL);
        talon_reach(sv, s, cost);
        for (int j = 0; j < k; j++)
            if (card[j] == x) {
                if (cost[j] == 2) return 0;
                c->kind = C_TALON;
                c->a = (uint8_t)j;
                c->b = cost[j];
                return 1;
            }
        return 0;
    }
    case PM_TAB:
        for (int i = 0; i < NCOL; i++)
            for (int j = 0; j < s->nup[i]; j++)
                if (s->up[i][j] == x) {
                    if (dest == DEST_HOME) {
                        if (j != s->nup[i] - 1) return 0;
                        c->kind = C_TAB_HOME;
                    } else {
                        if (d == i) return 0;
                        c->kind = C_TAB;
                        c->b = (uint8_t)j;
                    }
                    c->a = (uint8_t)i;
                    return 1;
                }
        return 0;
    case PM_DOWN:
        if (dest == DEST_HOME || s->home[x & 3] != rank_of(x) + 1) return 0;
        c->kind = C_DOWN;
        c->a = (uint8_t)(x & 3);
        return 1;
    }
    return 0;
}

/* ---- Solver object ------------------------------------------------------------------------------- */

SolSolver *sol_solver_new(uint32_t max_nodes)
{
    if (max_nodes == 0) max_nodes = SOL_SOLVER_DEFAULT_NODES;
    if (max_nodes > SOL_SOLVER_MAX_NODES) max_nodes = SOL_SOLVER_MAX_NODES;
    if (max_nodes < 16) max_nodes = 16;
    SolSolver *sv = calloc(1, sizeof *sv);
    if (!sv) return NULL;
    uint32_t slots = 1;
    while (slots < 2 * max_nodes) slots <<= 1;
    sv->cap = max_nodes;
    sv->mask = slots - 1;
    sv->nodes = malloc((size_t)max_nodes * sizeof(Node));
    sv->table = calloc(slots, sizeof(uint32_t));
    sv->bucket = malloc(PQ_SIZE * sizeof(uint32_t));
    if (!sv->nodes || !sv->table || !sv->bucket) {
        sol_solver_free(sv);
        return NULL;
    }
    return sv;
}

void sol_solver_free(SolSolver *sv)
{
    if (!sv) return;
    free(sv->nodes);
    free(sv->table);
    free(sv->bucket);
    free(sv->pm);
    free(sv->path);
    free(sv);
}

size_t sol_solver_memory(const SolSolver *sv)
{
    if (!sv) return 0;
    return sizeof *sv + (size_t)sv->cap * sizeof(Node) + ((size_t)sv->mask + 1) * sizeof(uint32_t) +
           PQ_SIZE * sizeof(uint32_t) + (size_t)sv->pm_cap * sizeof(uint32_t) +
           (size_t)sv->path_cap * sizeof(SolSolveMove);
}

/* Declared under SOL_SOLVER_TUNING (solver.h) for the offline tool and the tests. */
void sol_solver_tune(SolSolver *sv, const int *w, int n)
{
    if (!w) { sv->tuned = 0; return; }
    for (int i = 0; i < n && i < W_COUNT; i++) sv->w[i] = w[i];
    sv->tuned = 1;
}

void sol_solver_filters(SolSolver *sv, int on)
{
    sv->unfiltered = !on;
}

/* ---- Search -------------------------------------------------------------------------------------- */

static int heuristic(const SolSolver *sv, const St *s)
{
    int depth = 0;
    for (int c = 0; c < NCOL; c++) depth += s->nd[c] * (s->nd[c] + 1) / 2;
    return sv->w[W_FD] * s->fdn + sv->w[W_DEPTH] * depth + sv->w[W_HOME] * (52 - home_total(s)) +
           sv->w[W_TAL] * s->k;
}

/* Empty the hash table: the previous search's slots one by one, newest first (each node's probe run
 * only crosses older nodes, so it is still intact when it is removed), or all at once when many. */
static void clear_table(SolSolver *sv)
{
    if (sv->nnodes > (sv->mask + 1) / 16) {
        memset(sv->table, 0, ((size_t)sv->mask + 1) * sizeof(uint32_t));
    } else {
        for (uint32_t k = sv->nnodes; k-- > 0;) {
            uint32_t i = sv->nodes[k].hash & sv->mask;
            while ((sv->table[i] & SLOT_INDEX) != k + 1) i = (i + 1) & sv->mask;
            sv->table[i] = 0;
        }
    }
    sv->nnodes = 0;
}

static void push(SolSolver *sv, uint32_t idx, int prio)
{
    if (prio < 0) prio = 0;
    if (prio >= PQ_SIZE) prio = PQ_SIZE - 1;
    sv->nodes[idx].next = sv->bucket[prio];
    sv->bucket[prio] = idx;
    if ((uint32_t)prio < sv->pmin) sv->pmin = (uint32_t)prio;
}

static uint32_t pop(SolSolver *sv)                  /* lowest priority, newest first among equals */
{
    while (sv->pmin < PQ_SIZE && sv->bucket[sv->pmin] == NIL) sv->pmin++;
    if (sv->pmin >= PQ_SIZE) return NIL;
    uint32_t idx = sv->bucket[sv->pmin];
    sv->bucket[sv->pmin] = sv->nodes[idx].next;
    return idx;
}

/* The index of the position with this key (existing, or new with *added = 1); NIL when it is new and
 * the pool is full. */
static uint32_t intern(SolSolver *sv, const uint32_t *key, int *added)
{
    uint32_t h = key_hash(key), i = h & sv->mask, fp = h & ~SLOT_INDEX;
    *added = 0;
    for (;;) {
        uint32_t t = sv->table[i];
        if (!t) break;
        if ((t & ~SLOT_INDEX) == fp) {
            const Node *nd = &sv->nodes[(t & SLOT_INDEX) - 1];
            if (nd->hash == h && memcmp(nd->key, key, sizeof nd->key) == 0) return (t & SLOT_INDEX) - 1;
        }
        i = (i + 1) & sv->mask;
    }
    if (sv->nnodes >= sv->cap) return NIL;
    uint32_t idx = sv->nnodes++;
    Node *nd = &sv->nodes[idx];
    memcpy(nd->key, key, sizeof nd->key);
    nd->hash = h;
    sv->table[i] = (idx + 1) | fp;
    *added = 1;
    return idx;
}

/* The solution: the packed moves from the root to node idx, then `last` (if has_last), replayed by card
 * identity on the start board with the recorder, then the finish. Returns the number of actions, -1 on
 * failure (out of memory; a replay that does not end in a won board would be a bug). */
static int build_path(SolSolver *sv, const SolBoard *start, uint32_t idx, uint32_t last, int has_last)
{
    int n = has_last ? 1 : 0;
    for (uint32_t i = idx; i != NIL && sv->nodes[i].parent != NIL; i = sv->nodes[i].parent) n++;
    if (n > sv->pm_cap) {
        uint32_t *p = realloc(sv->pm, (size_t)n * sizeof *p);
        if (!p) return -1;
        sv->pm = p;
        sv->pm_cap = n;
    }
    int k = n;
    if (has_last) sv->pm[--k] = last;
    for (uint32_t i = idx; i != NIL && sv->nodes[i].parent != NIL; i = sv->nodes[i].parent)
        sv->pm[--k] = sv->nodes[i].move;

    St s;
    Rec r;
    memset(&r, 0, sizeof r);
    r.sv = sv;
    r.b = *start;
    if (!st_from_board(sv, start, &s)) return -1;
    for (int c = 0; c < NCOL; c++) exposed(sv, &s, c, &r);
    st_auto(sv, &s, &r);
    for (k = 0; k < n && !r.fail; k++) {
        Cand c;
        if (!resolve(sv, &s, sv->pm[k], &c)) return -1;
        apply(sv, &s, &c, &r);
    }
    if (!r.fail) st_finish(sv, &s, &r);
    if (r.fail || !sol_is_won(&r.b)) return -1;
    return r.n;
}

/* The best-first search from root position s0 (start's St, its safe moves played). */
static int search(SolSolver *sv, const SolBoard *start, const St *s0, const volatile int *cancel,
                  SolSolveResult *res)
{
    St s = *s0;
    res->status = SOL_SOLVE_GAVE_UP;
    clear_table(sv);
    for (int i = 0; i < PQ_SIZE; i++) sv->bucket[i] = NIL;
    sv->pmin = PQ_SIZE;
    uint32_t key[KEY_WORDS];
    int added;
    encode(sv, &s, key);
    uint32_t root = intern(sv, key, &added);
    if (root == NIL) return res->status;
    sv->nodes[root].parent = NIL;
    sv->nodes[root].move = 0;
    sv->nodes[root].g = 0;
    push(sv, root, heuristic(sv, &s));

    Cand cand[MAX_CAND];
    St child;
    int status = SOL_SOLVE_UNSOLVABLE;
    uint32_t idx;
    while ((idx = pop(sv)) != NIL) {
        if (cancel && *cancel) { status = SOL_SOLVE_CANCELLED; break; }
        res->expanded++;
        decode(sv, sv->nodes[idx].key, &s);
        int g = sv->nodes[idx].g + 1, nc = generate(sv, &s, cand);
        if (g > 0xFFFF) g = 0xFFFF;
        for (int i = 0; i < nc; i++) {
            child = s;
            apply(sv, &child, &cand[i], NULL);
            if (st_finished(sv, &child)) {
                int n = build_path(sv, start, idx, cand[i].pm, 1);
                res->nodes = sv->nnodes;
                if (n < 0) return res->status;           /* out of memory (or a bug): GAVE_UP */
                res->status = SOL_SOLVE_SOLVED;
                res->nmoves = n;
                res->moves = sv->path;
                return res->status;
            }
            if (cand[i].kind == C_TALON && !sv->unfiltered && waste_dead(sv, &child)) continue;   /* burials */
            encode(sv, &child, key);
            uint32_t k = intern(sv, key, &added);
            if (k == NIL) { status = SOL_SOLVE_GAVE_UP; goto done; }
            if (!added) continue;
            Node *nd = &sv->nodes[k];
            nd->parent = idx;
            nd->move = cand[i].pm;
            nd->g = (uint16_t)g;
            push(sv, k, g * sv->w[W_G] + heuristic(sv, &child));
        }
    }
done:
    res->status = status;
    res->nodes = sv->nnodes;
    return status;
}

int sol_solve(SolSolver *sv, const SolBoard *start, int draw, int recycles_left, const volatile int *cancel,
              SolSolveResult *res)
{
    memset(res, 0, sizeof *res);
    res->status = SOL_SOLVE_GAVE_UP;
    if (!sv || !start) return res->status;
    sv->draw = draw == 1 ? 1 : 3;
    sv->limit = recycles_left < 0 ? -1 : recycles_left;
    if (!sv->tuned) memcpy(sv->w, default_w[sv->draw == 1 && sv->limit < 0 ? 0 : 1], sizeof sv->w);
    St s;
    if (recycles_left > MAXREC || !st_from_board(sv, start, &s)) {
        res->status = SOL_SOLVE_INVALID;
        return res->status;
    }
    if (sol_is_won(start)) {
        res->status = SOL_SOLVE_SOLVED;
        return res->status;
    }
    for (int c = 0; c < NCOL; c++) exposed(sv, &s, c, NULL);
    st_auto(sv, &s, NULL);
    if (!sv->unfiltered && !st_finished(sv, &s) && (columns_dead(sv, &s) || waste_dead(sv, &s))) {
        res->status = SOL_SOLVE_UNSOLVABLE;
        return res->status;
    }
    if (st_finished(sv, &s)) {
        int n = build_path(sv, start, NIL, 0, 0);
        if (n >= 0) {
            res->status = SOL_SOLVE_SOLVED;
            res->nmoves = n;
            res->moves = sv->path;
        }
        return res->status;
    }

    int status = search(sv, start, &s, cancel, res);
    if (status == SOL_SOLVE_UNSOLVABLE && !sv->unfiltered) {
        /* The move filters' postponement argument (generate) has gaps: a follow-up that needs two
         * preparing moves (3S home from the talon and 3D off 4S, for 4S home) has neither generated,
         * since neither lets it follow at once. So their proof is only a hint: it is checked by a search
         * over every legal move, the sound parts (safe moves, the dead-end tests) kept. */
        sv->all_moves = 1;
        status = search(sv, start, &s, cancel, res);      /* (expanded counts both searches) */
        sv->all_moves = 0;
    }
    return status;
}

int sol_solve_recycles_left(int scoring, int draw, int recycles)
{
    if (scoring != SOL_SCORING_VEGAS) return -1;
    int left = (draw == 1 ? 1 : 3) - 1 - recycles;
    return left < 0 ? 0 : left;
}

/* ---- Single actions ------------------------------------------------------------------------------ */

int sol_solve_apply(SolBoard *b, const SolSolveMove *m, int draw)
{
    if (!b || !m) return 0;
    draw = draw == 1 ? 1 : 3;
    SolPile *st = &b->p[SOL_STOCK], *w = &b->p[SOL_WASTE];
    switch (m->kind) {
    case SOL_SM_DRAW: {
        int n = st->n < draw ? st->n : draw;
        if (!n || m->n != n || m->card != sol_card_id(st->c[st->n - n])) return 0;
        for (int i = 0; i < n; i++) w->c[w->n++] = (SolCard)(st->c[--st->n] | SOL_UP);
        return 1;
    }
    case SOL_SM_RECYCLE:
        if (st->n || !w->n || m->n != w->n || m->card != sol_card_id(w->c[0])) return 0;
        while (w->n) st->c[st->n++] = (SolCard)(w->c[--w->n] & ~SOL_UP);
        return 1;
    case SOL_SM_TURN: {
        if (!sol_is_tab(m->src)) return 0;
        SolPile *p = &b->p[m->src];
        if (!p->n || m->index != p->n - 1 || m->card != sol_card_id(p->c[p->n - 1])) return 0;
        p->c[p->n - 1] |= SOL_UP;
        return 1;
    }
    case SOL_SM_MOVE: {
        int src = m->src, idx = m->index, dst = m->dst;
        if (src <= SOL_STOCK || src >= SOL_NPILES || dst >= SOL_NPILES) return 0;
        SolPile *p = &b->p[src], *q = &b->p[dst];
        if (idx >= p->n || !sol_is_up(p->c[idx])) return 0;
        if ((src == SOL_WASTE || sol_is_found(src)) && idx != p->n - 1) return 0;
        if (m->n != p->n - idx || m->card != sol_card_id(p->c[idx])) return 0;
        if (!sol_can_drop(b, dst, src, idx)) return 0;
        memcpy(&q->c[q->n], &p->c[idx], (size_t)m->n);
        q->n = (uint8_t)(q->n + m->n);
        p->n = (uint8_t)idx;
        return 1;
    }
    }
    return 0;
}
