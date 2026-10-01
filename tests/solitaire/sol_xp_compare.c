/*
 * Solitaire HD — proof that the extras change nothing while they are off: random play through the
 * session API of v1.0 (git 5dc1896, Solitaire HD + shared card engine), logging every UI callback,
 * every registry access and the whole observable state after every input. `make sol-xp-compare` builds
 * this file twice, against v1.0's src/solitaire (game.c, session.c, extracted with git show) and against
 * today's, runs both and compares the two logs byte for byte.
 *
 * Today's build (-DSOL_NEW_API) also attaches the statistics (as the Win32 front end always does) and
 * logs the new callbacks (message, animate_move, solve_start / solve_cancel): v1.0 has none, so a
 * single call of any of them with the extras off is a difference. The statistics are passive (written
 * through stats_changed, shown only in their dialog): those calls are counted apart. Inputs: presses and drags
 * (random and legal, mouse and keyboard), double-clicks, autoplay, the stock with and without the cheat,
 * keys with modifiers, clock ticks, minimizing, Deal, Undo, Redo, the forced win, the Options and Deck
 * dialogs' results, "Deal Again?" answered both ways, with and without post_command.
 *
 *   sol_xp_compare GAMES > log     (default 400 games)
 */
#include "solitaire/game.h"
#include "solitaire/session.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *out;

static void L(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
}

static uint64_t rng_state = 1;
static int R(int n)
{
    rng_state = rng_state * 6364136223846793005u + 1442695040888963407u;
    return (int)((rng_state >> 33) % (uint64_t)n);
}

/* ---- state digest ------------------------------------------------------------------------------- */

static SolSession S;

