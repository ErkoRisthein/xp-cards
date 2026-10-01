/*
 * Solitaire HD — randomized playouts of the controller (src/solitaire/session.c) with invariants
 * checked after every input: 52 distinct cards in legal piles (sol_board_valid), the score (Vegas:
 * the deal's score + 5 per foundation card; Standard never negative; None 0), the Vegas pass limit,
 * the waste fan, the drag state; every Undo returns exactly to the position before the action it
 * undoes (a shadow stack), and at the end of each game Undo all returns to the deal, Redo all to the
 * position before. Inputs: legal and random drags (mouse and keyboard), presses, double-clicks,
 * autoplay, the stock with and without the cheat, keys with modifiers, clock ticks, deals, the
 * forced win, Undo and Redo. A third set of games runs with the extras on (auto-turn, Finish
 * automatically, click-to-move, Hint and its flash, Finish, statistics): the same invariants, plus no
 * face-down card left on top of a column after an action, and the statistics' sums.
 */
#include "sol_test.h"

typedef struct Snap { SolBoard b; int fan, recycles; } Snap;

static void take(const SolSession *s, Snap *p)
{
    p->b = s->board;
    p->fan = s->waste_fan;
    p->recycles = s->recycles;
}

static int snap_eq(const SolSession *s, const Snap *p)
{
    return sol_board_equal(&s->board, &p->b) && s->waste_fan == p->fan && s->recycles == p->recycles;
}

/* A random legal drag (src, index -> dst), or 0 if there is none. */
static int legal_move(const SolSession *s, int *src, int *idx, int *dst)
{
    int ms[600][3], n = 0;
    for (int p = SOL_WASTE; p < SOL_NPILES; p++) {
        const SolPile *q = &s->board.p[p];
        for (int i = 0; i < q->n; i++) {
            if (!sol_is_up(q->c[i])) continue;
            if (!sol_is_tab(p) && i != q->n - 1) continue;
            for (int d = 0; d < SOL_NPILES && n < 600; d++)
                if (sol_can_drop(&s->board, d, p, i)) { ms[n][0] = p; ms[n][1] = i; ms[n][2] = d; n++; }
        }
    }
    if (!n) return 0;
    int k = trand() % n;
    *src = ms[k][0];
    *idx = ms[k][1];
    *dst = ms[k][2];
    return 1;
}

/* A plain greedy player, so that some games go deep and are won: turn cards over, send cards home,
 * move runs that uncover a face-down card (kings to empty columns only then), the waste to the
 * tableau, otherwise the stock. Returns 0 if it found nothing (the stock is exhausted). */
static int smart_step(SolSession *s)
{
    SolBoard *b = &s->board;
    for (int t = SOL_TAB0; t < SOL_NPILES; t++)
        if (b->p[t].n && !sol_is_up(b->p[t].c[b->p[t].n - 1])) return sol_press(s, t, b->p[t].n - 1, 0) != 0;
    for (int p = SOL_WASTE; p < SOL_NPILES; p++) {
        if (sol_is_found(p) || !b->p[p].n) continue;
        for (int f = SOL_FOUND0; f < SOL_FOUND0 + 4; f++)
            if (sol_can_drop(b, f, p, b->p[p].n - 1)) return sol_dblclick(s, p, b->p[p].n - 1, 0) != 0;
    }
    for (int t = SOL_TAB0; t < SOL_NPILES; t++) {
        int n = b->p[t].n, i = n - sol_up_run(b, t);
        if (i >= n || i == 0) continue;            /* nothing face up, or nothing under the run */
        for (int d = SOL_TAB0; d < SOL_NPILES; d++)
            if (sol_can_drop(b, d, t, i) && sol_begin_drag(s, t, i)) return sol_drop(s, d);
    }
    if (b->p[SOL_WASTE].n)
        for (int d = SOL_TAB0; d < SOL_NPILES; d++)
            if (sol_can_drop(b, d, SOL_WASTE, b->p[SOL_WASTE].n - 1) && sol_begin_drag(s, SOL_WASTE, b->p[SOL_WASTE].n - 1))
                return sol_drop(s, d);
    return sol_press(s, SOL_STOCK, 0, 0) != 0;
}

