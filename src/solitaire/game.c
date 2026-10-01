/*
 * Solitaire HD — XP Solitaire rules primitives (see game.h).
 */
#include "game.h"

#include <stdio.h>
#include <string.h>

const int sol_std_table[8]   = { -2, -20, 10, 5, 5, -15, 0, 0 };    /* sol.exe 0x1007034 */
const int sol_vegas_table[8] = { 0, 0, 5, 0, 0, -5, -52, 0 };       /* sol.exe 0x1007054 */

void sol_board_clear(SolBoard *b)
{
    memset(b, 0, sizeof *b);
}

void sol_deal_board(SolBoard *b, unsigned seed, uint32_t *rng_after)
{
    uint32_t x = seed;                  /* srand(seed) */
    SolPile *stock = &b->p[SOL_STOCK];
    sol_board_clear(b);
    /* StockInit 0x10045CB + Shuffle 0x1003DFE: five passes, rand() before each swap. */
    for (int i = 0; i < 52; i++) stock->c[i] = (SolCard)i;
    stock->n = 52;
    for (int pass = 0; pass < 5; pass++)
        for (int i = 0; i < 52; i++) {
            int j = sol_rand(&x) % 52;
            SolCard t = stock->c[i];
            stock->c[i] = stock->c[j];
            stock->c[j] = t;
        }
    /* KlondDeal 0x1004A7C: row by row, the top stock card; the first card of each row face up. */
    for (int row = 0; row < 7; row++)
        for (int t = row; t < 7; t++) {
            SolPile *col = &b->p[SOL_TAB0 + t];
            SolCard c = stock->c[--stock->n];
            col->c[col->n++] = (SolCard)(t == row ? c | SOL_UP : c);
        }
    if (rng_after) *rng_after = x;
}

int sol_can_drop(const SolBoard *b, int dst, int src, int idx)
{
    if (dst < 0 || dst >= SOL_NPILES || src < 0 || src >= SOL_NPILES || dst == src) return 0;
    const SolPile *s = &b->p[src], *d = &b->p[dst];
    if (idx < 0 || idx >= s->n) return 0;
    SolCard cd = s->c[idx];
    if (sol_is_tab(dst)) {                              /* TableauCanDrop 0x1004279 */
        if (sol_rank(cd) == SOL_KING) return d->n == 0;
        if (d->n == 0) return 0;
        SolCard top = d->c[d->n - 1];
        if (!sol_is_up(top) || !sol_opposite(top, cd)) return 0;
        return sol_rank(cd) + 1 == sol_rank(top);
    }
    if (sol_is_found(dst)) {                            /* FoundationCanDrop 0x1004531 */
        if (idx != s->n - 1) return 0;                  /* single cards only */
        if (d->n == 0) return sol_rank(cd) == 0;
        SolCard top = d->c[d->n - 1];
        return sol_rank(top) + 1 == sol_rank(cd) && sol_suit(top) == sol_suit(cd);
    }
    return 0;                                           /* stock, waste */
}

int sol_up_run(const SolBoard *b, int pile)
{
    const SolPile *p = &b->p[pile];
    int i = p->n - 1;
    while (i >= 0 && sol_is_up(p->c[i])) i--;
    return p->n - 1 - i;
}

int sol_is_won(const SolBoard *b)
{
    for (int f = 0; f < 4; f++)
        if (b->p[SOL_FOUND0 + f].n != 13) return 0;
    return 1;
}

int sol_board_equal(const SolBoard *a, const SolBoard *b)
{
    for (int p = 0; p < SOL_NPILES; p++) {
        if (a->p[p].n != b->p[p].n) return 0;
        if (memcmp(a->p[p].c, b->p[p].c, a->p[p].n)) return 0;
    }
    return 1;
}

#define FAIL(...) do { if (why && n) snprintf(why, n, __VA_ARGS__); return 0; } while (0)

int sol_board_valid(const SolBoard *b, char *why, size_t n)
{
    int seen[52] = { 0 }, total = 0;
    for (int p = 0; p < SOL_NPILES; p++) {
        const SolPile *q = &b->p[p];
        if (q->n > 52) FAIL("pile %d holds %d cards", p, q->n);
        for (int i = 0; i < q->n; i++) {
            SolCard c = q->c[i];
            if ((c & ~(SOL_UP | 0x3F)) || sol_card_id(c) >= 52) FAIL("pile %d card %d = 0x%02X", p, i, c);
            if (seen[sol_card_id(c)]++) FAIL("card %d twice", sol_card_id(c));
            total++;
            if (p == SOL_STOCK && sol_is_up(c)) FAIL("stock card %d face up", i);
            if (p == SOL_WASTE && !sol_is_up(c)) FAIL("waste card %d face down", i);
            if (sol_is_found(p)) {
                if (!sol_is_up(c)) FAIL("foundation %d card %d face down", p, i);
                if (sol_rank(c) != i) FAIL("foundation %d card %d has rank %d", p, i, sol_rank(c));
                if (i && sol_suit(c) != sol_suit(q->c[0])) FAIL("foundation %d mixes suits", p);
            }
            if (sol_is_tab(p) && i > 0) {
                SolCard below = q->c[i - 1];
                if (!sol_is_up(c) && sol_is_up(below)) FAIL("tableau %d: face-down card %d on a face-up one", p, i);
                if (sol_is_up(c) && sol_is_up(below) &&
                    (sol_rank(c) + 1 != sol_rank(below) || !sol_opposite(c, below)))
                    FAIL("tableau %d: card %d breaks the run", p, i);
            }
        }
    }
    if (total != 52) FAIL("%d cards on the board", total);
    return 1;
}

