/*
 * FreeCell HD — solver: weighted best-first search over session actions (see solver.h).
 *
 * Every move is played with the game.c primitives exactly as session.c click_move / check_target play
 * a click pair, followed by fc_autoplay. The only session logic restated here is which primitive a
 * click pair runs and when the MoveCol dialog appears (play, empty_column_offer, empty_column_take);
 * tests/test_solver.c checks the move set against every click pair the session accepts. Integer only;
 * nothing is allocated after fc_solver_new except the solution buffers.
 */
#include "solver.h"
#include "session.h"   /* FCS_MOVECOL_* */

#include <stdlib.h>
#include <string.h>

/* ---- Canonical state ----------------------------------------------------------------------------
 * 60 bytes: [0..3] the free cells ascending (0xFF = empty, so empties sort last); [4..7] per suit the
 * home pile size (home_rank + 1, which is also the rank of the next card the pile needs); [8..] the
 * cards of the non-empty columns, columns ordered by their deepest card, each column deepest first,
 * a column's exposed card flagged with 0x80; the remaining bytes 0xFF. The number of column cards is
 * 52 - home - free cells. Equal bytes <=> the same position up to free-cell and column order. */
#define ST_BYTES 60
#define ST_HOME  4
#define ST_COLS  8
#define ST_LAST  0x80u
#define ST_NONE  0xFFu

typedef struct Node {
    uint8_t  st[ST_BYTES];
    uint32_t hash;
    uint32_t parent;          /* NIL for the root */
    uint32_t move;            /* packed move from the parent (PM below) */
    uint32_t next;            /* open-list bucket link */
    uint16_t g;               /* user actions from the root (saturating) */
    uint16_t spare;
} Node;

#define NIL      0xFFFFFFFFu
#define PQ_SIZE  8192         /* priorities 0..PQ_SIZE-1 (clamped) */

/* A move by card identity, so it means the same in every column/free-cell order of a state:
 * bits 0-2 kind, 3-8 the card (source; the base of a moved run), 9-14 the exposed card of the
 * destination column (FC_SM_COLUMN), 15-16 the dialog (DLG_*), 17-22 cards moved. */
enum { DLG_NONE = 0, DLG_SINGLE = 1, DLG_COLUMN = 2 };
#define PM(kind, card, dcard, dlg, n) \
    ((uint32_t)(kind) | (uint32_t)(card) << 3 | (uint32_t)(dcard) << 9 | (uint32_t)(dlg) << 15 | (uint32_t)(n) << 17)
#define PM_KIND(m)  ((int)((m) & 7u))
#define PM_CARD(m)  ((Card)(((m) >> 3) & 63u))
#define PM_DCARD(m) ((Card)(((m) >> 9) & 63u))
#define PM_DLG(m)   ((int)(((m) >> 15) & 3u))
#define PM_N(m)     ((int)(((m) >> 17) & 63u))

/* Heuristic weights: priority = W_G * g + h(state), see heuristic(). */
enum { W_LEFT, W_BLOCK, W_SEQ, W_DEPTH, W_FREE, W_EMPTY, W_G, W_COUNT };
static const int default_w[W_COUNT] = { 4, 12, 4, 2, 12, 8, 6 };

struct FcSolver {
    uint32_t  cap;            /* node pool size = node budget */
    uint32_t  mask;           /* hash table slots - 1 */
    Node     *nodes;
    uint32_t *table;          /* node index + 1, 0 = free slot */
    uint32_t *bucket;         /* PQ_SIZE list heads */
    uint32_t  nnodes, pmin;
    int       path_cap;       /* solution buffers, grown on demand */
    uint32_t *pm;
    FcSolveMove *path;
    int       w[W_COUNT];
    FcBoard   blank;
};

/* ---- Playing a move as the session does --------------------------------------------------------- */

/* session.c check_target, tableau column sc -> empty column dc: the number of cards the click offers
 * to move (1 = a single card without the dialog; >= 2 = the MoveCol dialog appears). */
