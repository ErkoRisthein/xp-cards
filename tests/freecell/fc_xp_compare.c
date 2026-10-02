/*
 * FreeCell HD — proof that the v1.4 extras change nothing while they are off: random play through the
 * session API of FreeCell HD 1.3 (git FC_REF, default 904243b: XP's FreeCell plus the v1.1 / v1.2
 * extras) and today's, logging every UI callback, every registry access and the whole observable state
 * after every input. `make fc-xp-compare` builds this file twice, against 1.3's src/freecell (game,
 * session, assist, solver, stats, wondeals; extracted with git show) and against today's, runs both and
 * compares the two logs byte for byte.
 *
 * Inputs: clicks and double-clicks (random and legal targets), digit keys and H, the right-button peek,
 * mouse moves (the cursor and the king), the activation click, every timer, New / Select / Restart (the
 * dialogs answered at random), Undo, Redo, the cheat, Hint, Finish, Exit (the resign question), the
 * MoveCol / YouWin / YouLose answers, Options changes (XP's three and the 1.1 / 1.2 extras: time and
 * moves, the standard supermove rule, the full range, the warning, Finish automatically) and the
 * background solver's answers (the real solver with a small budget, or a scripted status). Today's
 * build (-DFC_NEW_API) also sets the new ui.confirm callback, which 1.3 does not have: a single call
 * of it with the extras off is a difference.
 *
 *   fc_xp_compare GAMES > log     (default 300 games)
 */
#include "freecell/game.h"
#include "freecell/session.h"
#include "freecell/solver.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *out;
static long nlines;