static uint32_t board_hash(const SolBoard *b)
{
    uint8_t p[SOL_PACKED_SIZE];
    uint32_t h = 2166136261u;
    sol_board_pack(b, p);
    for (int i = 0; i < SOL_PACKED_SIZE; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static void state(const char *tag)
{
    const SolSession *s = &S;
    char sc[32], wt[96];
    int kp, kc;
    sol_score_text(s, sc, sizeof sc);
    sol_win_text(s, wt, sizeof wt);
    sol_kbd_cursor(s, &kp, &kc);
    L("%s b%08x fan%d d%d v%d in%d w%d fw%d bo%d sc%d t%d rc%d pen%d uf%d dr%d seed%u rng%08x min%d tm%d busy%d "
      "drag%d.%d.%d k%d tg%d tu%d kc%d.%d h%d r%d opt%02x back%d cur%d | %d%d%d%d sym%d sec%d run%d wf%d wfs%d "
      "kbd%d.%d '%s' '%s' kdt%d\n",
      tag, board_hash(&s->board), s->waste_fan, s->dealt, s->visible, s->input, s->won, s->forced_win, s->bonus,
      s->score, s->ticks, s->recycles, s->clock_pen, s->undo_fresh, s->draw, s->seed, (unsigned)s->rng,
      s->minimized, s->timer_on, s->busy, s->drag_pile, s->drag_index, s->drag_count, s->drag_kbd, s->target,
      s->target_ui, s->kbd_pile, s->kbd_card, s->nhist, s->nredo, (unsigned)sol_options_pack(&s->opts), s->back,
      s->currency, sol_idle(s), sol_undo_enabled(s), sol_redo_enabled(s), sol_board_visible(s),
      sol_stock_symbol(s), sol_seconds(s), sol_clock_running(s), sol_waste_fan(s), sol_waste_fan_start(s), kp, kc,
      sc, wt, sol_key_drop_target(s));
}

/* ---- the UI and the registry, logged -------------------------------------------------------------- */

static int deal_again_answer, use_post;
static uint32_t now_value;

static void c_invalidate(void *ctx) { L("  invalidate b%08x\n", board_hash(&S.board)); }
static void c_status(void *ctx) { L("  status sc%d t%d\n", S.score, S.ticks); }
static void c_timer(void *ctx, int id, int ms) { L("  timer %d %d\n", id, ms); }
static void c_cascade(void *ctx) { state("  cascade"); }
static int  c_deal_again(void *ctx) { L("  dealagain -> %d\n", deal_again_answer); return deal_again_answer; }
static void c_post(void *ctx, int cmd) { L("  post %d\n", cmd); }
static uint32_t c_now(void *ctx) { L("  now %u\n", (unsigned)now_value); return now_value; }
static void c_kbd(void *ctx, int p, int c, int d) { L("  kbd %d %d %d\n", p, c, d); }
#ifdef SOL_NEW_API
static void c_message(void *ctx, int id, const char *t) { L("  NEW message %d\n", id); }
static void c_animate(void *ctx, int src, int dst) { L("  NEW animate %d %d\n", src, dst); }
/* the statistics are passive (recorded, shown only in their dialog): counted apart, not in the log */
static long nstats_changed;
static void c_stats(void *ctx) { nstats_changed++; }
static void c_solve(void *ctx, uint32_t id, const SolBoard *b, int d, int r) { L("  NEW solve %u\n", (unsigned)id); }
static void c_cancel(void *ctx) { L("  NEW solve_cancel\n"); }
#endif

static struct { char name[32]; uint32_t v; int set; } reg[16];

static int r_get(void *ctx, const char *name, uint32_t *v)
{
    for (int i = 0; i < 16; i++)
        if (reg[i].set && !strcmp(reg[i].name, name)) {
            *v = reg[i].v;
            L("  reg get %s = %u\n", name, (unsigned)*v);
            return 1;
        }
    L("  reg get %s absent\n", name);
    return 0;
}
static void r_set(void *ctx, const char *name, uint32_t v)
{
    int i, f = -1;
    L("  reg set %s %u\n", name, (unsigned)v);
    for (i = 0; i < 16; i++) {
        if (reg[i].set && !strcmp(reg[i].name, name)) { reg[i].v = v; return; }
        if (!reg[i].set && f < 0) f = i;
    }
    if (f >= 0) {
        snprintf(reg[f].name, sizeof reg[f].name, "%s", name);
        reg[f].v = v;
        reg[f].set = 1;
    }
}
static void r_del(void *ctx, const char *name) { L("  reg del %s\n", name); }
static void r_flush(void *ctx) { L("  reg flush\n"); }

static void init_session(uint32_t opts, int with_back)
{
    SolSessionUI ui;
    CeStore st;
    memset(&ui, 0, sizeof ui);
    memset(&st, 0, sizeof st);
    memset(reg, 0, sizeof reg);
    if (opts) {
        snprintf(reg[0].name, sizeof reg[0].name, "Options");
        reg[0].v = opts;
        reg[0].set = 1;
    }
    if (with_back) {
        snprintf(reg[1].name, sizeof reg[1].name, "Back");
        reg[1].v = (uint32_t)with_back;
        reg[1].set = 1;
    }
    ui.ctx = NULL;
    ui.invalidate = c_invalidate;
    ui.status_changed = c_status;
    ui.set_timer = c_timer;
    ui.win_cascade = c_cascade;
    ui.deal_again = c_deal_again;
    ui.post_command = use_post ? c_post : NULL;
    ui.now_seed = c_now;
    ui.kbd_cursor = c_kbd;
#ifdef SOL_NEW_API
    ui.message = c_message;
    ui.animate_move = c_animate;
    ui.stats_changed = c_stats;
    ui.solve_start = c_solve;
    ui.solve_cancel = c_cancel;
#endif
    st.get = r_get;
    st.set = r_set;
    st.del = r_del;
    st.flush = r_flush;
    sol_init(&S, &ui, &st);
#ifdef SOL_NEW_API
    sol_attach_stats(&S, NULL);        /* as the Win32 front end does: counted, never shown here */
#endif
}

/* ---- inputs ------------------------------------------------------------------------------------- */

static int legal_drag(int *src, int *idx, int *dst)
{
    int ms[700][3], n = 0;
    for (int p = SOL_WASTE; p < SOL_NPILES; p++) {
        const SolPile *q = &S.board.p[p];
        for (int i = 0; i < q->n; i++) {
            if (!sol_is_up(q->c[i])) continue;
            for (int d = 0; d < SOL_NPILES && n < 700; d++)
                if (sol_can_drop(&S.board, d, p, i)) { ms[n][0] = p; ms[n][1] = i; ms[n][2] = d; n++; }
        }
    }
    if (!n) return 0;
    n = R(n);
    *src = ms[n][0];
    *idx = ms[n][1];
    *dst = ms[n][2];
    return 1;
}

static const int keys[] = { SOL_KEY_TAB, SOL_KEY_RETURN, SOL_KEY_ESCAPE, SOL_KEY_SPACE, SOL_KEY_END, SOL_KEY_HOME,
                            SOL_KEY_LEFT, SOL_KEY_UP, SOL_KEY_RIGHT, SOL_KEY_DOWN, SOL_KEY_A, 'H', 0x71, 0x75 };

static void step(int k)
{
    int what = R(100), mods = R(8) == 0 ? R(8) : 0, r;
    if (what < 28) {
        int src, idx, dst;
        if (!sol_dragging(&S) && legal_drag(&src, &idx, &dst)) {
            if (R(4)) {
                r = sol_press(&S, src, idx, mods);
                L("press %d %d %d -> %d\n", src, idx, mods, r);
                if (r == SOL_PRESS_DRAG) {
                    int t = R(5) ? dst : R(14) - 1;
                    L("over %d -> %d\n", t, sol_drag_over(&S, t));
                    if (R(10)) L("drop %d -> %d\n", S.target, sol_drop(&S, S.target));
                    else { L("cancel\n"); sol_cancel_drag(&S); }
                }
            } else {
                S.kbd_pile = src;
                S.kbd_card = idx;
                L("key ret -> %d\n", sol_key(&S, SOL_KEY_RETURN, 0));
                if (sol_dragging(&S)) {
                    S.kbd_pile = dst;
                    L("key drop -> %d\n", sol_key(&S, R(2) ? SOL_KEY_RETURN : SOL_KEY_SPACE, 0));
                }
            }
        } else {
            L("stock -> %d\n", sol_press(&S, SOL_STOCK, 0, mods));
        }
    } else if (what < 42) {
        L("stock -> %d\n", sol_press(&S, SOL_STOCK, S.board.p[SOL_STOCK].n - 1, R(6) == 0 ? SOL_MOD_CHEAT : mods));
    } else if (what < 52) {
        int p = R(14) - 1, n = p >= 0 ? S.board.p[p].n : 0, i = n ? R(n) : -1;
        r = sol_press(&S, p, i, mods);
        L("press %d %d %d -> %d\n", p, i, mods, r);
        if (r == SOL_PRESS_DRAG) {
            L("over -> %d\n", sol_drag_over(&S, R(14) - 1));
            switch (R(3)) {
            case 0: L("drop -> %d\n", sol_drop(&S, S.target)); break;
            case 1: L("drop -> %d\n", sol_drop(&S, R(14) - 1)); break;
            default: L("cancel\n"); sol_cancel_drag(&S); break;
            }
        }
    } else if (what < 60) {
        int p = R(13), n = S.board.p[p].n, i = n ? (R(3) ? n - 1 : R(n)) : -1;
        r = sol_dblclick(&S, p, i, mods);
        L("dblclick %d %d -> %d\n", p, i, r);
        if (r == SOL_PRESS_DRAG) L("drop -> %d\n", sol_drop(&S, S.target));
    } else if (what < 64) {
        L("autoplay -> %d\n", sol_autoplay(&S));
    } else if (what < 73) {
        L("undo\n");
        sol_command(&S, SOL_CMD_UNDO);
    } else if (what < 78) {
        L("redo\n");
        sol_command(&S, SOL_CMD_REDO);
    } else if (what < 89) {
        int key = keys[R((int)(sizeof keys / sizeof keys[0]))];
        L("key %d %d -> %d\n", key, mods, sol_key(&S, key, mods));
    } else if (what < 94) {
        int t = R(60);
        L("ticks %d\n", t);
        while (t-- > 0) sol_timer(&S, SOL_TIMER_CLOCK);
        if (R(20) == 0) { int m = R(2); L("minimized %d\n", m); sol_set_minimized(&S, m); }
    } else if (what < 96) {
        L("deal\n");
        now_value = (uint32_t)R(40000);
        sol_command(&S, SOL_CMD_DEAL);
    } else if (what < 98) {
        SolOptions o = S.opts;
        switch (R(6)) {
        case 0: o.draw = o.draw == 1 ? 3 : 1; break;
        case 1: o.scoring = R(3); break;
        case 2: o.timed = !o.timed; break;
        case 3: o.cumulative = !o.cumulative; break;
        case 4: o.outline = !o.outline; break;
        default: o.status_bar = !o.status_bar; break;
        }
        L("options -> %d\n", sol_apply_options(&S, &o));
    } else if (what < 99) {
        int b = R(14) - 1;
        L("back %d\n", b);
        sol_set_back(&S, b);
    } else {
        if (R(10) == 0) { L("forcewin\n"); sol_command(&S, SOL_CMD_FORCEWIN); }
        else { L("timer other\n"); sol_timer(&S, 12345); }
    }
    if (!S.dealt && !sol_dragging(&S) && R(3)) {
        L("deal (after)\n");
        now_value = (uint32_t)R(40000);
        sol_command(&S, SOL_CMD_DEAL);
    }
    (void)k;
}

int main(int argc, char **argv)
{
    static const uint32_t modes[] = { 0, 0x0B, 0x03, 0x1B, 0x13, 0x5B, 0x53, 0x2B, 0x23, 0x09, 0x0F, 0x01 };
    int games = argc > 1 ? atoi(argv[1]) : 400;
    long inputs = 0;
    out = stdout;
    for (int g = 0; g < games; g++) {
        rng_state = 0x9E3779B97F4A7C15ull * (uint64_t)(g + 1);
        use_post = R(2);
        deal_again_answer = R(2);
        now_value = (uint32_t)R(1 << 30);
        L("== game %d post %d again %d\n", g, use_post, deal_again_answer);
        init_session(modes[g % (int)(sizeof modes / sizeof modes[0])], R(3) ? R(14) : 0);
        state("init");
        if (R(8)) sol_command(&S, SOL_CMD_DEAL);
        else sol_deal(&S, (unsigned)R(32768), 0);
        state("dealt");
        int n = 200 + R(900);
        for (int k = 0; k < n; k++) {
            step(k);
            state("st");
            inputs++;
        }
        sol_free(&S);
    }
    fprintf(stderr, "sol_xp_compare: %d games, %ld inputs\n", games, inputs);
#ifdef SOL_NEW_API
    {
        long played = 0, won = 0;
        for (int m = 0; m < SOL_STATS_MODES; m++) {
            played += (long)S.stats.m[m].played;
            won += (long)S.stats.m[m].won;
        }
        fprintf(stderr, "sol_xp_compare: statistics written %ld times (the last session: %ld played, %ld won)\n",
                nstats_changed, played, won);
    }
#endif
    return 0;
}