static int empty_column_offer(const FcBoard *b, int sc, int dc, int std)
{
    int n = fc_cards_to_move(b, sc, dc);
    if (std) {
        int m = fc_max_to_empty(b, 1);
        if (n > m) n = m;
    } else if (fc_free_cells_empty(b) == 0 && n > 1) {
        n = 1;
    }
    return n;
}

/* Cards the "Move column" answer moves (session.c click_move): XP min(run, f+1) through the free
 * cells only (fc_move_run_via_free_cells), standard the offer itself. */
static int empty_column_take(const FcBoard *b, int sc, int dc, int std)
{
    if (std) return empty_column_offer(b, sc, dc, 1);
    int n = fc_cards_to_move(b, sc, dc), f = fc_free_cells_empty(b) + 1;
    return n < f ? n : f;
}

/* The user's part of a click pair, exactly as session.c click_move runs it, then autoplay. */
static void play(FcBoard *b, int kind, int sc, int sp, int dc, int dp, int dlg, int std)
{
    switch (kind) {
    case FC_SM_HOME:
    case FC_SM_FREECELL:
        fc_queue(b, NULL, sc, sp, 0, dp);
        break;
    case FC_SM_COLUMN:
        if (sc == 0) fc_queue(b, NULL, 0, sp, dc, 0);
        else if (std) fc_move_cards_std(b, NULL, sc, dc, fc_cards_to_move(b, sc, dc));
        else fc_supermove(b, NULL, sc, dc);
        break;
    case FC_SM_EMPTY:
        if (sc == 0 || dlg != DLG_COLUMN) fc_queue(b, NULL, sc, sp, dc, 0);
        else if (std) fc_move_cards_std(b, NULL, sc, dc, empty_column_take(b, sc, dc, 1));
        else fc_move_run_via_free_cells(b, NULL, sc, dc);
        break;
    default:                                         /* FC_SM_AUTOPLAY: a deselect */
        break;
    }
    fc_autoplay(b, NULL, 0);
}

/* fc_can_stack without the empty checks; only for the heuristic and the generator's quick filter. */
static int stacks(int src, int dst)
{
    return (dst >> 2) - (src >> 2) == 1 && ((0x6 >> (src & 3)) & 1) != ((0x6 >> (dst & 3)) & 1);
}

static int can_home(const FcBoard *b, Card c)        /* check_target, home destination */
{
    return fc_rank(c) == 0 || b->home_rank[fc_suit(c)] == fc_rank(c) - 1;
}

static int home_slot(const FcBoard *b, Card c)       /* the slot to click: the suit's, else any empty */
{
    int s = b->suit_home_slot[fc_suit(c)];
    for (int i = 4; s < 0 && i < 8; i++)
        if (b->board[0][i] == FC_EMPTY) s = i;
    return s;
}

/* ---- Canonical encoding ------------------------------------------------------------------------- */

static void encode(const FcBoard *b, uint8_t *st)
{
    uint8_t f[4];
    for (int i = 0; i < 4; i++) f[i] = b->board[0][i] == FC_EMPTY ? ST_NONE : (uint8_t)b->board[0][i];
#define CSWAP(x, y) do { if (f[x] > f[y]) { uint8_t t_ = f[x]; f[x] = f[y]; f[y] = t_; } } while (0)
    CSWAP(0, 1); CSWAP(2, 3); CSWAP(0, 2); CSWAP(1, 3); CSWAP(1, 2);
#undef CSWAP
    memcpy(st, f, 4);
    for (int s = 0; s < 4; s++) st[ST_HOME + s] = (uint8_t)(b->home_rank[s] + 1);
    int order[8], n = 0;
    for (int c = 1; c <= 8; c++) {                   /* non-empty columns by their deepest card */
        Card top = b->board[c][0];
        if (top == FC_EMPTY) continue;
        int k = n++;
        while (k > 0 && b->board[order[k - 1]][0] > top) { order[k] = order[k - 1]; k--; }
        order[k] = c;
    }
    int p = ST_COLS;
    for (int k = 0; k < n; k++) {
        const Card *col = b->board[order[k]];
        int i = 0;
        while (i + 1 < FC_COLLEN && col[i + 1] != FC_EMPTY && p < ST_BYTES - 1) st[p++] = (uint8_t)col[i++];
        if (p < ST_BYTES) st[p++] = (uint8_t)(col[i] | ST_LAST);
    }
    while (p < ST_BYTES) st[p++] = ST_NONE;
}

