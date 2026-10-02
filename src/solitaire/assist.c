/*
 * Solitaire HD — fair hint, click-to-move destination and Finish (see assist.h).
 *
 * Only visible cards are read: sol_can_drop is called with face-up source cards only (it looks at a
 * destination's top card only after checking that it is face up), face-down cards are counted, never
 * looked at, and the stock is only tested for being empty.
 */
#include "assist.h"

#include <string.h>

/* The leftmost foundation that takes the face-up top card of pile (XP's double-click), or -1. */
static int home_dest(const SolBoard *b, int pile)
{
    int n = b->p[pile].n;
    if (n == 0 || !sol_is_up(b->p[pile].c[n - 1]))
        return -1;
    for (int f = SOL_FOUND0; f < SOL_FOUND0 + 4; f++)
        if (sol_can_drop(b, f, pile, n - 1))
            return f;
    return -1;
}

/* Could face-up card c go to some foundation of b? */
static int fits_home(const SolBoard *b, SolCard c)
{
    for (int f = SOL_FOUND0; f < SOL_FOUND0 + 4; f++) {
        const SolPile *p = &b->p[f];
        if (p->n == 0) {
            if (sol_rank(c) == 0)
                return 1;
        } else if (sol_suit(p->c[p->n - 1]) == sol_suit(c) && sol_rank(p->c[p->n - 1]) + 1 == sol_rank(c)) {
            return 1;
        }
    }
    return 0;
}

/* The top card of pile is face up and could go to some foundation. */
static int top_fits_home(const SolBoard *b, int pile)
{
    const SolPile *p = &b->p[pile];
    return p->n > 0 && sol_is_up(p->c[p->n - 1]) && fits_home(b, p->c[p->n - 1]);
}

/* Cards home per suit. */
static void home_counts(const SolBoard *b, int home[4])
{
    memset(home, 0, 4 * sizeof home[0]);
    for (int f = SOL_FOUND0; f < SOL_FOUND0 + 4; f++)
        if (b->p[f].n)
            home[sol_suit(b->p[f].c[0])] = b->p[f].n;
}

/* Index of the lowest face-up card of a tableau column (= its number of face-down cards); n if none. */
static int first_up(const SolBoard *b, int pile)
{
    return b->p[pile].n - sol_up_run(b, pile);
}

/* A king the player can see waiting for an empty column: the waste's top card, or a king heading a run
 * that lies on face-down cards. */
static int king_waiting(const SolBoard *b)
{
    const SolPile *w = &b->p[SOL_WASTE];
    if (w->n && sol_rank(w->c[w->n - 1]) == SOL_KING)
        return 1;
    for (int t = SOL_TAB0; t < SOL_NPILES; t++) {
        int i = first_up(b, t);
        if (i > 0 && i < b->p[t].n && sol_rank(b->p[t].c[i]) == SOL_KING)
            return 1;
    }
    return 0;
}

typedef struct Best {
    SolHintMove m;
    int tie;            /* larger first: face-down cards under the move's source */
    int have;
} Best;

static void offer(Best *best, int kind, int src, int index, int dst, int cls, int tie)
{
    if (best->have && (cls > best->m.cls || (cls == best->m.cls && tie <= best->tie)))
        return;
    best->have = 1;
    best->m.kind = kind;
    best->m.src = src;
    best->m.index = index;
    best->m.dst = dst;
    best->m.cls = cls;
    best->tie = tie;
}

