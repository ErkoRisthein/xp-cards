/*
 * Solitaire HD — shared helpers of the native tests (tests/solitaire/test_sol_*.c): checks, card
 * notation ("9C", "#9C" = face down), board builders, a memory registry and a recording fake UI.
 */
#ifndef SOL_TEST_H
#define SOL_TEST_H

#include "solitaire/game.h"
#include "solitaire/session.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;

#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (a), _b = (b); checks++; if (_a != _b) { fails++; \
    printf("FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); checks++; if (strcmp(_a, _b)) { fails++; \
    printf("FAIL %s:%d: %s == \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

static inline int test_summary(const char *name)
{
    printf("%s: %d checks, %d failures\n", name, checks, fails);
    return fails ? 1 : 0;
}

/* The random tests' own generator (the C library's rand differs between macOS and glibc). */
static uint64_t trng = 1;
static inline void tsrand(unsigned seed) { trng = seed; }
static inline int trand(void)
{
    trng = trng * 6364136223846793005u + 1442695040888963407u;
    return (int)(trng >> 33);
}

/* ---- cards and boards ---------------------------------------------------------------------------- */

static inline SolCard C(const char *s)
{
    const char *ranks = "A23456789TJQK", *suits = "CDHS";
    int up = 1;
    if (*s == '#') { up = 0; s++; }
    int c = (int)(strchr(ranks, s[0]) - ranks) * 4 + (int)(strchr(suits, s[1]) - suits);
    return (SolCard)(up ? c | SOL_UP : c);
}

static inline const char *N(SolCard c)
{
    static char buf[8][4];
    static int k;
    char *b = buf[k++ & 7], *p = b;
    if (!sol_is_up(c)) *p++ = '#';
    *p++ = "A23456789TJQK"[sol_rank(c)];
    *p++ = "CDHS"[sol_suit(c)];
    *p = 0;
    return b;
}

/* Pile from "#9C 2H ..." (bottom first). */
static inline void set_pile(SolBoard *b, int pile, const char *cards)
{
    SolPile *p = &b->p[pile];
    p->n = 0;
    while (*cards) {
        while (*cards == ' ') cards++;
        if (!*cards) break;
        p->c[p->n++] = C(cards);
        cards += *cards == '#' ? 3 : 2;
    }
}

static inline void pile_str(const SolBoard *b, int pile, char *out, size_t n)
{
    size_t k = 0;
    out[0] = 0;
    for (int i = 0; i < b->p[pile].n && k + 5 < n; i++)
        k += (size_t)snprintf(out + k, n - k, "%s%s", i ? " " : "", N(b->p[pile].c[i]));
}

static inline int pile_is(const SolBoard *b, int pile, const char *cards)
{
    SolBoard t;
    memset(&t, 0, sizeof t);
    set_pile(&t, pile, cards);
    if (t.p[pile].n == b->p[pile].n && !memcmp(t.p[pile].c, b->p[pile].c, t.p[pile].n)) return 1;
    char got[256];
    pile_str(b, pile, got, sizeof got);
    printf("  pile %d = \"%s\", expected \"%s\"\n", pile, got, cards);
    return 0;
}

/* Put every card missing from the board face down at the BOTTOM of the stock (scenario boards keep
 * all 52 cards; the scenario's own stock cards stay on top). */
static inline void fill_stock(SolBoard *b)
{
    int seen[52] = { 0 };
    SolCard missing[52];
    int nm = 0;
    for (int p = 0; p < SOL_NPILES; p++)
        for (int i = 0; i < b->p[p].n; i++) seen[sol_card_id(b->p[p].c[i])] = 1;
    for (int c = 0; c < 52; c++) if (!seen[c]) missing[nm++] = (SolCard)c;
    SolPile *st = &b->p[SOL_STOCK];
    memmove(st->c + nm, st->c, st->n);
    memcpy(st->c, missing, (size_t)nm);
    st->n = (uint8_t)(st->n + nm);
}

/* ---- memory registry ------------------------------------------------------------------------------ */

typedef struct Reg {
    struct { char name[32]; uint32_t v; } val[16];
    int n, nset;
} Reg;

static inline int reg_find(Reg *r, const char *name)
{
    for (int i = 0; i < r->n; i++) if (!strcmp(r->val[i].name, name)) return i;
    return -1;
}
static inline int reg_get(void *ctx, const char *name, uint32_t *v)
{
    Reg *r = ctx;
    int i = reg_find(r, name);
    if (i < 0) return 0;
    *v = r->val[i].v;
    return 1;
}
static inline void reg_set(void *ctx, const char *name, uint32_t v)
{
    Reg *r = ctx;
    int i = reg_find(r, name);
    if (i < 0 && r->n < 16) { i = r->n++; snprintf(r->val[i].name, sizeof r->val[i].name, "%s", name); }
    if (i >= 0) r->val[i].v = v;
    r->nset++;
}
static inline void reg_del(void *ctx, const char *name)
{
    Reg *r = ctx;
    int i = reg_find(r, name);
    if (i >= 0) r->val[i] = r->val[--r->n];
}
static inline CeStore reg_store(Reg *r)
{
    CeStore s = { r, reg_get, reg_set, reg_del, NULL, NULL };
    return s;
}

/* ---- fake UI -------------------------------------------------------------------------------------- */

typedef struct Fake {
    SolSession *s;
    char log[4096];
    int ninval, nstatus, ncascade, ndealagain;
    int deal_again;          /* answer to "Deal Again?" */
    int post;                /* 1: post_command records instead of running */
    int posted;              /* last posted command */
    uint32_t now;            /* time(NULL) */
    int timer_ms;            /* current timer state for SOL_TIMER_CLOCK */
    int ntimer_calls;
    int kpile, kcard, kdrag, nkbd;
    int cascade_dealt, cascade_visible, cascade_score, cascade_forced;
    int cascade_hist;        /* history entries during the cascade */
    char cascade_text[128];
    /* extras */
    int hint_timer_ms;       /* current timer state for SOL_TIMER_HINT */
    int nmsg, last_msg;      /* ui.message */
    char last_msg_text[128];
    int nanim;               /* ui.animate_move */
    int anim[60][2];         /* src, dst */
    int nstats;              /* ui.stats_changed */
    int nsolve, ncancel;     /* ui.solve_start / solve_cancel */
    uint32_t solve_id;
    SolBoard solve_board;
    int solve_draw, solve_left;
} Fake;

static inline void flog(Fake *f, const char *fmt, ...)
{
    size_t k = strlen(f->log);
    va_list ap;
    if (k + 64 >= sizeof f->log) return;
    va_start(ap, fmt);
    vsnprintf(f->log + k, sizeof f->log - k, fmt, ap);
    va_end(ap);
}

static inline void f_invalidate(void *ctx) { ((Fake *)ctx)->ninval++; }
static inline void f_status(void *ctx) { ((Fake *)ctx)->nstatus++; }
static inline void f_set_timer(void *ctx, int id, int ms)
{
    Fake *f = ctx;
    if (id == SOL_TIMER_CLOCK) f->timer_ms = ms;
    if (id == SOL_TIMER_HINT) f->hint_timer_ms = ms;
    f->ntimer_calls++;
}
static inline void f_cascade(void *ctx)
{
    Fake *f = ctx;
    f->ncascade++;
    f->cascade_dealt = f->s->dealt;
    f->cascade_visible = f->s->visible;
    f->cascade_score = f->s->score;
    f->cascade_forced = f->s->forced_win;
    f->cascade_hist = f->s->nhist;
    sol_win_text(f->s, f->cascade_text, sizeof f->cascade_text);
    flog(f, "cascade;");
}
static inline int f_deal_again(void *ctx)
{
    Fake *f = ctx;
    f->ndealagain++;
    flog(f, "dealagain;");
    return f->deal_again;
}
static inline void f_post(void *ctx, int cmd)
{
    Fake *f = ctx;
    f->posted = cmd;
    flog(f, "post(%d);", cmd);
}
static inline uint32_t f_now(void *ctx) { return ((Fake *)ctx)->now; }
static inline void f_kbd(void *ctx, int pile, int card, int dragging)
{
    Fake *f = ctx;
    f->kpile = pile;
    f->kcard = card;
    f->kdrag = dragging;
    f->nkbd++;
}

static inline void f_message(void *ctx, int id, const char *text)
{
    Fake *f = ctx;
    f->nmsg++;
    f->last_msg = id;
    snprintf(f->last_msg_text, sizeof f->last_msg_text, "%s", text);
    flog(f, "msg(%d);", id);
}
static inline void f_animate(void *ctx, int src, int dst)
{
    Fake *f = ctx;
    if (f->nanim < 60) { f->anim[f->nanim][0] = src; f->anim[f->nanim][1] = dst; }
    f->nanim++;
}
static inline void f_stats(void *ctx) { ((Fake *)ctx)->nstats++; }
static inline void f_solve(void *ctx, uint32_t id, const SolBoard *b, int draw, int left)
{
    Fake *f = ctx;
    f->nsolve++;
    f->solve_id = id;
    f->solve_board = *b;
    f->solve_draw = draw;
    f->solve_left = left;
}
static inline void f_cancel(void *ctx) { ((Fake *)ctx)->ncancel++; }

static inline SolSessionUI fake_ui(Fake *f, int with_post)
{
    SolSessionUI ui;
    memset(&ui, 0, sizeof ui);
    ui.ctx = f;
    ui.invalidate = f_invalidate;
    ui.status_changed = f_status;
    ui.set_timer = f_set_timer;
    ui.win_cascade = f_cascade;
    ui.deal_again = f_deal_again;
    ui.post_command = with_post ? f_post : NULL;
    ui.now_seed = f_now;
    ui.kbd_cursor = f_kbd;
    ui.message = f_message;
    ui.animate_move = f_animate;
    ui.stats_changed = f_stats;
    ui.solve_start = f_solve;
    ui.solve_cancel = f_cancel;
    return ui;
}

/* A session with a memory registry (r) and a fake UI, Options = opts (0 = absent), dealt `seed`. */
static inline void start(SolSession *s, Fake *f, Reg *r, uint32_t opts, int seed)
{
    memset(f, 0, sizeof *f);
    memset(r, 0, sizeof *r);
    f->s = s;
    f->now = 1234567;
    if (opts) reg_set(r, "Options", opts);
    r->nset = 0;
    SolSessionUI ui = fake_ui(f, 0);
    CeStore st = reg_store(r);
    sol_init(s, &ui, &st);
    if (seed >= 0) sol_deal(s, (unsigned)seed, 0);
}

static inline int total_found(const SolSession *s)
{
    int n = 0;
    for (int f = 0; f < 4; f++) n += s->board.p[SOL_FOUND0 + f].n;
    return n;
}

#endif