static void decode(const FcSolver *sv, const uint8_t *st, FcBoard *b)
{
    *b = sv->blank;
    int left = 52;                                   /* cards not home */
    for (int s = 0; s < 4; s++) {
        int r = st[ST_HOME + s] - 1;                 /* home slots: any fixed suit -> slot map will do */
        b->home_rank[s] = (int8_t)r;
        if (r >= 0) {
            b->suit_home_slot[s] = (int8_t)(4 + s);
            b->board[0][4 + s] = r * 4 + s;
        }
        left -= r + 1;
    }
    b->cards_left = left;
    for (int i = 0; i < 4; i++)
        if (st[i] != ST_NONE) { b->board[0][i] = st[i]; left--; }
    int c = 1, i = 0;
    for (int p = ST_COLS; p < ST_COLS + left && c <= 8; p++) {
        b->board[c][i++] = st[p] & 63;
        if (st[p] & ST_LAST) { c++; i = 0; }
    }
}

static uint32_t rotl(uint32_t x, int r) { return x << r | x >> (32 - r); }

static uint32_t st_hash(const uint8_t *st)          /* MurmurHash3-style, byte order independent */
{
    uint32_t h = 0x9747B28Cu;
    for (int i = 0; i < ST_BYTES; i += 4) {
        uint32_t k = (uint32_t)st[i] | (uint32_t)st[i + 1] << 8 | (uint32_t)st[i + 2] << 16 | (uint32_t)st[i + 3] << 24;
        k *= 0xCC9E2D51u;
        k = rotl(k, 15);
        k *= 0x1B873593u;
        h ^= k;
        h = rotl(h, 13);
        h = h * 5u + 0xE6546B64u;
    }
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

/* ---- Heuristic ------------------------------------------------------------------------------------
 * Estimated remaining work, weighted: cards not home; column cards above a lower card of their column
 * (they must move before it can go home), less for those resting on their natural parent (they can
 * move with it in one supermove); the cards covering the next card each home pile needs; occupied
 * free cells; minus empty columns. */
static int heuristic(const int *w, const uint8_t *st)
{
    int left = 52 - st[ST_HOME] - st[ST_HOME + 1] - st[ST_HOME + 2] - st[ST_HOME + 3];
    int occupied = (st[0] != ST_NONE) + (st[1] != ST_NONE) + (st[2] != ST_NONE) + (st[3] != ST_NONE);
    int end = ST_COLS + left - occupied, minr = 99, prev = 0xFF, cols = 0;
    int blockers = 0, seq = 0, depth = 0, need = 0, needpos = 0;
    for (int p = ST_COLS; p < end; p++) {            /* branch-light: this runs for every new state */
        int v = st[p], c = v & 63, r = c >> 2;
        int above = r > minr;                        /* above a lower card of its column */
        int nat = (prev >> 2) - r == 1 && ((0x6 >> (c & 3) ^ 0x6 >> (prev & 3)) & 1);
        seq += above & nat;
        blockers += above & !nat;
        minr = above ? minr : r;
        int nx = st[ST_HOME + (c & 3)] == r;         /* the next card its home pile needs */
        need += nx;
        needpos += p & -nx;
        prev = c;
        if (v & ST_LAST) {                           /* column end: cards covering the needed ones */
            depth += need * p - needpos;
            need = needpos = 0;
            minr = 99;
            prev = 0xFF;
            cols++;
        }
    }
    int h = w[W_LEFT] * left + w[W_BLOCK] * blockers + w[W_SEQ] * seq + w[W_DEPTH] * depth +
            w[W_FREE] * occupied - w[W_EMPTY] * (8 - cols);
    return h > 0 ? h : 0;
}

/* ---- Move generation ------------------------------------------------------------------------------ */

typedef struct Cand {
    int8_t   kind, sc, sp, dc, dp, dlg;
    uint32_t pm;
} Cand;

static int add(Cand *out, int n, int kind, int sc, int sp, int dc, int dp, int dlg, Card card, Card dcard, int k)
{
    Cand *c = &out[n];
    c->kind = (int8_t)kind;
    c->sc = (int8_t)sc; c->sp = (int8_t)sp; c->dc = (int8_t)dc; c->dp = (int8_t)dp;
    c->dlg = (int8_t)dlg;
    c->pm = PM(kind, card, dcard < 0 ? 0 : dcard, dlg, k);
    return n + 1;
}

/* Every session action from b that can change the position up to free-cell and column order (at most
 * 4*(1+8+1) + 8*(1+1+7+2) + 1 = 129). Left out: free cell -> free cell, a whole column or a lone card
 * into an empty column, and a deselect (autoplay only) unless with_autoplay (a fresh deal). */
static int generate(const FcBoard *b, int std, int with_autoplay, Cand *out)
{
    int last[9], f = 0, ffc = -1, e = 0, fec = -1, n = 0;
    Card bot[9];
    for (int i = 3; i >= 0; i--)
        if (b->board[0][i] == FC_EMPTY) { f++; ffc = i; }
    for (int c = 8; c >= 1; c--) {
        last[c] = fc_last_index(b, c);
        bot[c] = last[c] >= 0 ? b->board[c][last[c]] : FC_EMPTY;
        if (last[c] < 0) { e++; fec = c; }
    }
    int maxm = std ? fc_capacity_std(f, e) : fc_capacity(f, e);   /* fc_max_movable_rule */

    for (int i = 0; i < 4; i++) {                                 /* free cell sources */
        Card x = b->board[0][i];
        if (x == FC_EMPTY) continue;
        if (can_home(b, x)) n = add(out, n, FC_SM_HOME, 0, i, 0, home_slot(b, x), DLG_NONE, x, -1, 1);
        for (int d = 1; d <= 8; d++)
            if (last[d] >= 0 && fc_can_stack(x, bot[d]))
                n = add(out, n, FC_SM_COLUMN, 0, i, d, last[d], DLG_NONE, x, bot[d], 1);
        if (e) n = add(out, n, FC_SM_EMPTY, 0, i, fec, -1, DLG_NONE, x, -1, 1);
    }
    for (int c = 1; c <= 8; c++) {                                /* column sources */
        if (last[c] < 0) continue;
        Card x = bot[c];
        int run = 1;
        while (run <= last[c] && stacks(b->board[c][last[c] - run + 1], b->board[c][last[c] - run])) run++;
        if (can_home(b, x)) n = add(out, n, FC_SM_HOME, c, last[c], 0, home_slot(b, x), DLG_NONE, x, -1, 1);
        if (f) n = add(out, n, FC_SM_FREECELL, c, last[c], 0, ffc, DLG_NONE, x, -1, 1);
        for (int d = 1; d <= 8; d++) {
            if (d == c || last[d] < 0) continue;
            int k = fc_rank(bot[d]) - fc_rank(x);                 /* the card that fits is k-1 above x */
            if (k < 1 || k > run) continue;
            k = fc_cards_to_move(b, c, d);
            if (k >= 1 && k <= maxm)
                n = add(out, n, FC_SM_COLUMN, c, last[c], d, last[d], DLG_NONE, b->board[c][last[c] - k + 1], bot[d], k);
        }
        if (e && last[c] > 0) {
            int offer = empty_column_offer(b, c, fec, std);
            n = add(out, n, FC_SM_EMPTY, c, last[c], fec, -1, offer > 1 ? DLG_SINGLE : DLG_NONE, x, -1, 1);
            if (offer > 1) {
                int k = empty_column_take(b, c, fec, std);
                if (k > 1 && k <= last[c])
                    n = add(out, n, FC_SM_EMPTY, c, last[c], fec, -1, DLG_COLUMN, b->board[c][last[c] - k + 1], -1, k);
            }
        }
    }
    if (with_autoplay) {                                          /* select a card, click it again */
        int sc = -1, sp = -1;
        for (int c = 1; c <= 8 && sc < 0; c++)
            if (last[c] >= 0) { sc = c; sp = last[c]; }
        for (int i = 0; i < 4 && sc < 0; i++)
            if (b->board[0][i] != FC_EMPTY) { sc = 0; sp = i; }
        if (sc >= 0) n = add(out, n, FC_SM_AUTOPLAY, sc, sp, sc, sp, DLG_NONE, b->board[sc][sp], -1, 0);
    }
    return n;
}

static int needs_autoplay(const FcBoard *b)          /* a fresh deal: autoplay has not run yet */
{
    FcBoard t = *b;
    fc_autoplay(&t, NULL, 0);
    return t.cards_left != b->cards_left;
}

static void to_move(const Cand *c, FcSolveMove *m)
{
    int dlg = c->dlg;
    m->src_col = c->sc; m->src_pos = c->sp;
    m->dst_col = c->dc; m->dst_pos = c->dp;
    m->movecol = (int8_t)(dlg == DLG_COLUMN ? FCS_MOVECOL_COLUMN : dlg == DLG_SINGLE ? FCS_MOVECOL_SINGLE : FC_SM_NODIALOG);
    m->kind = c->kind;
    m->ncards = (int8_t)PM_N(c->pm);
    m->reserved = 0;
    m->card = PM_CARD(c->pm);
}

/* ---- Solver object -------------------------------------------------------------------------------- */

FcSolver *fc_solver_new(uint32_t max_nodes)
{
    if (max_nodes == 0) max_nodes = FC_SOLVER_DEFAULT_NODES;
    if (max_nodes > FC_SOLVER_MAX_NODES) max_nodes = FC_SOLVER_MAX_NODES;
    uint32_t slots = 1024;
    while (slots < 2u * max_nodes) slots <<= 1;
    FcSolver *sv = calloc(1, sizeof *sv);
    if (!sv) return NULL;
    sv->cap = max_nodes;
    sv->mask = slots - 1;
    sv->nodes = malloc((size_t)max_nodes * sizeof(Node));
    sv->table = calloc(slots, sizeof(uint32_t));
    sv->bucket = malloc(PQ_SIZE * sizeof(uint32_t));
    if (!sv->nodes || !sv->table || !sv->bucket) {
        fc_solver_free(sv);
        return NULL;
    }
    memcpy(sv->w, default_w, sizeof sv->w);
    fc_board_clear(&sv->blank);
    return sv;
}

static void free_path(FcSolver *sv)
{
    free(sv->pm);
    free(sv->path);
    sv->pm = NULL;
    sv->path = NULL;
    sv->path_cap = 0;
}

void fc_solver_free(FcSolver *sv)
{
    if (!sv) return;
    free(sv->nodes);
    free(sv->table);
    free(sv->bucket);
    free_path(sv);
    free(sv);
}

size_t fc_solver_memory(const FcSolver *sv)
{
    if (!sv) return 0;
    return sizeof *sv + (size_t)sv->cap * sizeof(Node) + ((size_t)sv->mask + 1) * sizeof(uint32_t) +
           PQ_SIZE * sizeof(uint32_t) +
           (size_t)sv->path_cap * (sizeof(uint32_t) + sizeof(FcSolveMove));
}

#ifdef FC_SOLVER_TUNING
void fc_solver_tune(FcSolver *sv, const int *w, int n)
{
    for (int i = 0; i < n && i < W_COUNT; i++) sv->w[i] = w[i];
}
#endif

/* ---- Search ---------------------------------------------------------------------------------------- */

/* Empty the hash table: the previous search's slots one by one, newest first (each node's probe run
 * only crosses older nodes, so it is still intact when it is removed), or all at once when many. */
static void clear_table(FcSolver *sv)
{
    if (sv->nnodes > (sv->mask + 1) / 16) {
        memset(sv->table, 0, ((size_t)sv->mask + 1) * sizeof(uint32_t));
    } else {
        for (uint32_t k = sv->nnodes; k-- > 0;) {
            uint32_t i = sv->nodes[k].hash & sv->mask;
            while (sv->table[i] != k + 1) i = (i + 1) & sv->mask;
            sv->table[i] = 0;
        }
    }
    sv->nnodes = 0;
}

static void push(FcSolver *sv, uint32_t idx, int prio)
{
    if (prio < 0) prio = 0;
    if (prio >= PQ_SIZE) prio = PQ_SIZE - 1;
    sv->nodes[idx].next = sv->bucket[prio];
    sv->bucket[prio] = idx;
    if ((uint32_t)prio < sv->pmin) sv->pmin = (uint32_t)prio;
}

static uint32_t pop(FcSolver *sv)                    /* lowest priority, newest first among equals */
{
    while (sv->pmin < PQ_SIZE && sv->bucket[sv->pmin] == NIL) sv->pmin++;
    if (sv->pmin >= PQ_SIZE) return NIL;
    uint32_t idx = sv->bucket[sv->pmin];
    sv->bucket[sv->pmin] = sv->nodes[idx].next;
    return idx;
}

/* The index of state st (existing, or new with *added = 1); NIL when it is new and the pool is full. */
static uint32_t intern(FcSolver *sv, const uint8_t *st, int *added)
{
    uint32_t h = st_hash(st), i = h & sv->mask;
    *added = 0;
    for (;;) {
        uint32_t k = sv->table[i];
        if (!k) break;
        const Node *nd = &sv->nodes[k - 1];
        if (nd->hash == h && memcmp(nd->st, st, ST_BYTES) == 0) return k - 1;
        i = (i + 1) & sv->mask;
    }
    if (sv->nnodes >= sv->cap) return NIL;
    uint32_t idx = sv->nnodes++;
    Node *nd = &sv->nodes[idx];
    memcpy(nd->st, st, ST_BYTES);
    nd->hash = h;
    sv->table[i] = idx + 1;
    *added = 1;
    return idx;
}

/* Find a packed move's cards on a real board and fill in the clicks. 0 if it does not fit. */
static int resolve(const FcBoard *b, uint32_t pm, FcSolveMove *m)
{
    int kind = PM_KIND(pm), sc = -1, sp = -1, dc = -1, dp = -1, dlg = PM_DLG(pm);
    Card card = PM_CARD(pm);
    for (int i = 0; i < 4 && sc < 0; i++)
        if (b->board[0][i] == card) { sc = 0; sp = i; }
    for (int c = 1; c <= 8 && sc < 0; c++)
        for (int p = 0; p < FC_COLLEN && b->board[c][p] != FC_EMPTY; p++)
            if (b->board[c][p] == card) { sc = c; sp = fc_last_index(b, c); break; }
    if (sc < 0) return 0;
    switch (kind) {
    case FC_SM_HOME:
        dc = 0; dp = home_slot(b, card);
        break;
    case FC_SM_FREECELL:
        dc = 0;
        for (int i = 3; i >= 0; i--) if (b->board[0][i] == FC_EMPTY) dp = i;
        break;
    case FC_SM_COLUMN:
        for (int c = 1; c <= 8 && dc < 0; c++) {
            int l = fc_last_index(b, c);
            if (l >= 0 && b->board[c][l] == PM_DCARD(pm)) { dc = c; dp = l; }
        }
        break;
    case FC_SM_EMPTY:
        for (int c = 8; c >= 1; c--) if (b->board[c][0] == FC_EMPTY) dc = c;
        break;
    default:
        dc = sc; dp = sp;
        break;
    }
    if (dc < 0 || (dc == 0 && dp < 0)) return 0;
    Cand c = { (int8_t)kind, (int8_t)sc, (int8_t)sp, (int8_t)dc, (int8_t)dp, (int8_t)dlg, pm };
    to_move(&c, m);
    return 1;
}

static int dlg_of(const FcSolveMove *m)
{
    return m->movecol == FCS_MOVECOL_COLUMN ? DLG_COLUMN : m->movecol == FCS_MOVECOL_SINGLE ? DLG_SINGLE : DLG_NONE;
}

static void play_move(FcBoard *b, const FcSolveMove *m, int std)
{
    play(b, m->kind, m->src_col, m->src_pos, m->dst_col, m->dst_pos, dlg_of(m), std);
}

static int grow_path(FcSolver *sv, int n)
{
    if (n <= sv->path_cap) return 1;
    free_path(sv);
    sv->pm = malloc((size_t)n * sizeof *sv->pm);
    sv->path = malloc((size_t)n * sizeof *sv->path);
    if (!sv->pm || !sv->path) { free_path(sv); return 0; }
    sv->path_cap = n;
    return 1;
}

/* The solution: walk back from the goal (reached from node idx by move last), then replay the moves on
 * the real start board, resolving each to clicks. Returns the number of moves, -1 on failure.
 * (Parents are the first discoverers and nodes are never reopened, so no position on the path is one
 * move from a later one but the next: the path has no one-move shortcuts to take.) */
static int build_path(FcSolver *sv, const FcBoard *start, uint32_t idx, uint32_t last, int std)
{
    int n = 1;
    for (uint32_t i = idx; sv->nodes[i].parent != NIL; i = sv->nodes[i].parent) n++;
    if (!grow_path(sv, n)) return -1;
    int k = n - 1;
    sv->pm[k--] = last;
    for (uint32_t i = idx; sv->nodes[i].parent != NIL; i = sv->nodes[i].parent) sv->pm[k--] = sv->nodes[i].move;
    FcBoard b = *start;
    for (k = 0; k < n; k++) {
        if (!resolve(&b, sv->pm[k], &sv->path[k])) return -1;
        play_move(&b, &sv->path[k], std);
    }
    return b.cards_left == 0 ? n : -1;
}

int fc_solve(FcSolver *sv, const FcBoard *start, int std, const volatile int *cancel, FcSolveResult *res)
{
    std = std != 0;
    memset(res, 0, sizeof *res);
    res->status = FC_SOLVE_GAVE_UP;
    if (!sv || !start) return res->status;
    if (start->cards_left == 0) {
        res->status = FC_SOLVE_SOLVED;
        return res->status;
    }
    clear_table(sv);
    for (int i = 0; i < PQ_SIZE; i++) sv->bucket[i] = NIL;
    sv->pmin = PQ_SIZE;

    uint8_t st[ST_BYTES];
    int added;
    encode(start, st);
    uint32_t root = intern(sv, st, &added);
    if (root == NIL) return res->status;
    sv->nodes[root].parent = NIL;
    sv->nodes[root].move = 0;
    sv->nodes[root].g = 0;
    push(sv, root, heuristic(sv->w, st));
    int root_autoplay = needs_autoplay(start);

    Cand cand[FC_SOLVE_MAX_MOVES];
    FcBoard b, child;
    int status = FC_SOLVE_UNSOLVABLE;
    uint32_t idx;
    while ((idx = pop(sv)) != NIL) {
        if (cancel && *cancel) { status = FC_SOLVE_CANCELLED; break; }
        res->expanded++;
        decode(sv, sv->nodes[idx].st, &b);
        int g = sv->nodes[idx].g + 1, nc = generate(&b, std, idx == root && root_autoplay, cand);
        if (g > 0xFFFF) g = 0xFFFF;
        for (int i = 0; i < nc; i++) {
            const Cand *c = &cand[i];
            child = b;
            play(&child, c->kind, c->sc, c->sp, c->dc, c->dp, c->dlg, std);
            if (child.cards_left == 0) {
                int n = build_path(sv, start, idx, c->pm, std);
                res->nodes = sv->nnodes;
                if (n < 0) return res->status;           /* out of memory (or a bug): GAVE_UP */
                res->status = FC_SOLVE_SOLVED;
                res->nmoves = n;
                res->moves = sv->path;
                return res->status;
            }
            encode(&child, st);
            uint32_t k = intern(sv, st, &added);
            if (k == NIL) { status = FC_SOLVE_GAVE_UP; goto done; }
            if (!added) continue;
            Node *nd = &sv->nodes[k];
            nd->parent = idx;
            nd->move = c->pm;
            nd->g = (uint16_t)g;
            push(sv, k, g * sv->w[W_G] + heuristic(sv->w, st));
        }
    }
done:
    res->status = status;
    res->nodes = sv->nnodes;
    return status;
}

/* ---- Single moves ------------------------------------------------------------------------------- */

int fc_solve_moves(const FcBoard *b, int std, FcSolveMove *out)
{
    Cand cand[FC_SOLVE_MAX_MOVES];
    int n = generate(b, std != 0, needs_autoplay(b), cand);
    for (int i = 0; i < n; i++) to_move(&cand[i], &out[i]);
    return n;
}

int fc_solve_estimate(const FcBoard *b)
{
    uint8_t st[ST_BYTES];
    encode(b, st);
    return heuristic(default_w, st);
}

int fc_solve_play(FcBoard *b, const FcSolveMove *m, int std)
{
    int sc = m->src_col, sp = m->src_pos, dc = m->dst_col, dp = m->dst_pos;
    std = std != 0;
    if (sc == 0) {
        if (sp < 0 || sp > 3 || b->board[0][sp] == FC_EMPTY) return 0;
    } else if (sc < 1 || sc > 8 || fc_last_index(b, sc) < 0) {
        return 0;
    } else {
        sp = fc_last_index(b, sc);
    }
    Card x = b->board[sc][sp];
    int ok = 0;
    switch (m->kind) {
    case FC_SM_HOME:
        ok = dc == 0 && dp >= 4 && dp <= 7 &&
             (b->board[0][dp] == FC_EMPTY ? fc_rank(x) == 0
                                          : fc_suit(b->board[0][dp]) == fc_suit(x) && can_home(b, x));
        break;
    case FC_SM_FREECELL:
        ok = sc != 0 && dc == 0 && dp >= 0 && dp <= 3 && b->board[0][dp] == FC_EMPTY;
        break;
    case FC_SM_COLUMN:
        if (dc < 1 || dc > 8 || dc == sc || fc_last_index(b, dc) < 0) break;
        if (sc == 0) {
            ok = fc_can_stack(x, b->board[dc][fc_last_index(b, dc)]);
        } else {
            int k = fc_cards_to_move(b, sc, dc);
            ok = k >= 1 && k <= fc_max_movable_rule(b, std);
        }
        break;
    case FC_SM_EMPTY:
        if (dc < 1 || dc > 8 || b->board[dc][0] != FC_EMPTY) break;
        ok = sc == 0 ? m->movecol == FC_SM_NODIALOG
                     : (empty_column_offer(b, sc, dc, std) > 1) == (m->movecol != FC_SM_NODIALOG);
        break;
    case FC_SM_AUTOPLAY:
        ok = dc == sc && (sc != 0 || dp == sp);
        break;
    }
    if (!ok) return 0;
    play(b, m->kind, sc, sp, dc, dp, dlg_of(m), std);
    return 1;
}

int fc_sure_win(const FcBoard *start, FcSolveMove *moves, int *nmoves)
{
    FcBoard b = *start;
    int n = 0;
    while (b.cards_left > 0 && n < 52) {
        int sc = -1, sp = -1, best = 99;             /* the lowest card that can go home */
        for (int i = 0; i < 4; i++) {
            Card x = b.board[0][i];
            if (x != FC_EMPTY && can_home(&b, x) && fc_rank(x) < best) { best = fc_rank(x); sc = 0; sp = i; }
        }
        for (int c = 1; c <= 8; c++) {
            int l = fc_last_index(&b, c);
            if (l >= 0 && can_home(&b, b.board[c][l]) && fc_rank(b.board[c][l]) < best) {
                best = fc_rank(b.board[c][l]); sc = c; sp = l;
            }
        }
        if (sc < 0) break;
        Card x = b.board[sc][sp];
        FcSolveMove m = { (int8_t)sc, (int8_t)sp, 0, (int8_t)home_slot(&b, x), FC_SM_NODIALOG, FC_SM_HOME, 1, 0, x };
        if (moves) moves[n] = m;
        n++;
        play_move(&b, &m, 0);
    }
    if (nmoves) *nmoves = n;
    return b.cards_left == 0;
}