int sol_hint_find(const SolBoard *b, int recycle_ok, SolHintMove *out)
{
    Best best;
    int home[4], t, i, d;
    memset(&best, 0, sizeof best);
    home_counts(b, home);

    /* 1. face-down top cards */
    for (t = SOL_TAB0; t < SOL_NPILES; t++) {
        const SolPile *p = &b->p[t];
        if (p->n && !sol_is_up(p->c[p->n - 1]))
            offer(&best, SOL_HINT_TURN, t, p->n - 1, t, SOL_HC_TURN, p->n);
    }

    /* 2-4, 6, 10. cards to the foundations: the waste's, then the columns' tops */
    for (t = SOL_WASTE; t < SOL_NPILES; t++) {
        const SolPile *p = &b->p[t];
        int f, r, cls;
        SolCard c;
        if (sol_is_found(t) || (f = home_dest(b, t)) < 0)
            continue;
        c = p->c[p->n - 1];
        r = sol_rank(c);
        if (r <= 1) {
            cls = SOL_HC_LOW_HOME;
        } else {
            int red = sol_suit(c) == 1 || sol_suit(c) == 2;
            int o1 = red ? 0 : 1, o2 = red ? 3 : 2;              /* the other colour's suits */
            if (home[o1] >= r && home[o2] >= r)
                cls = SOL_HC_SAFE_HOME;
            else if (t == SOL_WASTE)
                cls = SOL_HC_WASTE_HOME;
            else if (p->n >= 2 && !sol_is_up(p->c[p->n - 2]))
                cls = SOL_HC_HOME_REVEAL;
            else
                cls = SOL_HC_HOME;
        }
        offer(&best, SOL_HINT_MOVE, t, p->n - 1, f, cls, sol_is_tab(t) ? first_up(b, t) : 0);
    }

    /* 5, 7, 9. tableau runs onto other columns */
    {
        int kw = king_waiting(b);
        for (t = SOL_TAB0; t < SOL_NPILES; t++) {
            const SolPile *p = &b->p[t];
            int lo = first_up(b, t);
            for (i = lo; i < p->n; i++) {
                int cls = 0;
                if (i > 0 && !sol_is_up(p->c[i - 1]))
                    cls = SOL_HC_REVEAL;                          /* uncovers a face-down card */
                else if (i > 0 && fits_home(b, p->c[i - 1]))
                    cls = SOL_HC_FREE_HOME;                       /* frees the card under it for home */
                else if (i == 0 && sol_rank(p->c[0]) != SOL_KING && kw)
                    cls = SOL_HC_EMPTY_COL;                       /* empties the column for a king */
                if (!cls)
                    continue;
                for (d = SOL_TAB0; d < SOL_NPILES; d++) {
                    if (d == t || !sol_can_drop(b, d, t, i))
                        continue;
                    /* freeing a card for home by covering another that could go home is no progress
                     * (and the next hint would move the run back) */
                    if (cls == SOL_HC_FREE_HOME && top_fits_home(b, d))
                        continue;
                    offer(&best, SOL_HINT_MOVE, t, i, d, cls, cls == SOL_HC_REVEAL ? i : 0);
                    break;                                        /* the leftmost destination */
                }
            }
        }
    }

    /* 8. the waste's card to the tableau */
    {
        const SolPile *w = &b->p[SOL_WASTE];
        if (w->n && sol_is_up(w->c[w->n - 1]))
            for (d = SOL_TAB0; d < SOL_NPILES; d++)
                if (sol_can_drop(b, d, SOL_WASTE, w->n - 1)) {
                    offer(&best, SOL_HINT_MOVE, SOL_WASTE, w->n - 1, d, SOL_HC_WASTE_TAB, 0);
                    break;
                }
    }

    /* 11, 12. the stock */
    if (b->p[SOL_STOCK].n)
        offer(&best, SOL_HINT_DRAW, SOL_STOCK, b->p[SOL_STOCK].n - 1, SOL_WASTE, SOL_HC_DRAW, 0);
    else if (b->p[SOL_WASTE].n && recycle_ok)
        offer(&best, SOL_HINT_RECYCLE, SOL_STOCK, -1, SOL_WASTE, SOL_HC_RECYCLE, 0);

    if (!best.have) {
        memset(out, 0, sizeof *out);
        out->kind = SOL_HINT_NONE;
        out->src = out->dst = out->index = -1;
        return 0;
    }
    *out = best.m;
    return 1;
}

int sol_click_dest(const SolBoard *b, int pile, int index)
{
    const SolPile *p;
    int d, first = -1;
    if (pile != SOL_WASTE && !sol_is_tab(pile))
        return -1;                                  /* the stock is pressed; a foundation's card stays */
    p = &b->p[pile];
    if (index < 0 || index >= p->n || !sol_is_up(p->c[index]))
        return -1;
    if (pile == SOL_WASTE && index != p->n - 1)
        return -1;
    if (index == p->n - 1)
        for (d = SOL_FOUND0; d < SOL_FOUND0 + 4; d++)
            if (sol_can_drop(b, d, pile, index))
                return d;
    if (sol_rank(p->c[index]) == SOL_KING && sol_is_tab(pile) && index == 0)
        return -1;                                  /* a king already heading its column stays */
    for (d = SOL_TAB0; d < SOL_NPILES; d++)
        if (d != pile && sol_can_drop(b, d, pile, index)) {
            if (!top_fits_home(b, d))
                return d;                           /* the leftmost that buries no card bound for home */
            if (first < 0)
                first = d;
        }
    return first;
}

int sol_auto_home_step(const SolBoard *b, int *src, int *dst)
{
    int home[4];
    home_counts(b, home);
    for (int t = SOL_WASTE; t < SOL_NPILES; t++) {
        int f, r, red;
        SolCard c;
        if (sol_is_found(t) || (f = home_dest(b, t)) < 0)
            continue;
        c = b->p[t].c[b->p[t].n - 1];
        r = sol_rank(c);
        red = sol_suit(c) == 1 || sol_suit(c) == 2;
        /* home[] counts the cards: >= r means the other colour's rank - 1 is there */
        if (r <= 1 || (home[red ? 0 : 1] >= r && home[red ? 3 : 2] >= r)) {
            *src = t;
            *dst = f;
            return 1;
        }
    }
    return 0;
}

int sol_finish_ready(const SolBoard *b)
{
    int left = 0;
    if (b->p[SOL_STOCK].n || b->p[SOL_WASTE].n)
        return 0;
    for (int t = SOL_TAB0; t < SOL_NPILES; t++) {
        if (sol_up_run(b, t) != b->p[t].n)
            return 0;
        left += b->p[t].n;
    }
    return left > 0;
}

int sol_finish_step(const SolBoard *b, int *src, int *dst)
{
    int best = -1, best_rank = 99, best_dst = -1;
    for (int t = SOL_TAB0; t < SOL_NPILES; t++) {
        int f = home_dest(b, t);
        if (f >= 0 && sol_rank(b->p[t].c[b->p[t].n - 1]) < best_rank) {
            best = t;
            best_rank = sol_rank(b->p[t].c[b->p[t].n - 1]);
            best_dst = f;
        }
    }
    if (best < 0)
        return 0;
    *src = best;
    *dst = best_dst;
    return 1;
}