void sol_board_pack(const SolBoard *b, uint8_t out[SOL_PACKED_SIZE])
{
    int k = SOL_NPILES;
    for (int p = 0; p < SOL_NPILES; p++) {
        out[p] = b->p[p].n;
        for (int i = 0; i < b->p[p].n && k < SOL_PACKED_SIZE; i++) out[k++] = b->p[p].c[i];
    }
    while (k < SOL_PACKED_SIZE) out[k++] = 0;
}

void sol_board_unpack(SolBoard *b, const uint8_t in[SOL_PACKED_SIZE])
{
    int k = SOL_NPILES;
    sol_board_clear(b);
    for (int p = 0; p < SOL_NPILES; p++) {
        int cnt = in[p] <= 52 ? in[p] : 52;
        b->p[p].n = (uint8_t)cnt;
        for (int i = 0; i < cnt && k < SOL_PACKED_SIZE; i++) b->p[p].c[i] = in[k++];
    }
}

/* ---- Scoring ------------------------------------------------------------------------------------- */

int sol_move_event(int dst, int src)
{
    int dc = sol_pile_class(dst), sc = sol_pile_class(src);
    if (dc == SOL_CLASS_STOCK && sc == SOL_CLASS_WASTE) return SOL_EV_RECYCLE;
    if (dc == SOL_CLASS_FOUND && (sc == SOL_CLASS_WASTE || sc == SOL_CLASS_TAB)) return SOL_EV_TO_FOUND;
    if (dc == SOL_CLASS_TAB && sc == SOL_CLASS_WASTE) return SOL_EV_WASTE_TO_TAB;
    if (dc == SOL_CLASS_TAB && sc == SOL_CLASS_FOUND) return SOL_EV_FOUND_TO_TAB;
    return -1;
}

int sol_time_bonus(int ticks)
{
    return ticks >= 120 ? (20000 / (ticks >> 2)) * 35 : 0;
}

int sol_change_score(int *score, int ev, int scoring, int timed, int draw, int ticks, int recycles)
{
    int d;
    if (ev < 0 || ev > SOL_EV_WIN || scoring == SOL_SCORING_NONE) return 0;
    if (scoring == SOL_SCORING_VEGAS) {
        d = sol_vegas_table[ev];
        *score += d;
        return ev == SOL_EV_WIN ? 0 : d;
    }
    /* Standard */
    if (ev == SOL_EV_WIN) {
        d = timed ? sol_time_bonus(ticks) : sol_std_table[SOL_EV_WIN];
    } else if (ev == SOL_EV_RECYCLE) {
        if (draw == 1) {
            if (recycles < 1) return 0;
            d = -100;
        } else if (draw == 3) {
            if (recycles <= 3) return 0;
            d = sol_std_table[SOL_EV_RECYCLE];
        } else {
            return 0;
        }
    } else {
        d = sol_std_table[ev];
    }
    int before = *score;
    *score = *score + d < 0 ? 0 : *score + d;
    return ev == SOL_EV_WIN ? d : *score - before;
}

/* ---- Options ------------------------------------------------------------------------------------- */

void sol_options_unpack(SolOptions *o, uint32_t v)
{
    o->status_bar = (v & 0x01) != 0;
    o->timed = (v & 0x02) != 0;
    o->outline = (v & 0x04) != 0;
    o->draw = (v & 0x08) ? 3 : 1;
    switch ((v >> 4) & 3) {
    case 1:  o->scoring = SOL_SCORING_VEGAS; break;
    case 2:  o->scoring = SOL_SCORING_NONE; break;
    default: o->scoring = SOL_SCORING_STANDARD; break;
    }
    o->cumulative = (v & 0x40) != 0;
}

uint32_t sol_options_pack(const SolOptions *o)
{
    uint32_t v = 0;
    if (o->status_bar) v |= 0x01;
    if (o->timed) v |= 0x02;
    if (o->outline) v |= 0x04;
    if (o->draw != 1) v |= 0x08;
    if (o->scoring == SOL_SCORING_VEGAS) v |= 0x10;
    else if (o->scoring == SOL_SCORING_NONE) v |= 0x20;
    if (o->cumulative) v |= 0x40;
    return v;
}

int sol_back_from_reg(uint32_t v)
{
    long long id = (long long)(int32_t)v + 53;          /* XP: an int, clamped to 54..65 */
    if (id < 54) id = 54;
    if (id > 65) id = 65;
    return (int)(id - 54);
}

uint32_t sol_back_to_reg(int back)
{
    if (back < 0) back = 0;
    if (back >= SOL_NBACKS) back = SOL_NBACKS - 1;
    return (uint32_t)back + 1;
}

void sol_format_score(int score, int vegas, int icurrency, char *buf, size_t n)
{
    /* %u, not %lld: XP's msvcrt printf does not know "ll" */
    const char *minus = score < 0 ? "-" : "";
    unsigned a = score < 0 ? 0u - (unsigned)score : (unsigned)score;
    if (!n) return;
    if (!vegas) { snprintf(buf, n, "%s%u", minus, a); return; }
    switch (icurrency) {
    case 1:  snprintf(buf, n, "%s%u$", minus, a); break;
    case 2:  snprintf(buf, n, "%s$ %u", minus, a); break;
    case 3:  snprintf(buf, n, "%s%u $", minus, a); break;
    default: snprintf(buf, n, "%s$%u", minus, a); break;
    }
}