static const int keys[] = { SOL_KEY_TAB, SOL_KEY_RETURN, SOL_KEY_ESCAPE, SOL_KEY_SPACE, SOL_KEY_END,
                            SOL_KEY_HOME, SOL_KEY_LEFT, SOL_KEY_UP, SOL_KEY_RIGHT, SOL_KEY_DOWN, SOL_KEY_A,
                            'B', 0x71 };

static int bad;
#define INV(c, ...) do { checks++; if (!(c)) { fails++; if (bad++ < 20) { printf("FAIL %s:%d: %s: ", \
    __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static long long steps_total, undos_checked, games_restored, wins, forced_wins, max_home;

static long long extra_finishes, extra_hints, extra_clicks;

static void play(unsigned game, uint32_t opts, int smart, int extras)
{
    SolSession s;
    Fake f;
    Reg r;
    char why[128];
    Snap *shadow = malloc(sizeof(Snap) * 4096);
    int nshadow = 0;
    start(&s, &f, &r, opts, (int)(game * 7919u % 32768u));
    if (extras) {
        SolExtras x = s.extras;
        x.auto_turn = 1;
        x.auto_finish = (game & 3) != 0;
        x.click_move = 1;
        sol_set_extras(&s, &x);
        sol_attach_stats(&s, NULL);
    }
    f.now = game * 13u;
    f.deal_again = (int)(game & 1);
    int base = s.score;                         /* the score right after the deal */
    unsigned seed = s.seed;
    tsrand(game * 2654435761u + opts);
    int nsteps = smart ? 1500 + trand() % 1500 : 300 + trand() % 500;
    for (int step = 0; step < nsteps; step++) {
        Snap prev;
        take(&s, &prev);
        int prev_hist = s.nhist;
        int prev_dealt = s.dealt;
        int what = trand() % 100, nanim = f.nanim;
        if (smart && trand() % 100 < 90) what = -1;
        int mods = trand() % 8 == 0 ? (trand() & 7) : 0;
        if (extras && what >= 0 && trand() % 6 == 0) {
            /* the extras' own inputs */
            int k = trand() % 4;
            if (k == 0) {
                sol_command(&s, SOL_CMD_HINT);
                if (s.hint) extra_hints++;
                for (int t = trand() % 10; t > 0; t--) sol_timer(&s, SOL_TIMER_HINT);
            } else if (k == 1) {
                sol_command(&s, SOL_CMD_FINISH);
            } else {                                /* a click on a random card: click-to-move */
                int p = trand() % 13, n = s.board.p[p].n, i = n ? trand() % n : -1;
                if (sol_press(&s, p, i, 0) == SOL_PRESS_DRAG) {
                    int t = sol_click_target(&s);
                    if (t >= 0) extra_clicks++;
                    sol_drop(&s, t);
                }
            }
            what = 200;
        }
        if (what == 200) {
        } else if (what < 0) {
            if (!sol_dragging(&s) && !smart_step(&s) && trand() % 20 == 0) sol_command(&s, SOL_CMD_DEAL);
        } else if (what < 30) {                 /* a legal drag, by mouse or keyboard */
            int src, idx, dst;
            if (!sol_dragging(&s) && legal_move(&s, &src, &idx, &dst)) {
                if (trand() % 4) {
                    if (sol_press(&s, src, idx, 0) == SOL_PRESS_DRAG) {
                        sol_drag_over(&s, trand() % 5 ? dst : trand() % 14 - 1);
                        if (trand() % 10) sol_drop(&s, s.target);
                        else sol_cancel_drag(&s);
                    }
                } else {
                    s.kbd_pile = src;
                    s.kbd_card = idx;
                    sol_key(&s, SOL_KEY_RETURN, 0);
                    if (sol_dragging(&s)) {
                        s.kbd_pile = dst;
                        sol_key(&s, trand() % 2 ? SOL_KEY_RETURN : SOL_KEY_SPACE, 0);
                    }
                }
            } else {
                sol_press(&s, SOL_STOCK, 0, mods);
            }
        } else if (what < 45) {
            sol_press(&s, SOL_STOCK, 0, trand() % 6 == 0 ? SOL_MOD_CHEAT : mods);
        } else if (what < 55) {                 /* a random press, drag and drop */
            int p = trand() % 14 - 1, n = p >= 0 ? s.board.p[p].n : 0;
            int i = n ? trand() % n : -1;
            if (sol_press(&s, p, i, mods) == SOL_PRESS_DRAG) {
                sol_drag_over(&s, trand() % 14 - 1);
                int k = trand() % 3;
                if (k == 0) sol_drop(&s, s.target);
                else if (k == 1) sol_drop(&s, trand() % 14 - 1);
                else sol_cancel_drag(&s);
            }
        } else if (what < 62) {                 /* double-click on a top card (or anywhere) */
            int p = trand() % 13, n = s.board.p[p].n;
            int i = n ? (trand() % 3 ? n - 1 : trand() % n) : -1;
            if (sol_dblclick(&s, p, i, mods) == SOL_PRESS_DRAG) sol_drop(&s, s.target);
        } else if (what < 66) {
            sol_autoplay(&s);
        } else if (what < 76) {
            sol_command(&s, SOL_CMD_UNDO);
        } else if (what < 81) {
            sol_command(&s, SOL_CMD_REDO);
        } else if (what < 93) {
            sol_key(&s, keys[trand() % (int)(sizeof keys / sizeof keys[0])], mods);
        } else if (what < 98) {
            for (int t = trand() % 60; t > 0; t--) sol_timer(&s, SOL_TIMER_CLOCK);
            if (trand() % 20 == 0) sol_set_minimized(&s, trand() & 1);
        } else if (what < 99) {
            if (trand() % 3 == 0) sol_command(&s, SOL_CMD_DEAL);
        } else {
            if (trand() % 40 == 0) sol_command(&s, SOL_CMD_FORCEWIN);
        }
        steps_total++;

        /* a game that ended (won; "Deal Again?" No) gets a new deal; a new deal restarts the shadow */
        int ended = prev_dealt && !s.dealt;
        if (ended) {
            if (s.forced_win) forced_wins++;
            else wins++;
        }
        if (s.dealt && total_found(&s) > max_home) max_home = total_found(&s);
        if (!s.dealt && !sol_dragging(&s)) sol_command(&s, SOL_CMD_DEAL);
        int newdeal = s.seed != seed;           /* never the same seed twice in a row */
        if (newdeal) {
            seed = s.seed;
            base = s.score;
        }

        /* invariants */
        INV(sol_board_valid(&s.board, why, sizeof why), "game %u step %d: %s", game, step, why);
        if (s.opts.scoring == SOL_SCORING_VEGAS && !s.forced_win && s.dealt)
            INV(s.score == base + 5 * total_found(&s), "game %u step %d: Vegas score %d, base %d, %d home",
                game, step, s.score, base, total_found(&s));
        if (s.opts.scoring == SOL_SCORING_VEGAS)
            INV(s.recycles <= s.draw - 1, "game %u step %d: %d recycles", game, step, s.recycles);
        if (s.opts.scoring == SOL_SCORING_STANDARD) INV(s.score >= 0, "game %u: score %d", game, s.score);
        if (s.opts.scoring == SOL_SCORING_NONE) INV(s.score == 0, "game %u: score %d", game, s.score);
        INV(s.waste_fan >= 0 && s.waste_fan <= 3, "fan %d", s.waste_fan);
        INV(sol_waste_fan(&s) <= s.board.p[SOL_WASTE].n, "fan");
        INV(sol_waste_fan_start(&s) >= 0, "fan start");
        if (sol_dragging(&s)) {
            const SolPile *q = &s.board.p[s.drag_pile];
            INV(s.drag_index >= 0 && s.drag_index < q->n && s.drag_count == q->n - s.drag_index &&
                sol_is_up(q->c[s.drag_index]), "drag");
            INV(s.target == -1 || sol_can_drop_on(&s, s.target), "target");
        } else {
            INV(s.target == -1, "a target without a drag");
        }
        INV(s.ticks >= 0 && s.ticks <= SOL_TICKS_MAX, "ticks");
        if (f.nanim > nanim) extra_finishes++;    /* Finish flew cards: by hand or automatically */
        if (extras && s.dealt && s.nhist > prev_hist)
            for (int t = SOL_TAB0; t < SOL_NPILES; t++)
                INV(!s.board.p[t].n || sol_is_up(s.board.p[t].c[s.board.p[t].n - 1]),
                    "game %u step %d: auto-turn left a face-down card on column %d", game, step, t);
        if (extras) {
            for (int m = 0; m < SOL_STATS_MODES; m++)
                INV(s.stats.m[m].won <= s.stats.m[m].played, "stats: won > played");
        }
        INV(s.timer_on == sol_clock_running(&s), "timer state");
        int kp, kc;
        sol_kbd_cursor(&s, &kp, &kc);
        INV(kp >= 0 && kp < SOL_NPILES && kc >= 0 && (kc < s.board.p[kp].n || kc == 0), "kbd");

        /* the shadow history: an action pushes the position before it; an Undo must land on it */
        if (ended || newdeal) {
            nshadow = 0;
        } else if (s.nhist == prev_hist + 1) {
            if (nshadow < 4096) shadow[nshadow++] = prev;
        } else if (s.nhist == prev_hist - 1 && nshadow > 0) {
            Snap *want = &shadow[--nshadow];
            INV(snap_eq(&s, want), "game %u step %d: Undo did not restore the position", game, step);
            undos_checked++;
        } else if (s.nhist == 0) {
            nshadow = 0;
        }
        INV(nshadow == s.nhist || nshadow == 4096, "game %u step %d: shadow %d vs history %d", game, step,
            nshadow, s.nhist);
    }
    /* end of the game: Undo all = the deal; Redo all = back here */
    if (sol_dragging(&s)) sol_cancel_drag(&s);
    if (s.dealt && s.nhist > 0) {
        Snap end;
        take(&s, &end);
        int end_score = s.score, n = s.nhist, r0 = s.nredo;   /* r0: undone earlier, not redone */
        while (sol_undo(&s)) {}
        SolBoard d;
        sol_deal_board(&d, s.seed, NULL);
        INV(sol_board_equal(&s.board, &d) && s.recycles == 0 && s.waste_fan == 0,
            "game %u: Undo all did not return to the deal", game);
        if (s.opts.scoring == SOL_SCORING_VEGAS) INV(s.score == base, "game %u: Vegas score %d after Undo all", game, s.score);
        if (s.opts.scoring == SOL_SCORING_STANDARD) INV(s.score == 0, "game %u: Standard score %d after Undo all", game, s.score);
        INV(s.nredo == n + r0, "redo stack %d, expected %d", s.nredo, n + r0);
        for (int k = 0; k < n; k++) INV(sol_redo(&s), "game %u: Redo %d of %d refused", game, k, n);
        INV(snap_eq(&s, &end), "game %u: Redo all did not return", game);
        if (s.opts.scoring == SOL_SCORING_VEGAS) INV(s.score == end_score, "game %u: Vegas score after Redo all", game);
        games_restored++;
    }
    free(shadow);
    sol_free(&s);
}