static void L(const char *fmt, ...)
{
    va_list ap;
    if (++nlines > 40000000L) {                  /* a runaway loop: stop before the disk fills */
        fprintf(stderr, "fc_xp_compare: too many log lines\n");
        exit(3);
    }
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

static FcSession S;

static uint32_t board_hash(const FcBoard *b)
{
    const uint8_t *p = (const uint8_t *)b;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof *b; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static void state(const char *tag)
{
    const FcSession *s = &S;
    FcsViewState v;
    FcStatsView sv;
    char t[32];
    fcs_view_state(s, &v);
    fc_stats_view(&s->stats, &sv);
    fcs_format_time(fcs_elapsed_ms(s), t, sizeof t);
    L("%s b%08x g%d ip%d d%d sf%d ch%d sel%d.%d.%d k%d bk%d pk%d.%d kp%d fl%d sw%d busy%d q%d h%d r%d mv%d "
      "clk%d '%s' u%d rd%d rs%d hi%d fi%d hw%d | v %d.%d %d.%d %d %d %d %d.%d | st %u %u %u %u %u %u %u %u %d | "
      "o%d%d%d x%d%d%d%d%d | as %u %d %d %d %d %d %d cur",
      tag, board_hash(&s->board), s->game_number, s->in_progress, s->dealt, s->select_flag, s->cheat, s->sel,
      s->sel_col, s->sel_pos, s->king, s->big_king, s->peek_col, s->peek_pos, s->kbd_peek, s->flash_left,
      s->swallow_click, s->busy, s->quiet, s->nhist, s->nredo, fcs_moves(s), fcs_clock_running(s), t,
      fcs_undo_enabled(s), fcs_redo_enabled(s), fcs_restart_enabled(s), fcs_hint_enabled(s),
      fcs_finish_enabled(s), fcs_hint_waiting(s), v.sel_col, v.sel_pos, v.peek_col, v.peek_pos, v.king,
      v.big_king, v.no_game, v.hint_col, v.hint_pos, (unsigned)sv.session_won, (unsigned)sv.session_lost,
      (unsigned)sv.won, (unsigned)sv.lost, (unsigned)sv.wins, (unsigned)sv.losses, (unsigned)sv.streak,
      (unsigned)sv.stype, s->stats.last_recorded, s->opts.messages, s->opts.quick, s->opts.dblclick,
      s->extras.show_time_moves, s->extras.standard_supermove, s->extras.full_range, s->extras.warn_unwinnable,
      s->extras.auto_finish, (unsigned)s->as.req_id, s->as.req_pending, s->as.hint, s->as.hint_step,
      s->as.warned, s->as.deal_lost, s->as.sol_n);
    for (int col = -1; col <= 8; col++)
        L(" %d", fcs_cursor(s, col, col == 0 ? R(8) : col > 0 ? fc_last_index(&s->board, col) : -1, col >= 0));
    L("\n");
}

/* ---- the UI and the registry, logged ------------------------------------------------------------- */

static int a_resign, a_movecol, a_gamenum_ok, a_gamenum, a_win, a_win_sel, a_lose, a_lose_same, a_cheat, use_post;
static int gn_calls;
static uint32_t now_seed, now_ms;
static int posted[8], nposted;

/* the background solver: requests kept, answered later by step() */
static int req_on;
static uint32_t req_id;
static FcBoard req_board;
static int req_std;
static FcSolver *solver;

static void c_message(void *c, int id, const char *t) { L("  message %d '%s'\n", id, t); }
static int  c_resign(void *c) { L("  resign? -> %d\n", a_resign); return a_resign; }
static int  c_movecol(void *c) { L("  movecol? -> %d\n", a_movecol); return a_movecol; }
static int  c_gamenum(void *c, int init, int *v)
{
    if (gn_calls++ > 0) a_gamenum = 1 + (a_gamenum & 0x3FFF);   /* asked again (invalid): a valid one */
    L("  gamenum(%d) -> %d %d\n", init, a_gamenum_ok, a_gamenum);
    if (!a_gamenum_ok) return 0;
    *v = a_gamenum;
    return 1;
}
static int  c_win(void *c, int *sel)
{
    L("  youwin(%d) -> %d %d\n", *sel, a_win, a_win_sel);
    *sel = a_win_sel;
    return a_win;
}
static int  c_lose(void *c, int *same)
{
    L("  youlose(%d) -> %d %d\n", *same, a_lose, a_lose_same);
    *same = a_lose_same;
    return a_lose;
}
static int  c_cheat(void *c) { L("  cheat? -> %d\n", a_cheat); return a_cheat; }
static void c_anim(void *c, const FcStep *st, int fwd)
{
    L("  anim %d %d.%d %d.%d %d b%08x\n", fwd, st->src_col, st->src_pos, st->dst_col, st->dst_pos, st->card,
      board_hash(&S.board));
}
static void c_inval(void *c) { L("  inval\n"); }
static void c_title(void *c, const char *t) { L("  title '%s'\n", t); }
static void c_cards_left(void *c, int n) { L("  cardsleft %d\n", n); }
static void c_menu(void *c, int u, int r, int rd) { L("  menu %d %d %d\n", u, r, rd); }
static void c_timer(void *c, int id, int ms) { L("  timer %d %d\n", id, ms); }
static void c_flash(void *c, int inv) { L("  flash %d\n", inv); }
static void c_post(void *c, int cmd)
{
    L("  post %d\n", cmd);
    if (nposted < 8) posted[nposted++] = cmd;
}
static uint32_t c_now(void *c) { return now_seed; }
static uint32_t c_now_ms(void *c) { return now_ms; }
static void c_status(void *c) { L("  status\n"); }
static void c_solve(void *c, uint32_t id, const FcBoard *b, int std)
{
    L("  solve %u b%08x %d\n", (unsigned)id, board_hash(b), std);
    req_on = 1;
    req_id = id;
    req_board = *b;
    req_std = std;
}
static void c_cancel(void *c) { L("  solve_cancel\n"); req_on = 0; }
static void c_assist_menu(void *c, int h, int f) { L("  assistmenu %d %d\n", h, f); }
#ifdef FC_NEW_API
static int c_confirm(void *c, int id, const char *t) { L("  NEW confirm %d\n", id); return 1; }
#endif

static struct { char name[32]; uint32_t v; int set; } reg[32];

static int r_get(void *ctx, const char *name, uint32_t *v)
{
    for (int i = 0; i < 32; i++)
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
    int f = -1;
    L("  reg set %s %u\n", name, (unsigned)v);
    for (int i = 0; i < 32; i++) {
        if (reg[i].set && !strcmp(reg[i].name, name)) { reg[i].v = v; return; }
        if (!reg[i].set && f < 0) f = i;
    }
    if (f >= 0) {
        snprintf(reg[f].name, sizeof reg[f].name, "%s", name);
        reg[f].v = v;
        reg[f].set = 1;
    }
}
static void r_del(void *ctx, const char *name)
{
    L("  reg del %s\n", name);
    for (int i = 0; i < 32; i++)
        if (reg[i].set && !strcmp(reg[i].name, name)) reg[i].set = 0;
}
static void r_flush(void *ctx) { L("  reg flush\n"); }
static int r_legacy(void *ctx, const char *key, uint32_t *v) { L("  reg legacy %s\n", key); return 0; }

static uint8_t won_file[1 << 16];
static long won_len = -1;
static long w_read(void *ctx, void *buf, size_t cap)
{
    L("  won read\n");
    if (won_len < 0) return -1;
    if ((size_t)won_len > cap) return (long)cap + 1;
    memcpy(buf, won_file, (size_t)won_len);
    return won_len;
}
static int w_write(void *ctx, const void *data, size_t len)
{
    L("  won write %lu\n", (unsigned long)len);
    if (len > sizeof won_file) return 0;
    memcpy(won_file, data, len);
    won_len = (long)len;
    return 1;
}

static void init_session(void)
{
    FcSessionUI ui;
    CeStore st;
    CeBlobIO io;
    memset(&ui, 0, sizeof ui);
    memset(&st, 0, sizeof st);
    ui.message = c_message;
    ui.confirm_resign = c_resign;
    ui.ask_move_column = c_movecol;
    ui.ask_game_number = c_gamenum;
    ui.you_win = c_win;
    ui.you_lose = c_lose;
    ui.cheat_prompt = c_cheat;
    ui.animate_step = c_anim;
    ui.invalidate = c_inval;
    ui.set_title = c_title;
    ui.cards_left_changed = c_cards_left;
    ui.menu_state = c_menu;
    ui.set_timer = c_timer;
    ui.flash = c_flash;
    ui.post_command = use_post ? c_post : NULL;
    ui.now_seed = c_now;
    ui.now_ms = c_now_ms;
    ui.status_changed = c_status;
    ui.solve_start = c_solve;
    ui.solve_cancel = c_cancel;
    ui.assist_menu = c_assist_menu;
#ifdef FC_NEW_API
    ui.confirm = c_confirm;
#endif
    st.get = r_get;
    st.set = r_set;
    st.del = r_del;
    st.flush = r_flush;
    st.legacy_get = r_legacy;
    io.ctx = NULL;
    io.read = w_read;
    io.write = w_write;
    req_on = 0;
    fcs_init(&S, &ui, &st);
    L("  won attach %d\n", fcs_attach_won_deals(&S, &io));
}

/* ---- inputs ------------------------------------------------------------------------------------- */

static void answers(void)
{
    a_resign = R(2);
    a_movecol = R(3) - 1;
    a_gamenum_ok = R(5) != 0;
    a_gamenum = R(6) == 0 ? R(5) - 2 : 1 + R(R(4) ? 32000 : 1000000);
    a_win = R(2);
    a_win_sel = R(2);
    a_lose = R(2);
    a_lose_same = R(2);
    a_cheat = R(4) == 0 ? 2 : R(3) == 0 ? 1 : 0;
    gn_calls = 0;
}

/* A legal click pair from the solver's move list (so that games go somewhere). */
static int legal_pair(int *sc, int *sp, int *dc, int *dp)
{
    FcSolveMove m[FC_SOLVE_MAX_MOVES];
    int n = fc_solve_moves(&S.board, S.extras.standard_supermove, m);
    if (n <= 0) return 0;
    n = R(n);
    *sc = m[n].src_col;
    *sp = m[n].src_pos;
    *dc = m[n].dst_col;
    *dp = m[n].dst_pos;
    return 1;
}

static void answer_solver(void)
{
    FcSolveResult res;
    uint32_t id = req_id;
    int k = R(4);
    req_on = 0;
    if (k == 0) {
        L("solve_done %u scripted gave up -> %d\n", (unsigned)id, fcs_solve_done(&S, id, FC_SOLVE_GAVE_UP, NULL, 0));
        return;
    }
    if (k == 1 && R(3) == 0) {
        L("solve_done %u scripted unsolvable -> %d\n", (unsigned)id,
          fcs_solve_done(&S, id, FC_SOLVE_UNSOLVABLE, NULL, 0));
        return;
    }
    fc_solve(solver, &req_board, req_std, NULL, &res);
    L("solve_done %u status %d n %d -> %d\n", (unsigned)id, res.status, res.nmoves,
      fcs_solve_done(&S, id, res.status, res.moves, res.nmoves));
}

static int rand_col(void) { int c = R(10) - 1; return c < 0 ? FCS_MISS : c; }
static int rand_pos(int col) { return col >= 1 && col <= 8 ? (R(3) ? fc_last_index(&S.board, col) : R(8)) : R(8); }

static void step(void)
{
    int what = R(1000), col = rand_col(), pos = rand_pos(col);
    answers();
    now_ms += (uint32_t)R(3000);
    if (what < 350) {
        int sc, sp, dc, dp;
        if (!S.sel && legal_pair(&sc, &sp, &dc, &dp)) {
            L("pair %d.%d -> %d.%d\n", sc, sp, dc, dp);
            fcs_click(&S, sc, sp);
            if (R(10)) fcs_click(&S, dc, dp);
        } else {
            L("click %d.%d\n", col, pos);
            fcs_click(&S, col, pos);
        }
    } else if (what < 550) {
        L("click %d.%d\n", col, pos);
        fcs_click(&S, col, pos);
    } else if (what < 610) {
        L("dblclick %d.%d\n", col, pos);
        fcs_dblclick(&S, col, pos);
    } else if (what < 680) {
        int ch = R(12) == 0 ? 'h' : R(15) == 0 ? 'H' : '0' + R(10);
        L("char %c\n", ch);
        fcs_char(&S, ch);
    } else if (what < 700) {
        L("rbutton %d.%d\n", col, pos);
        fcs_rbutton_down(&S, col, pos);
        if (R(3)) fcs_rbutton_up(&S);
    } else if (what < 760) {
        L("move %d.%d -> %d\n", col, pos, fcs_mouse_move(&S, col, pos, R(2)));
    } else if (what < 765) {
        L("activate\n");
        fcs_mouse_activate(&S);
    } else if (what < 815) {
        static const int timers[4] = { FCS_TIMER_FLASH, FCS_TIMER_PEEK, FCS_TIMER_HINT, FCS_TIMER_HINT_WAIT };
        int id = timers[R(4)];
        L("timer %d\n", id);
        fcs_timer(&S, id);
    } else if (what < 860) {
        L("undo\n");
        fcs_command(&S, FCS_CMD_UNDO);
    } else if (what < 885) {
        L("redo\n");
        fcs_command(&S, FCS_CMD_REDO);
    } else if (what < 900) {
        L("hint\n");
        fcs_command(&S, FCS_CMD_HINT);
    } else if (what < 910) {
        L("finish\n");
        fcs_command(&S, FCS_CMD_FINISH);
    } else if (what < 935) {
        static const int cmds[4] = { FCS_CMD_NEW, FCS_CMD_SELECT, FCS_CMD_RESTART, FCS_CMD_CHEAT };
        int cmd = cmds[R(4)];
        now_seed += (uint32_t)R(3);
        L("command %d\n", cmd);
        fcs_command(&S, cmd);
    } else if (what < 960) {
        switch (R(8)) {
        case 0: S.opts.messages = !S.opts.messages; break;
        case 1: S.opts.quick = !S.opts.quick; break;
        case 2: S.opts.dblclick = !S.opts.dblclick; break;
        case 3: S.extras.show_time_moves = !S.extras.show_time_moves; break;
        case 4: S.extras.standard_supermove = !S.extras.standard_supermove; break;
        case 5: S.extras.full_range = !S.extras.full_range; break;
        case 6: S.extras.warn_unwinnable = !S.extras.warn_unwinnable; break;
        default: S.extras.auto_finish = !S.extras.auto_finish; break;
        }
        L("options\n");
        fcs_options_changed(&S);
    } else if (what < 963) {
        L("close -> %d\n", fcs_close(&S));
    } else {
        L("idle\n");
    }
    if (req_on && R(3) == 0) answer_solver();
    while (nposted > 0) {
        int cmd = posted[0];
        memmove(posted, posted + 1, sizeof posted[0] * (size_t)--nposted);
        answers();
        L("posted %d\n", cmd);
        fcs_command(&S, cmd);
    }
    /* keep a game going */
    if (S.game_number == 0 && R(3)) {
        answers();
        a_gamenum_ok = 1;
        L("new game\n");
        fcs_command(&S, R(2) ? FCS_CMD_NEW : FCS_CMD_SELECT);
    }
}

int main(int argc, char **argv)
{
    int games = argc > 1 ? atoi(argv[1]) : 300;
    long inputs = 0;
    out = stdout;
    solver = fc_solver_new(5000);
    if (!solver) return 2;
    for (int g = 0; g < games; g++) {
        rng_state = 0x9E3779B97F4A7C15ull * (uint64_t)(g + 1);
        use_post = R(2);
        now_seed = 1700000000u + (uint32_t)R(1 << 20);
        now_ms = (uint32_t)R(1 << 30);
        memset(reg, 0, sizeof reg);
        if (R(2)) won_len = -1;
        L("== game %d post %d\n", g, use_post);
        init_session();
        if (R(4) == 0) S.extras.warn_unwinnable = 1;
        if (R(4) == 0) S.extras.auto_finish = 1;
        if (R(4) == 0) S.extras.standard_supermove = 1;
        state("init");
        answers();
        a_gamenum_ok = 1;
        fcs_command(&S, R(2) ? FCS_CMD_NEW : FCS_CMD_SELECT);
        state("dealt");
        int n = 150 + R(450);
        for (int k = 0; k < n; k++) {
            step();
            state("st");
            inputs++;
        }
        fcs_free(&S);
    }
    fc_solver_free(solver);
    fprintf(stderr, "fc_xp_compare: %d games, %ld inputs\n", games, inputs);
    return 0;
}