int main(void)
{
    static const uint32_t modes[] = {
        0x0B, 0x03,             /* Standard, draw three / one */
        0x1B, 0x13, 0x5B,       /* Vegas, draw three / one, cumulative */
        0x2B, 0x23,             /* None */
        0x09, 0x0F };           /* untimed; outline dragging */
    int n = 0;
    for (unsigned g = 1; g <= 450; g++) play(g, modes[g % (sizeof modes / sizeof modes[0])], 0, 0), n++;
    for (unsigned g = 1001; g <= 1300; g++) play(g, modes[g % (sizeof modes / sizeof modes[0])], 1, 0), n++;
    printf("%d games, %lld inputs, %lld wins (+%lld forced), up to %lld cards home, %lld undos checked "
           "against the shadow history, %lld games undone to the deal and redone\n", n, steps_total, wins,
           forced_wins, max_home, undos_checked, games_restored);
    steps_total = wins = forced_wins = max_home = undos_checked = games_restored = 0;
    n = 0;
    for (unsigned g = 2001; g <= 2300; g++) play(g, modes[g % (sizeof modes / sizeof modes[0])], (g >> 1) & 1, 1), n++;
    printf("extras on: %d games, %lld inputs, %lld wins (+%lld forced), %lld finishes, %lld hints, %lld "
           "click moves, %lld undos checked, %lld games undone to the deal and redone\n", n, steps_total, wins,
           forced_wins, extra_finishes, extra_hints, extra_clicks, undos_checked, games_restored);
    return test_summary("test_sol_playout");
}
