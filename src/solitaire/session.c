/*
 * Solitaire HD — the XP Solitaire controller (see session.h). Section numbers (§n) refer to
 * docs/xp-reference/solitaire/rules.md, addresses to XP's sol.exe.
 */
#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "solver.h"
#include "winnable_seeds.h"

/* ---- UI helpers ---------------------------------------------------------------------------------- */

static void ui_invalidate(SolSession *s)
{
    if (s->ui.invalidate) s->ui.invalidate(s->ui.ctx);
}

static void ui_status(SolSession *s)
{
    if (s->ui.status_changed) s->ui.status_changed(s->ui.ctx);
}

static uint32_t ui_now(SolSession *s)
{
    return s->ui.now_seed ? s->ui.now_seed(s->ui.ctx) : 0;
}

static void ui_kbd_cursor(SolSession *s)
{
    int p, c;
    if (!s->ui.kbd_cursor) return;
    sol_kbd_cursor(s, &p, &c);
    s->ui.kbd_cursor(s->ui.ctx, p, c, sol_dragging(s));
}

static void ui_set_timer(SolSession *s, int id, int ms)
{
    if (s->ui.set_timer) s->ui.set_timer(s->ui.ctx, id, ms);
}

const char *sol_message_text(int id)
{
    switch (id) {
    case SOL_MSG_NO_HINT:         return "No hint is available.";
    case SOL_MSG_UNWINNABLE:      return "This game can no longer be won. Use Undo to go back.";
    case SOL_MSG_UNWINNABLE_DEAL: return "This game cannot be won.";
    case SOL_MSG_UNDO_ALL:        return "Do you want to undo all your moves and return to the start of the game?";
    case SOL_MSG_NO_USEFUL:       return "There are no more useful moves.";
    case SOL_ASK_NO_MOVES:        return "There are no more moves. What do you want to do?";
    case SOL_ASK_NEW_GAME:        return "A game is in progress. What do you want to do?";
    case SOL_ASK_EXIT:            return "A game is in progress. What do you want to do?";
    case SOL_ASK_RESUME:          return "You have a saved game. What do you want to do?";
    case SOL_ASK_SETTINGS:        return "The new settings apply to your next game. What do you want to do?";
    }
    return "";
}

static void ui_message(SolSession *s, int id)
{
    if (!s->ui.message) return;
    s->busy++;                          /* a modal box: input and commands wait */
    s->ui.message(s->ui.ctx, id, sol_message_text(id));
    s->busy--;
}

/* 2c: a question with several answers; def without a UI (or for an answer out of range). */
static int ui_choose(SolSession *s, int id, int def, int nanswers)
{
    int r;
    if (!s->ui.choose) return def;
    s->busy++;
    r = s->ui.choose(s->ui.ctx, id, sol_message_text(id));
    s->busy--;
    return r >= 0 && r < nanswers ? r : def;
}

static uint32_t ui_today(SolSession *s)
{
    return s->ui.today ? s->ui.today(s->ui.ctx) : 0;
}

static int ui_confirm(SolSession *s, int id)
{
    int r;
    if (!s->ui.confirm) return 1;
    s->busy++;
    r = s->ui.confirm(s->ui.ctx, id, sol_message_text(id));
    s->busy--;
    return r != 0;
}

/* ---- Score and clock ----------------------------------------------------------------------------- */

static int score_event(SolSession *s, int ev)
{
    int r = sol_change_score(&s->score, ev, s->opts.scoring, s->opts.timed, s->draw, s->ticks, s->recycles);
    if (s->opts.scoring != SOL_SCORING_NONE) ui_status(s);    /* DefChangeScore redraws the status */
    return r;
}

/* KlondTimer 0x1005189: the clock counts only when timed, dealt, started by a press and not minimized. */
static int clock_should_run(const SolSession *s)
{
    return s->opts.timed && s->dealt && s->input && !s->minimized;
}

static void update_timer(SolSession *s)
{
    int want = clock_should_run(s);
    if (want == s->timer_on) return;
    s->timer_on = want;
    ui_set_timer(s, SOL_TIMER_CLOCK, want ? SOL_TICK_MS : 0);
}

static void start_input(SolSession *s)
{
    if (s->input) return;
    s->input = 1;
    update_timer(s);
}

/* ---- History ------------------------------------------------------------------------------------- */

static int stack_push(SolAction **arr, int *n, int *cap, const SolAction *a)
{
    if (*n == *cap) {
        int nc = *cap ? *cap * 2 : 64;
        SolAction *p = realloc(*arr, (size_t)nc * sizeof **arr);
        if (!p) return 0;               /* out of memory: the action happened but cannot be undone */
        *arr = p;
        *cap = nc;
    }
    (*arr)[(*n)++] = *a;
    return 1;
}

static void begin_action(SolSession *s, int type)
{
    SolAction *a = &s->work;
    memset(a, 0, sizeof *a);
    a->type = (uint8_t)type;
    sol_board_pack(&s->board, a->board);
    a->waste_fan = (uint8_t)s->waste_fan;
    a->score = s->score;
    a->recycles = s->recycles;
    a->clock_pen = s->clock_pen;
}

static void stats_note(SolSession *s);
static void csel_clear(SolSession *s);

/* Dead ends (2c): what the last committed action was for the tracking. */
enum { NM_RESET = 0, NM_DRAW = 1, NM_RECYCLE = 2 };

static void commit_action(SolSession *s, int keep_redo)
{
    const SolAction *w = &s->work;
    s->nm_kind = keep_redo || w->autoturn || w->nauto ? NM_RESET
               : w->type == SOL_ACT_DRAW ? NM_DRAW : w->type == SOL_ACT_RECYCLE ? NM_RECYCLE : NM_RESET;
    stack_push(&s->hist, &s->nhist, &s->hist_cap, &s->work);
    if (!keep_redo) {
        s->nredo = 0;
        s->redo_group = 0;
    }
    s->undo_fresh = 1;
    csel_clear(s);                      /* extra: an action ends a click selection */
    stats_note(s);                      /* extra: the game counts as played from its first action */
}

static void clear_history(SolSession *s)
{
    s->nhist = 0;
    s->nredo = 0;
    s->redo_group = 0;
}

/* ---- Board operations (no history) --------------------------------------------------------------- */

/* DefMove + ScoreMove: the top n cards of src onto dst, then the score event of the move. */
static void move_cards(SolSession *s, int src, int dst, int n)
{
    SolPile *a = &s->board.p[src], *b = &s->board.p[dst];
    memcpy(&b->c[b->n], &a->c[a->n - n], (size_t)n);
    b->n = (uint8_t)(b->n + n);
    a->n = (uint8_t)(a->n - n);
    if (src == SOL_WASTE) s->waste_fan = s->waste_fan > n ? s->waste_fan - n : 0;
    int ev = sol_move_event(dst, src);
    if (ev >= 0) score_event(s, ev);
}

/* KlondMouseDown's draw (§3.1): the packet of n turned over, so the card that was n-th from the top of
 * the stock ends on top of the waste; the new cards form the fan. No score. */
static int do_draw(SolSession *s, int n)
{
    SolPile *st = &s->board.p[SOL_STOCK], *w = &s->board.p[SOL_WASTE];
    if (n > st->n) n = st->n;
    for (int i = 0; i < n; i++) w->c[w->n++] = (SolCard)(st->c[--st->n] | SOL_UP);
    s->waste_fan = n;
    return n;
}

/* The recycle (§3.1): the whole waste turned over onto the empty stock; the count first, then the score. */
static void do_recycle(SolSession *s)
{
    SolPile *st = &s->board.p[SOL_STOCK], *w = &s->board.p[SOL_WASTE];
    s->recycles++;
    score_event(s, SOL_EV_RECYCLE);
    while (w->n) st->c[st->n++] = (SolCard)(w->c[--w->n] & ~SOL_UP);
    s->waste_fan = 0;
}

static void do_turn(SolSession *s, int pile)
{
    SolPile *p = &s->board.p[pile];
    p->c[p->n - 1] |= SOL_UP;
    score_event(s, SOL_EV_TURN);
}

static int recycle_allowed(const SolSession *s)
{
    if (s->board.p[SOL_STOCK].n != 0 || s->board.p[SOL_WASTE].n == 0) return 0;
    /* Vegas pass limit 0x1004C3B: draw one 1 pass, draw three 3 passes */
    return !(s->opts.scoring == SOL_SCORING_VEGAS && s->recycles >= s->draw - 1);
}

/* Extra (auto_turn): every face-down card left on top of a column is turned over, scored as XP's
 * click, and recorded in the action being built (its Redo turns them again). */
static void auto_turn(SolSession *s)
{
    if (!s->extras.auto_turn) return;
    for (int k = 0; k < 7; k++) {
        const SolPile *p = &s->board.p[SOL_TAB0 + k];
        if (p->n && !sol_is_up(p->c[p->n - 1])) {
            do_turn(s, SOL_TAB0 + k);
            s->work.autoturn |= (uint8_t)(1u << k);
        }
    }
}

/* Redo: the turns recorded with the action. */
static void replay_turns(SolSession *s, uint8_t bits)
{
    for (int k = 0; k < 7; k++) {
        const SolPile *p = &s->board.p[SOL_TAB0 + k];
        if ((bits & (1u << k)) && p->n && !sol_is_up(p->c[p->n - 1])) {
            do_turn(s, SOL_TAB0 + k);
            s->work.autoturn |= (uint8_t)(1u << k);
        }
    }
}

/* ---- Extra (auto_home): safe cards home after an action, as part of it ---------------------------- */

static void record_auto(SolSession *s, unsigned op)
{
    if (s->work.nauto < SOL_MAX_AUTO) s->work.autos[s->work.nauto++] = (uint8_t)op;
}

/* auto_turn during the cascade: the turns go into the sequence (a column can turn more than once). */
static void auto_turn_seq(SolSession *s)
{
    if (!s->extras.auto_turn) return;
    for (int k = 0; k < 7; k++) {
        const SolPile *p = &s->board.p[SOL_TAB0 + k];
        if (p->n && !sol_is_up(p->c[p->n - 1])) {
            do_turn(s, SOL_TAB0 + k);
            record_auto(s, SOL_AUTO_TURN | (unsigned)(SOL_TAB0 + k));
        }
    }
}

/* After an action and its auto-turns (still being built): every card that is safe to go home flies
 * there (ui.animate_move, before the move, as Finish's), one at a time, the auto-turns after each.
 * Returns the cards moved. */
static int auto_home(SolSession *s)
{
    int src, dst, moved = 0;
    if (!s->extras.auto_home) return 0;
    while (s->work.nauto + 8 <= SOL_MAX_AUTO && sol_auto_home_step(&s->board, &src, &dst)) {
        if (s->ui.animate_move) {
            s->busy++;                  /* the flight: commands and input wait */
            s->ui.animate_move(s->ui.ctx, src, dst);
            s->busy--;
        }
        move_cards(s, src, dst, 1);
        record_auto(s, (unsigned)(src * 4 + (dst - SOL_FOUND0)));
        moved++;
        if (sol_is_won(&s->board)) break;
        auto_turn_seq(s);
    }
    return moved;
}

/* Redo: the sequence again, exactly (each step checked; it always fits right after its Undo). */
static void replay_autos(SolSession *s, const SolAction *a)
{
    for (int i = 0; i < a->nauto && i < SOL_MAX_AUTO; i++) {
        unsigned op = a->autos[i];
        if (op & SOL_AUTO_TURN) {
            int t = (int)(op & 0x7F);
            const SolPile *p = &s->board.p[t < SOL_NPILES ? t : 0];
            if (!sol_is_tab(t) || !p->n || sol_is_up(p->c[p->n - 1])) return;
            do_turn(s, t);
        } else {
            int src = (int)(op >> 2), dst = SOL_FOUND0 + (int)(op & 3);
            const SolPile *p = &s->board.p[src < SOL_NPILES ? src : 0];
            if (src >= SOL_NPILES || !p->n || !sol_is_up(p->c[p->n - 1]) ||
                !sol_can_drop(&s->board, dst, src, p->n - 1)) return;
            move_cards(s, src, dst, 1);
        }
        record_auto(s, op);
    }
}

static void clear_drag(SolSession *s)
{
    s->drag_pile = -1;
    s->drag_index = 0;
    s->drag_count = 0;
    s->drag_kbd = 0;
    s->target = -1;
    s->target_ui = 0;
}

/* ---- Statistics (extra) -------------------------------------------------------------------------- */

static void stats_changed(SolSession *s)
{
    if (s->ui.stats_changed) s->ui.stats_changed(s->ui.ctx);
}

static int game_mode(const SolSession *s) { return sol_stats_mode(s->draw, s->game_scoring); }

/* The game's own score for the statistics: Vegas without the Cumulative carry-over. */
static int game_score(const SolSession *s, int *has)
{
    *has = s->game_scoring != SOL_SCORING_NONE;
    return s->game_scoring == SOL_SCORING_VEGAS ? s->score - s->carry : s->score;
}

/* After every committed action: the game counts as played from its first one. */
static void stats_note(SolSession *s)
{
    if (!s->stats_on || s->counted || !s->dealt) return;
    s->counted = 1;
    sol_stats_played(&s->stats, game_mode(s));
    stats_changed(s);
}

static void stats_win(SolSession *s)
{
    int has, score;
    if (!s->stats_on || !s->counted) return;
    s->counted = 0;
    score = game_score(s, &has);
    sol_stats_won(&s->stats, game_mode(s), s->game_timed ? sol_seconds(s) : 0, score, has, s->game_timed,
                  ui_today(s));
    stats_changed(s);
}

/* A game left unfinished (a new deal, Exit) is lost. */
static void stats_abandon(SolSession *s)
{
    int has, score;
    if (!s->stats_on || !s->counted) return;
    s->counted = 0;
    if (!s->dealt) return;
    score = game_score(s, &has);
    sol_stats_lost(&s->stats, game_mode(s), score, has, s->game_timed, ui_today(s));
    stats_changed(s);
}

void sol_attach_stats(SolSession *s, const SolStats *st)
{
    if (st) s->stats = *st;
    else sol_stats_clear(&s->stats);
    s->stats_on = 1;
}

void sol_reset_stats(SolSession *s)
{
    sol_stats_clear(&s->stats);
    s->counted = 0;
    stats_changed(s);
}

void sol_abandon(SolSession *s)
{
    stats_abandon(s);
}

/* ---- Hint (extra) --------------------------------------------------------------------------------- */

/* Any input, command or change of position ends the flash. */
static void hint_stop(SolSession *s)
{
    if (!s->hint) return;
    s->hint = 0;
    ui_set_timer(s, SOL_TIMER_HINT, 0);
    ui_invalidate(s);
}

/* 2c: input other than Hint: the next Hint starts from the best move again. */
static void hint_uncycle(SolSession *s)
{
    s->hint_cyc = 0;
}

/* ---- Click selection (extra, click_select) --------------------------------------------------------- */

static void csel_clear(SolSession *s)
{
    if (s->csel_pile < 0) return;
    s->csel_pile = -1;
    s->csel_index = 0;
    ui_invalidate(s);
}

void sol_selection(const SolSession *s, int *pile, int *index)
{
    *pile = s->csel_pile;
    *index = s->csel_pile >= 0 ? s->csel_index : -1;
}

/* ---- The unwinnable warning (extra) ---------------------------------------------------------------- */

enum { SOL_AS_DEAL = 1, SOL_AS_MOVE = 2, SOL_AS_UNDO = 3 };

static void req_drop(SolSession *s)
{
    if (!s->req_pending) return;
    s->req_pending = 0;
    if (s->ui.solve_cancel) s->ui.solve_cancel(s->ui.ctx);
}

/* The position changed (cause SOL_AS_*): with the warning on, solve it in the background. */
static void assist_changed(SolSession *s, int cause)
{
    if (cause == SOL_AS_DEAL) {
        s->warned = 0;
        s->deal_lost = 0;
    }
    if (!s->extras.warn_unwinnable || !s->dealt || !s->ui.solve_start) {
        req_drop(s);
        return;
    }
    if (++s->req_id == 0) s->req_id = 1;
    s->req_pending = 1;
    s->req_cause = cause;
    s->ui.solve_start(s->ui.ctx, s->req_id, &s->board, s->draw,
                      sol_solve_recycles_left(s->game_scoring, s->draw, s->recycles));
}

int sol_solve_done(SolSession *s, uint32_t id, int status)
{
    /* busy, or cards being dragged (the message box would leave the drag hanging): later */
    if (s->busy || sol_dragging(s)) return 0;
    if (!s->req_pending || id != s->req_id) return 1;   /* superseded or cancelled */
    s->req_pending = 0;
    if (!s->extras.warn_unwinnable || !s->dealt) return 1;
    if (status == SOL_SOLVE_SOLVED) {
        s->warned = 0;
    } else if (status == SOL_SOLVE_UNSOLVABLE) {
        if (s->req_cause == SOL_AS_DEAL) s->deal_lost = 1;
        if (s->req_cause == SOL_AS_MOVE && !s->warned) {
            s->warned = 1;
            ui_message(s, s->deal_lost ? SOL_MSG_UNWINNABLE_DEAL : SOL_MSG_UNWINNABLE);
        }
    }                                   /* gave up: nothing is known */
    return 1;
}

/* ---- Win (§8) ------------------------------------------------------------------------------------ */

static void win(SolSession *s)
{
    s->bonus = score_event(s, SOL_EV_WIN);
    if (s->opts.scoring != SOL_SCORING_STANDARD) s->bonus = 0;
    stats_win(s);
    hint_stop(s);
    csel_clear(s);
    req_drop(s);
    clear_history(s);
    clear_drag(s);
    s->dealt = 0;                       /* the clock stops, board input is ignored */
    s->won = 1;
    update_timer(s);
    ui_status(s);
    s->busy++;
    if (s->ui.win_cascade) s->ui.win_cascade(s->ui.ctx);
    s->visible = 0;                     /* XP erases the table; a repaint draws nothing until Deal */
    ui_invalidate(s);
    int yes = s->ui.deal_again ? s->ui.deal_again(s->ui.ctx) : 0;
    s->busy--;
    if (yes) {
        if (s->ui.post_command) s->ui.post_command(s->ui.ctx, SOL_CMD_DEAL);
        else sol_new_deal(s, 0);
    }
}

/* IsWinner after drops, double-clicks home and autoplayed cards (never after stock clicks or Undo). */
static int check_win(SolSession *s)
{
    if (!sol_is_won(&s->board)) return 0;
    win(s);
    return 1;
}

static int do_finish(SolSession *s);

/* ---- Dead ends (2c): no more useful moves --------------------------------------------------------- */

static void nm_reset(SolSession *s)
{
    s->nm_mark = -1;
    s->nm_recycled = 0;
    s->nm_done = 0;
    s->nm_shown = 0;
}

/* The position after an action (kind NM_*), an Undo, a Redo or a deal (NM_RESET: the tracking starts
 * over here). Fair: only sol_useful_move (the visible cards) and the stock's size are looked at. */
static void nm_update(SolSession *s, int kind)
{
    int stock = s->board.p[SOL_STOCK].n;
    if (kind == NM_RESET) nm_reset(s);
    if (!s->dealt) return;
    if (sol_useful_move(&s->board)) {
        nm_reset(s);
        return;
    }
    if (stock == 0 && !recycle_allowed(s)) {
        s->nm_done = 1;                 /* the stock is used up */
        return;
    }
    if (s->nm_mark < 0) {               /* the first position without a useful move */
        s->nm_mark = stock;
        s->nm_recycled = 0;
        return;
    }
    if (kind == NM_RECYCLE) s->nm_recycled = 1;
    if (s->nm_recycled && (stock == s->nm_mark || stock == 0))
        s->nm_done = 1;                 /* a whole cycle of the stock, nothing useful anywhere */
}

int sol_no_more_moves(const SolSession *s)
{
    return s->dealt && s->nm_done;
}

/* "End Game" (No More Moves): a loss, the game ends (the table stays as it is, frozen), "Deal Again?". */
static void end_game(SolSession *s)
{
    int yes;
    stats_abandon(s);
    hint_stop(s);
    csel_clear(s);
    req_drop(s);
    clear_history(s);
    clear_drag(s);
    s->dealt = 0;                       /* the clock stops, board input is ignored */
    update_timer(s);
    ui_status(s);
    ui_invalidate(s);
    s->busy++;
    yes = s->ui.deal_again ? s->ui.deal_again(s->ui.ctx) : 0;
    s->busy--;
    if (yes) {
        if (s->ui.post_command) s->ui.post_command(s->ui.ctx, SOL_CMD_DEAL);
        else sol_new_deal(s, 0);
    }
}

/* After a committed action (or Redo): (extra) Finish automatically, the dead-end tracking and (extra)
 * its question, the warning's check of the new position. Not after Undo. */
static void settle(SolSession *s)
{
    if (!s->dealt) return;              /* won */
    if (s->extras.auto_finish && sol_finish_ready(&s->board)) {
        do_finish(s);
        return;
    }
    nm_update(s, s->nm_kind);
    if (s->extras.no_more_moves && s->nm_done && !s->nm_shown) {
        s->nm_shown = 1;
        hint_stop(s);
        if (ui_choose(s, SOL_ASK_NO_MOVES, SOL_ANS_RETURN, 2) == SOL_ANS_END_GAME) {
            end_game(s);
            return;
        }
    }
    assist_changed(s, SOL_AS_MOVE);
}

/* KlondForceWin 0x1004FB6 (Alt+Shift+2): every pile cleared, clubs, diamonds, hearts, spades A..K in
 * foundations 1..4, nothing repainted, then the win (Vegas gets no +5 per card). */
static void force_win(SolSession *s)
{
    if (!s->dealt || s->busy) return;
    clear_drag(s);
    sol_board_clear(&s->board);
    for (int f = 0; f < 4; f++) {
        SolPile *p = &s->board.p[SOL_FOUND0 + f];
        for (int r = 0; r < 13; r++) p->c[r] = (SolCard)((r * 4 + f) | SOL_UP);
        p->n = 13;
    }
    s->waste_fan = 0;
    s->forced_win = 1;
    win(s);
}

/* ---- Init, deal ---------------------------------------------------------------------------------- */

void sol_init(SolSession *s, const SolSessionUI *ui, const CeStore *store)
{
    uint32_t v;
    memset(s, 0, sizeof *s);
    if (ui) s->ui = *ui;
    if (store) s->store = *store;
    clear_drag(s);
    s->csel_pile = -1;
    nm_reset(s);
    /* LoadOptions 0x1001504 */
    sol_options_unpack(&s->opts, ce_store_get(&s->store, SOL_REG_OPTIONS, SOL_OPTIONS_DEFAULT));
    if (s->store.get && s->store.get(s->store.ctx, SOL_REG_BACK, &v)) {
        s->back = sol_back_from_reg(v);
    } else {
        uint32_t x = ui_now(s) & 0xFFFF;               /* XP: srand((WORD)time(NULL)); rand() % 12 */
        s->back = sol_rand(&x) % SOL_NBACKS;           /* uniform (XP's +53 clamp skewed it) */
    }
    v = ce_store_get(&s->store, SOL_REG_CURRENCY, 0);
    s->currency = v <= 3 ? (int)v : 0;
    s->draw = s->opts.draw;
}

void sol_free(SolSession *s)
{
    req_drop(s);
    free(s->hist);
    free(s->redo);
    s->hist = s->redo = NULL;
    s->nhist = s->hist_cap = s->nredo = s->redo_cap = 0;
}

static void set_col(SolSession *s, int p);

/* next_game_options: the Options that waited for this deal. Returns 1 if Draw, Timed or Scoring
 * changed (the deal is then an Options redeal: a Vegas Cumulative score starts over). */
static int apply_pending(SolSession *s)
{
    SolOptions o = s->pend_opts;
    int changed;
    if (!s->pending) return 0;
    s->pending = 0;
    changed = o.draw != s->opts.draw || o.timed != s->opts.timed || o.scoring != s->opts.scoring;
    s->opts = o;
    return changed;
}

static void deal(SolSession *s, unsigned seed, int from_options);

void sol_deal(SolSession *s, unsigned seed, int from_options)
{
    if (apply_pending(s)) from_options = 1;
    deal(s, seed, from_options);
}

/* The deal itself, with the current s->opts (sol_restart: the game's own, pending ones wait). */
static void deal(SolSession *s, unsigned seed, int from_options)
{
    stats_abandon(s);                   /* extra: an unfinished game that counted is lost */
    hint_stop(s);
    csel_clear(s);
    clear_drag(s);
    seed &= 0x7FFF;
    s->seed = seed;
    s->seeded = 1;
    sol_deal_board(&s->board, seed, &s->rng);
    s->deals++;                         /* (the view's: a new deal can fly in, 2d) */
    clear_history(s);
    /* Init 0x10028A0: the score survives only for Vegas + Cumulative, and not an Options redeal */
    if (!(s->opts.scoring == SOL_SCORING_VEGAS && s->opts.cumulative && !from_options)) s->score = 0;
    s->ticks = 0;
    s->recycles = 0;
    s->clock_pen = 0;
    s->undo_fresh = 0;
    s->input = 0;
    s->won = 0;
    s->forced_win = 0;
    s->bonus = 0;
    s->draw = s->opts.draw == 1 ? 1 : 3;
    s->waste_fan = 0;
    s->dealt = 1;
    s->visible = 1;
    s->game_scoring = s->opts.scoring;
    s->game_timed = s->opts.timed;
    s->carry = s->opts.scoring == SOL_SCORING_VEGAS ? s->score : 0;
    s->counted = 0;
    s->hint_cyc = 0;
    nm_reset(s);
    score_event(s, SOL_EV_DEAL);        /* Vegas -52 */
    set_col(s, SOL_STOCK);              /* the keyboard cursor on the stock's top card */
    update_timer(s);
    ui_invalidate(s);
    ui_status(s);
    assist_changed(s, SOL_AS_DEAL);
    nm_update(s, NM_RESET);
}

void sol_restored(SolSession *s)
{
    hint_stop(s);
    s->hint_cyc = 0;
    csel_clear(s);
    clear_drag(s);
    s->seeded = 1;
    s->dealt = 1;
    s->visible = 1;
    s->input = 0;                       /* the clock waits for the first press, as after a deal */
    s->won = 0;
    s->forced_win = 0;
    s->bonus = 0;
    s->warned = 0;
    s->deal_lost = 0;
    set_col(s, SOL_STOCK);
    update_timer(s);
    ui_invalidate(s);
    ui_status(s);
    assist_changed(s, SOL_AS_UNDO);     /* the warning's check, silent */
    nm_update(s, NM_RESET);
}

void sol_new_deal(SolSession *s, int from_options)
{
    unsigned seed;
    if (apply_pending(s)) from_options = 1;   /* 2c: before the winnable table is consulted */
    seed = ui_now(s) & 0x7FFF;
    if (s->extras.winnable_only) {
        /* extra: the first seed from here on that the table proves winnable for this draw and pass
         * limit (never the last deal again) */
        int sc = sol_seeds_case(s->opts.draw == 1 ? 1 : 3, s->opts.scoring);
        seed = sol_seed_next_winnable(sc, seed);
        if (s->seeded && seed == s->seed) seed = sol_seed_next_winnable(sc, seed + 1);
        sol_deal(s, seed, from_options);
        return;
    }
    if (s->seeded && seed == s->seed) seed = (seed + 1) & 0x7FFF;
    sol_deal(s, seed, from_options);
}

/* ---- Mouse --------------------------------------------------------------------------------------- */

static int stock_press(SolSession *s, int mods)
{
    int home;
    if (s->board.p[SOL_STOCK].n == 0) {
        if (!recycle_allowed(s)) return SOL_PRESS_NONE;
        begin_action(s, SOL_ACT_RECYCLE);
        do_recycle(s);
        auto_turn(s);
        home = auto_home(s);
        commit_action(s, 0);
        ui_invalidate(s);
        if (!(home && check_win(s))) settle(s);   /* (XP never wins on a stock click; auto_home can) */
        return SOL_PRESS_DONE;
    }
    /* StockHitTest 0x1004625: Ctrl+Alt+Shift draws a single card */
    int n = (mods & SOL_MOD_CHEAT) == SOL_MOD_CHEAT ? 1 : s->draw;
    begin_action(s, SOL_ACT_DRAW);
    s->work.n = (uint8_t)do_draw(s, n);
    auto_turn(s);
    home = auto_home(s);
    commit_action(s, 0);
    ui_invalidate(s);
    if (!(home && check_win(s))) settle(s);
    return SOL_PRESS_DONE;
}

/* A move of the cards src[idx..top] onto dst, already checked: one action, then (extras) the turns and
 * the cards home that follow it, the win or settle. */
static void do_move(SolSession *s, int src, int idx, int dst)
{
    int n = s->board.p[src].n - idx;
    begin_action(s, SOL_ACT_MOVE);
    s->work.src = (uint8_t)src;
    s->work.dst = (uint8_t)dst;
    s->work.n = (uint8_t)n;
    move_cards(s, src, dst, n);
    auto_turn(s);
    auto_home(s);
    commit_action(s, 0);
    ui_invalidate(s);
    if (!check_win(s)) settle(s);
}

int sol_press(SolSession *s, int pile, int index, int mods)
{
    hint_uncycle(s);
    if (!s->busy) hint_stop(s);
    if (!s->dealt || s->busy || sol_dragging(s)) return SOL_PRESS_NONE;
    start_input(s);                     /* any press on the table starts the clock */
    s->csel_block = 0;
    if (s->csel_pile >= 0) {            /* extra (click_select): the press after a click selection */
        int sp = s->csel_pile, si = s->csel_index;
        csel_clear(s);
        if (pile == sp) {
            s->csel_block = 1;          /* on the selection's pile: only deselects (no new click selection) */
        } else if (pile >= 0 && pile < SOL_NPILES && si < s->board.p[sp].n &&
                   sol_can_drop(&s->board, pile, sp, si)) {
            do_move(s, sp, si, pile);   /* the destination */
            return SOL_PRESS_DONE;
        }                               /* else: deselected, and the press acts as usual */
    }
    if (pile == SOL_STOCK) return stock_press(s, mods);
    if (pile < 0 || pile >= SOL_NPILES) return SOL_PRESS_NONE;
    SolPile *p = &s->board.p[pile];
    /* TableauHitTest 0x10042EF: a press on a face-down top card turns it over */
    if (sol_is_tab(pile) && p->n > 0 && index == p->n - 1 && !sol_is_up(p->c[index])) {
        int home;
        begin_action(s, SOL_ACT_TURN);
        s->work.src = (uint8_t)pile;
        do_turn(s, pile);
        auto_turn(s);                   /* (others left face down, e.g. before the option was on) */
        home = auto_home(s);
        commit_action(s, 0);
        ui_invalidate(s);
        if (!(home && check_win(s))) settle(s);
        return SOL_PRESS_DONE;
    }
    return sol_begin_drag(s, pile, index) ? SOL_PRESS_DRAG : SOL_PRESS_NONE;
}

int sol_begin_drag(SolSession *s, int pile, int index)
{
    hint_uncycle(s);
    if (!s->busy) hint_stop(s);
    if (!s->dealt || s->busy || sol_dragging(s) || pile <= SOL_STOCK || pile >= SOL_NPILES) return 0;
    const SolPile *p = &s->board.p[pile];
    if (index < 0 || index >= p->n || !sol_is_up(p->c[index])) return 0;
    /* the waste gives only its top card; so does a foundation (XP picked up a run from its edge) */
    if ((pile == SOL_WASTE || sol_is_found(pile)) && index != p->n - 1) return 0;
    csel_clear(s);                      /* extra: a drag replaces a click selection */
    start_input(s);
    s->drag_pile = pile;
    s->drag_index = index;
    s->drag_count = p->n - index;
    s->drag_kbd = 0;
    s->target = -1;
    s->target_ui = 0;
    ui_invalidate(s);
    return 1;
}

int sol_can_drop_on(const SolSession *s, int pile)
{
    return sol_dragging(s) && sol_can_drop(&s->board, pile, s->drag_pile, s->drag_index);
}

int sol_drag_over(SolSession *s, int pile)
{
    if (!sol_dragging(s)) return -1;
    int t = pile >= 0 && sol_can_drop_on(s, pile) ? pile : -1;
    s->target_ui = 1;
    if (t != s->target) {
        s->target = t;
        if (s->opts.outline) ui_invalidate(s);   /* outline dragging shows the target inverted */
    }
    return t;
}

int sol_key_drop_target(const SolSession *s)
{
    int t;
    if (!sol_dragging(s)) return -1;
    t = s->target_ui ? s->target : s->kbd_pile;
    return t >= 0 && sol_can_drop_on(s, t) ? t : -1;
}

int sol_drop(SolSession *s, int pile)
{
    if (!sol_dragging(s)) return 0;
    int src = s->drag_pile, idx = s->drag_index, n = s->drag_count;
    clear_drag(s);
    /* checked again here: XP moved onto a stale target without a check (§11) */
    if (!s->dealt || s->busy || pile < 0 || !sol_can_drop(&s->board, pile, src, idx)) {
        ui_invalidate(s);
        return 0;
    }
    (void)n;
    do_move(s, src, idx, pile);
    return 1;
}

void sol_cancel_drag(SolSession *s)
{
    if (!sol_dragging(s)) return;
    clear_drag(s);                      /* no undo record, no stale target (§11) */
    ui_invalidate(s);
}

/* Extra (2d, "Enhanced animations"): the top card of src flies to dst before a move XP makes at once
 * (the double-click, the right button's autoplay); without the option nothing is called, as XP. */
static void enhanced_flight(SolSession *s, int src, int dst)
{
    if (!s->extras.enhanced_anim || !s->ui.animate_move) return;
    s->busy++;                          /* the flight: commands and input wait */
    s->ui.animate_move(s->ui.ctx, src, dst);
    s->busy--;
}

/* DblClkToFoundation 0x100438C: the top card of the waste or a tableau column, pressed on, to the
 * leftmost foundation that takes it; otherwise the click is an ordinary press. */
int sol_dblclick(SolSession *s, int pile, int index, int mods)
{
    hint_uncycle(s);
    if (!s->busy) hint_stop(s);
    if (!s->dealt || s->busy || sol_dragging(s)) return SOL_PRESS_NONE;
    if (pile == SOL_WASTE || sol_is_tab(pile)) {
        const SolPile *p = &s->board.p[pile];
        if (p->n > 0 && index == p->n - 1 && sol_is_up(p->c[index])) {
            for (int f = SOL_FOUND0; f < SOL_FOUND0 + 4; f++) {
                if (!sol_can_drop(&s->board, f, pile, index)) continue;
                start_input(s);
                enhanced_flight(s, pile, f);
                do_move(s, pile, index, f);
                return SOL_PRESS_DONE;
            }
        }
    }
    return sol_press(s, pile, index, mods);
}

/* Autoplay 0x1002A45 (§7.2): passes over the piles in index order, foundations skipped, each face-up
 * top card to the leftmost foundation that takes it, until a pass moves nothing. No safety rule. */
int sol_autoplay(SolSession *s)
{
    int total = 0, moved, won = 0;
    hint_uncycle(s);
    if (!s->busy) hint_stop(s);
    if (!s->dealt || s->busy || sol_dragging(s)) return 0;
    csel_clear(s);
    begin_action(s, SOL_ACT_AUTOPLAY);
    do {
        moved = 0;
        for (int c = 0; c < SOL_NPILES && !won; c++) {
            if (sol_is_found(c)) continue;
            const SolPile *p = &s->board.p[c];
            if (p->n == 0 || !sol_is_up(p->c[p->n - 1])) continue;
            for (int f = SOL_FOUND0; f < SOL_FOUND0 + 4; f++) {
                if (!sol_can_drop(&s->board, f, c, p->n - 1)) continue;
                s->work.steps[s->work.nsteps][0] = (uint8_t)c;
                s->work.steps[s->work.nsteps][1] = (uint8_t)f;
                s->work.nsteps++;
                enhanced_flight(s, c, f);
                move_cards(s, c, f, 1);
                moved++;
                total++;
                won = sol_is_won(&s->board);
                break;
            }
        }
    } while (moved && !won);
    if (!total) return 0;
    auto_turn(s);
    if (!won) {
        total += auto_home(s);          /* extra: what the turns uncovered */
        won = sol_is_won(&s->board);
    }
    commit_action(s, 0);
    ui_invalidate(s);
    if (won) win(s);
    else settle(s);
    return total;
}

/* ---- Undo / redo (extras: unlimited, see session.h) ---------------------------------------------- */

int sol_undo(SolSession *s)
{
    if (!sol_undo_enabled(s)) return 0;
    csel_clear(s);
    SolAction a = s->hist[--s->nhist];
    sol_board_unpack(&s->board, a.board);
    s->waste_fan = a.waste_fan;
    s->score = a.score;
    s->recycles = a.recycles;
    if (s->undo_fresh) {
        s->clock_pen = a.clock_pen;     /* XP: the score from before the move, the clock's penalties since refunded */
    } else {
        /* a deeper Undo: the clock's penalties since this action stay (Standard, floored at 0) */
        int keep = s->clock_pen - a.clock_pen;
        if (keep > 0) s->score = s->score > keep ? s->score - keep : 0;
    }
    s->undo_fresh = 0;
    score_event(s, SOL_EV_CLOCK);       /* XP's Undo charge: Standard -2 (floored at 0) */
    stack_push(&s->redo, &s->nredo, &s->redo_cap, &a);
    if (s->batch) return 1;             /* Undo All: told once, at the end */
    s->redo_group = 0;                  /* (an Undo All group is never under a single Undo) */
    ui_invalidate(s);
    ui_status(s);
    assist_changed(s, SOL_AS_UNDO);
    nm_update(s, NM_RESET);
    return 1;
}

int sol_undo_all(SolSession *s)
{
    int n = 0, r0;
    if (!sol_undo_enabled(s)) return 0;
    hint_stop(s);
    csel_clear(s);
    r0 = s->nredo;
    s->batch++;
    while (sol_undo(s)) n++;            /* as that many Undos: the same score and clock penalties */
    s->batch--;
    s->redo_group = s->nredo - r0;      /* (fewer if the redo stack ran out of memory) */
    ui_invalidate(s);
    ui_status(s);
    assist_changed(s, SOL_AS_UNDO);
    nm_update(s, NM_RESET);
    return n;
}

/* Is action a performable on the current position? (It always is right after its Undo.) */
static int replay_ok(const SolSession *s, const SolAction *a)
{
    const SolBoard *b = &s->board;
    switch (a->type) {
    case SOL_ACT_MOVE:
        return a->src < SOL_NPILES && a->n >= 1 && a->n <= b->p[a->src].n &&
               sol_is_up(b->p[a->src].c[b->p[a->src].n - a->n]) &&
               sol_can_drop(b, a->dst, a->src, b->p[a->src].n - a->n);
    case SOL_ACT_DRAW:
        return a->n >= 1 && a->n <= b->p[SOL_STOCK].n;
    case SOL_ACT_RECYCLE:
        return recycle_allowed(s);
    case SOL_ACT_TURN:
        return sol_is_tab(a->src) && b->p[a->src].n > 0 && !sol_is_up(b->p[a->src].c[b->p[a->src].n - 1]);
    case SOL_ACT_AUTOPLAY:
    case SOL_ACT_FINISH: {
        SolBoard t = *b;
        if (!a->nsteps || a->nsteps > 52) return 0;
        for (int i = 0; i < a->nsteps; i++) {
            int src = a->steps[i][0], dst = a->steps[i][1];
            if (src >= SOL_NPILES || !t.p[src].n || !sol_is_up(t.p[src].c[t.p[src].n - 1]) ||
                !sol_can_drop(&t, dst, src, t.p[src].n - 1)) return 0;
            t.p[dst].c[t.p[dst].n++] = t.p[src].c[--t.p[src].n];
        }
        return 1;
    }
    }
    return 0;
}

/* Redo the top action of the redo stack; last = 0 inside an Undo All group (the UI is told, the win
 * checked and the position settled after the group's last action only). */
static int redo_one(SolSession *s, int last)
{
    SolAction a = s->redo[s->nredo - 1];
    if (!replay_ok(s, &a)) {            /* cannot happen; never replay onto a wrong position */
        s->nredo = 0;
        s->redo_group = 0;
        return 0;
    }
    s->nredo--;
    begin_action(s, a.type);
    s->work.src = a.src;
    s->work.dst = a.dst;
    s->work.n = a.n;
    s->work.nsteps = a.nsteps;
    memcpy(s->work.steps, a.steps, sizeof a.steps);
    switch (a.type) {
    case SOL_ACT_MOVE:    move_cards(s, a.src, a.dst, a.n); break;
    case SOL_ACT_DRAW:    do_draw(s, a.n); break;
    case SOL_ACT_RECYCLE: do_recycle(s); break;
    case SOL_ACT_TURN:    do_turn(s, a.src); break;
    case SOL_ACT_AUTOPLAY:
    case SOL_ACT_FINISH:
        for (int i = 0; i < a.nsteps; i++) move_cards(s, a.steps[i][0], a.steps[i][1], 1);
        break;
    }
    replay_turns(s, a.autoturn);
    replay_autos(s, &a);
    commit_action(s, 1);
    if (!last) return 1;
    ui_invalidate(s);
    if (!((a.type == SOL_ACT_MOVE || a.type == SOL_ACT_AUTOPLAY || a.type == SOL_ACT_FINISH || a.nauto) &&
          check_win(s)))
        settle(s);
    return 1;
}

int sol_redo(SolSession *s)
{
    if (!sol_redo_enabled(s)) return 0;
    csel_clear(s);
    if (s->redo_group > 0 && s->redo_group <= s->nredo) {
        /* extra: what Undo All undid comes back whole */
        int n = s->redo_group, done = 0;
        s->redo_group = 0;
        while (done < n && redo_one(s, done == n - 1)) done++;
        if (done > 0 && done < n) {     /* (cannot happen: a stale group) */
            ui_invalidate(s);
            settle(s);
        }
        return done > 0;
    }
    s->redo_group = 0;
    return redo_one(s, 1);
}

/* ---- Keyboard (§9.1, KeyHit 0x1002EB8) ----------------------------------------------------------- */

/* CanFocus: the stock and the waste cannot take the cursor while something is being dragged. */
static int can_focus(int p, int dragging)
{
    return (p == SOL_STOCK || p == SOL_WASTE) ? !dragging : 1;
}

/* CanFocusCard: stock and waste only their top card; foundations too (XP let the cursor reach the
 * lower cards, the keyboard path of the foundation pick-up bug); tableau any card. */
static int can_focus_card(const SolSession *s, int p, int i)
{
    int n = s->board.p[p].n;
    return sol_is_tab(p) || n == 0 || i == n - 1;
}

/* 0x1002D93: the cursor to pile p, on its top card. */
static void set_col(SolSession *s, int p)
{
    if (!can_focus(p, sol_dragging(s))) return;
    s->kbd_pile = p;
    s->kbd_card = s->board.p[p].n > 0 ? s->board.p[p].n - 1 : 0;
}

/* 0x1002DDA: the next focusable pile in direction delta (wrapping); diff_class skips piles of the
 * cursor's own class. delta 0 re-targets the current pile. */
static void move_col(SolSession *s, int delta, int diff_class)
{
    int start = s->kbd_pile < 0 ? 0 : s->kbd_pile, c = start;
    if (delta) {
        for (;;) {
            c += delta;
            if (c < 0) c = SOL_NPILES - 1;
            else if (c >= SOL_NPILES) c = 0;
            if (c == start) break;
            if (!can_focus(c, sol_dragging(s))) continue;
            if (diff_class && sol_pile_class(c) == sol_pile_class(start)) continue;
            break;
        }
    }
    set_col(s, c);
}

/* 0x1002E4B: the card cursor within the face-up run on top of the pile, clamped. */
static void move_card(SolSession *s, int delta)
{
    int p = s->kbd_pile, n = s->board.p[p].n, up = sol_up_run(&s->board, p), i;
    if (n == 0) {
        i = 0;
    } else if (up == 0) {
        i = n - 1;
    } else {
        i = s->kbd_card + delta;
        if (i < n - up) i = n - up;
        if (i > n - 1) i = n - 1;
    }
    if (can_focus_card(s, p, i)) s->kbd_card = i;
    else s->kbd_card = n - 1;           /* a stale cursor below a top-only pile's top: the top */
}

int sol_key(SolSession *s, int key, int mods)
{
    int cyc = s->hint_cyc;
    if (s->busy) return 0;
    hint_stop(s);
    hint_uncycle(s);
    csel_clear(s);                      /* extra: the keyboard plays as in XP */
    switch (key) {
    case SOL_KEY_HOME:  set_col(s, SOL_STOCK); break;
    case SOL_KEY_END:   set_col(s, SOL_NPILES - 1); break;
    case SOL_KEY_TAB:   move_col(s, (mods & SOL_MOD_SHIFT) ? -1 : 1, 1); break;
    case SOL_KEY_LEFT:  move_col(s, -1, (mods & SOL_MOD_SHIFT) != 0); break;
    case SOL_KEY_RIGHT: move_col(s, 1, (mods & SOL_MOD_SHIFT) != 0); break;
    case SOL_KEY_UP:    move_card(s, -1); break;
    case SOL_KEY_DOWN:  move_card(s, 1); break;
    case SOL_KEY_RETURN:
    case SOL_KEY_SPACE:
        if (sol_dragging(s)) {
            /* XP's KeyHit: MouseUp, a drop on the highlighted target (where the pointer put the cards) */
            sol_drop(s, sol_key_drop_target(s));
            move_col(s, 0, 0);
            return 1;
        } else {
            move_card(s, 0);
            int n = s->board.p[s->kbd_pile].n;
            int r = sol_press(s, s->kbd_pile, n ? s->kbd_card : -1, mods);   /* as a mouse press */
            if (r == SOL_PRESS_NONE) return 1;
            if (r == SOL_PRESS_DRAG) s->drag_kbd = 1;
            move_col(s, 0, 0);
        }
        break;
    case SOL_KEY_ESCAPE:
        sol_cancel_drag(s);
        return 1;
    case SOL_KEY_A:
        if (mods & SOL_MOD_CTRL) sol_autoplay(s);
        return 1;
    default:
        s->hint_cyc = cyc;              /* 2c: a key the game does not use (H's own key-down) */
        return 0;
    }
    ui_kbd_cursor(s);
    return 1;
}

/* ---- Timer, window, commands, options ------------------------------------------------------------ */

void sol_timer(SolSession *s, int id)
{
    if (id == SOL_TIMER_HINT) {         /* extra: the hint's flash */
        if (!s->hint) {
            ui_set_timer(s, id, 0);
            return;
        }
        if (++s->hint_step >= 8) hint_stop(s);
        else ui_invalidate(s);
        return;
    }
    if (id != SOL_TIMER_CLOCK) return;
    if (!clock_should_run(s)) {
        update_timer(s);
        return;
    }
    if (s->ticks < SOL_TICKS_MAX) s->ticks++;
    if (s->ticks % 40 == 0) s->clock_pen -= score_event(s, SOL_EV_CLOCK);   /* Standard: -2 every 10 s */
    ui_status(s);
}

void sol_set_minimized(SolSession *s, int minimized)
{
    s->minimized = minimized != 0;
    update_timer(s);
}

void sol_command(SolSession *s, int cmd)
{
    if (s->busy) return;
    hint_stop(s);
    if (cmd != SOL_CMD_HINT) hint_uncycle(s);
    if (cmd != SOL_CMD_DRAW) csel_clear(s);
    switch (cmd) {
    case SOL_CMD_DEAL:
        if (sol_dragging(s)) break;                 /* XP grays Deal while a card is dragged */
        if (s->extras.save_game && s->extras.ask_save_game && sol_game_started(s)) {
            /* 2c: the Windows 7 question ("Save game on exit" with "Ask before saving or resuming") */
            int r = ui_choose(s, SOL_ASK_NEW_GAME, SOL_ANS_QUIT_NEW, 3);
            if (r == SOL_ANS_KEEP) break;
            if (r == SOL_ANS_RESTART) {
                sol_restart(s);
                break;
            }
        }
        sol_new_deal(s, 0);
        break;
    case SOL_CMD_UNDO:     sol_undo(s); break;
    case SOL_CMD_UNDO_ALL:                          /* extra: asks first */
        if (sol_undo_enabled(s) && ui_confirm(s, SOL_MSG_UNDO_ALL)) sol_undo_all(s);
        break;
    case SOL_CMD_DRAW:                              /* extra (D): a press on the stock */
        if (s->dealt && !sol_dragging(s)) {
            int n = s->board.p[SOL_STOCK].n;
            sol_press(s, SOL_STOCK, n ? n - 1 : -1, 0);
        }
        break;
    case SOL_CMD_REDO:     sol_redo(s); break;
    case SOL_CMD_FORCEWIN: force_win(s); break;
    case SOL_CMD_HINT:     sol_hint(s); break;
    case SOL_CMD_FINISH:   sol_finish(s); break;
    default: break;
    }
}

int sol_apply_options(SolSession *s, const SolOptions *in)
{
    SolOptions o = *in;
    hint_stop(s);
    hint_uncycle(s);
    csel_clear(s);
    o.status_bar = o.status_bar != 0;
    o.timed = o.timed != 0;
    o.outline = o.outline != 0;
    o.draw = o.draw == 1 ? 1 : 3;
    if (o.scoring != SOL_SCORING_VEGAS && o.scoring != SOL_SCORING_NONE) o.scoring = SOL_SCORING_STANDARD;
    o.cumulative = o.cumulative != 0;
    int redeal = o.draw != s->opts.draw || o.timed != s->opts.timed || o.scoring != s->opts.scoring;
    if (redeal && sol_game_started(s)) {
        /* 2c: the game in progress may go on with its own settings; the new ones wait for the next deal
         * (asked once: the same change again keeps waiting - also once the option is off again: the
         * dialog showed the waiting settings, and an OK that leaves them alone redeals nothing) */
        int same = s->pending && o.draw == s->pend_opts.draw && o.timed == s->pend_opts.timed &&
                   o.scoring == s->pend_opts.scoring;
        if (same || (s->extras.next_game_options &&
                     ui_choose(s, SOL_ASK_SETTINGS, SOL_ANS_PLAY_NEW, 2) == SOL_ANS_FINISH)) {
            s->pend_opts = o;
            s->pending = 1;
            s->opts.status_bar = o.status_bar;      /* these apply at once */
            s->opts.outline = o.outline;
            s->opts.cumulative = o.cumulative;
            ce_store_set(&s->store, SOL_REG_OPTIONS, sol_options_pack(&o));
            update_timer(s);
            ui_status(s);
            ui_invalidate(s);
            return 0;
        }
    }
    s->pending = 0;
    s->opts = o;
    ce_store_set(&s->store, SOL_REG_OPTIONS, sol_options_pack(&o));   /* SaveOptions(1) at OK */
    if (redeal) {
        clear_drag(s);
        sol_new_deal(s, 1);
    } else {
        update_timer(s);
        ui_status(s);
        ui_invalidate(s);
    }
    return redeal;
}

const SolOptions *sol_dialog_options(const SolSession *s)
{
    return s->pending ? &s->pend_opts : &s->opts;
}

void sol_set_back(SolSession *s, int back)
{
    if (back < 0) back = 0;
    if (back >= SOL_NBACKS) back = SOL_NBACKS - 1;
    if (back != s->back) {
        s->back = back;
        ui_invalidate(s);
    }
    ce_store_set(&s->store, SOL_REG_BACK, sol_back_to_reg(back));   /* SaveOptions(4) at OK */
}

/* ---- Extras: settings, Hint, Finish, click-to-move ---------------------------------------------- */

#define NEXTRAS 13
static const char *const extra_names[NEXTRAS] = { "AutoTurn", "ClickToMove", "AutoFinish", "WinnableOnly",
                                                  "SaveGame", "WarnUnwinnable", "AutoHome", "ClickSelect",
                                                  "NoMoreMoves", "NextGameOptions", "EnhancedAnimations",
                                                  "AskSaveGame", "LargePrint" };

static int *extra_field(SolExtras *x, int i)
{
    switch (i) {
    case 0: return &x->auto_turn;
    case 1: return &x->click_move;
    case 2: return &x->auto_finish;
    case 3: return &x->winnable_only;
    case 4: return &x->save_game;
    case 5: return &x->warn_unwinnable;
    case 6: return &x->auto_home;
    case 7: return &x->click_select;
    case 8: return &x->no_more_moves;
    case 9: return &x->next_game_options;
    case 10: return &x->enhanced_anim;
    case 11: return &x->ask_save_game;
    default: return &x->large_print;
    }
}

void sol_extras_load(SolExtras *x, const CeStore *store)
{
    memset(x, 0, sizeof *x);
    for (int i = 0; i < NEXTRAS; i++) *extra_field(x, i) = ce_store_get(store, extra_names[i], 0) != 0;
}

void sol_extras_save(const SolExtras *x, const CeStore *store)
{
    SolExtras t = *x;
    for (int i = 0; i < NEXTRAS; i++) ce_store_set(store, extra_names[i], *extra_field(&t, i) ? 1u : 0u);
    ce_store_flush(store);
}

void sol_set_extras(SolSession *s, const SolExtras *x)
{
    int warn_was = s->extras.warn_unwinnable;
    SolExtras t = *x;
    for (int i = 0; i < NEXTRAS; i++) *extra_field(&t, i) = *extra_field(&t, i) != 0;
    s->extras = t;
    if (!t.click_select) csel_clear(s);
    if (warn_was && !t.warn_unwinnable) req_drop(s);
    else if (!warn_was && t.warn_unwinnable && s->dealt && !s->busy)
        assist_changed(s, s->nhist == 0 && s->nredo == 0 ? SOL_AS_DEAL : SOL_AS_UNDO);   /* silent */
}

void sol_hint(SolSession *s)
{
    SolHintMove list[SOL_HINT_MAX], m;
    uint8_t pos[SOL_PACKED_SIZE];
    const SolBoard *b = &s->board;
    int n, k = 0;
    if (!sol_hint_enabled(s)) return;
    hint_stop(s);
    n = sol_hint_list(b, recycle_allowed(s), list, SOL_HINT_MAX);
    if (n == 0) {
        s->hint_cyc = 0;
        ui_message(s, SOL_MSG_NO_HINT);
        return;
    }
    if (s->nm_done && list[0].cls >= SOL_HC_DRAW) {
        s->hint_cyc = 0;                /* 2c: a whole stock cycle (or the used-up stock) gave nothing */
        ui_message(s, SOL_MSG_NO_USEFUL);
        return;
    }
    sol_board_pack(b, pos);
    if (s->hint_cyc && s->hint_rec == s->recycles && !memcmp(pos, s->hint_pos, sizeof pos))
        k = (s->hint_idx + 1) % n;      /* 2c: Hint again: the next move, wrapping */
    s->hint_cyc = 1;
    s->hint_idx = k;
    s->hint_rec = s->recycles;
    memcpy(s->hint_pos, pos, sizeof pos);
    m = list[k];
    s->hint_move = m;
    s->hint_src_pile = m.src;
    s->hint_src_card = m.index >= 0 ? m.index : 0;
    s->hint_dst_pile = m.dst;
    s->hint_dst_card = b->p[m.dst].n > 0 ? b->p[m.dst].n - 1 : 0;
    if (m.kind == SOL_HINT_TURN) s->hint_dst_card = m.index;   /* the card twice over */
    s->hint = 1;
    s->hint_step = 0;
    ui_set_timer(s, SOL_TIMER_HINT, SOL_HINT_STEP_MS);
    ui_invalidate(s);
}

void sol_hint_view(const SolSession *s, int *pile, int *card)
{
    *pile = *card = -1;
    if (!s->hint || (s->hint_step & 1)) return;
    if (s->hint_step < 4) {
        *pile = s->hint_src_pile;
        *card = s->hint_src_card;
    } else {
        *pile = s->hint_dst_pile;
        *card = s->hint_dst_card;
    }
}

/* Finish (assist.h): every card home, lowest first, as one action, each card flown by the UI. */
static int do_finish(SolSession *s)
{
    int src, dst, moved = 0;
    if (!sol_finish_ready(&s->board)) return 0;
    hint_stop(s);
    begin_action(s, SOL_ACT_FINISH);
    s->busy++;                          /* the flights: commands and input wait */
    while (s->work.nsteps < 52 && sol_finish_step(&s->board, &src, &dst)) {
        if (s->ui.animate_move) s->ui.animate_move(s->ui.ctx, src, dst);
        s->work.steps[s->work.nsteps][0] = (uint8_t)src;
        s->work.steps[s->work.nsteps][1] = (uint8_t)dst;
        s->work.nsteps++;
        move_cards(s, src, dst, 1);
        moved++;
    }
    s->busy--;
    commit_action(s, 0);
    ui_invalidate(s);
    check_win(s);                       /* always: the finish ends in the win */
    return moved;
}

int sol_finish(SolSession *s)
{
    if (!sol_finish_enabled(s)) return 0;
    start_input(s);
    return do_finish(s);
}

int sol_click_target(const SolSession *s)
{
    if (!s->extras.click_move || !sol_dragging(s) || !s->dealt || s->busy) return -1;
    return sol_click_dest(&s->board, s->drag_pile, s->drag_index);
}

int sol_click(SolSession *s)
{
    int t;
    hint_uncycle(s);
    if (!sol_dragging(s) || !s->dealt || s->busy) return SOL_CLICK_NONE;
    if (s->csel_block) {                /* the press only deselected */
        s->csel_block = 0;
        return SOL_CLICK_NONE;
    }
    if ((t = sol_click_target(s)) >= 0)
        return sol_drop(s, t) ? SOL_CLICK_MOVED : SOL_CLICK_NONE;
    if (!s->extras.click_select) return SOL_CLICK_NONE;
    s->csel_pile = s->drag_pile;        /* the drag's cards, picked up by the usual rules */
    s->csel_index = s->drag_index;
    clear_drag(s);
    ui_invalidate(s);
    return SOL_CLICK_SELECTED;
}

/* ---- 2c: the Windows 7 prompts ------------------------------------------------------------------ */

int sol_game_started(const SolSession *s)
{
    return s->dealt && (s->counted || s->nhist > 0 || s->nredo > 0);
}

void sol_restart(SolSession *s)
{
    int counted = s->counted;
    if (!s->dealt || s->busy || sol_dragging(s)) return;
    if (s->game_scoring == SOL_SCORING_VEGAS) s->score = s->carry;   /* this game's money back */
    s->counted = 0;                     /* not a loss: the same game goes on */
    deal(s, s->seed, 0);                /* its own settings (pending ones wait on) */
    s->counted = counted;
}

int sol_exit_choice(SolSession *s)
{
    if (!s->extras.save_game) return SOL_ANS_EXIT_NOSAVE;
    if (!s->extras.ask_save_game || !sol_game_started(s) || s->busy) return SOL_ANS_EXIT_SAVE;   /* silent */
    hint_stop(s);
    return ui_choose(s, SOL_ASK_EXIT, SOL_ANS_EXIT_SAVE, 3);
}

int sol_offer_resume(SolSession *s)
{
    if (!s->extras.save_game || !s->extras.ask_save_game || !sol_game_started(s) || s->busy) return 1;
    if (ui_choose(s, SOL_ASK_RESUME, SOL_ANS_CONTINUE, 2) == SOL_ANS_CONTINUE) return 1;
    sol_new_deal(s, 0);                 /* the saved game is lost */
    return 0;
}

/* ---- Queries ------------------------------------------------------------------------------------- */

int sol_idle(const SolSession *s)
{
    return !sol_dragging(s);
}

int sol_hint_enabled(const SolSession *s)
{
    return s->dealt && !s->busy && !sol_dragging(s);
}

int sol_finish_enabled(const SolSession *s)
{
    return s->dealt && !s->busy && !sol_dragging(s) && sol_finish_ready(&s->board);
}

int sol_undo_enabled(const SolSession *s)
{
    return s->nhist > 0 && s->dealt && !s->busy && !sol_dragging(s);
}

int sol_redo_enabled(const SolSession *s)
{
    return s->nredo > 0 && s->dealt && !s->busy && !sol_dragging(s);
}

int sol_board_visible(const SolSession *s)
{
    return s->visible;
}

int sol_stock_symbol(const SolSession *s)
{
    if (s->board.p[SOL_STOCK].n) return SOL_STOCK_CARDS;
    return s->opts.scoring == SOL_SCORING_VEGAS && s->recycles >= s->draw - 1 ? SOL_STOCK_X : SOL_STOCK_O;
}

int sol_seconds(const SolSession *s)
{
    return s->ticks >> 2;
}

int sol_clock_running(const SolSession *s)
{
    return clock_should_run(s);
}

int sol_waste_fan(const SolSession *s)
{
    int n = s->board.p[SOL_WASTE].n;
    return s->waste_fan < n ? s->waste_fan : n;
}

int sol_waste_fan_start(const SolSession *s)
{
    return s->board.p[SOL_WASTE].n - sol_waste_fan(s);
}

void sol_kbd_cursor(const SolSession *s, int *pile, int *card)
{
    int p = s->kbd_pile >= 0 && s->kbd_pile < SOL_NPILES ? s->kbd_pile : 0;
    int n = s->board.p[p].n, c = s->kbd_card;
    if (c > n - 1) c = n - 1;
    if (c < 0) c = 0;
    *pile = p;
    *card = c;
}

void sol_score_text(const SolSession *s, char *buf, size_t n)
{
    if (!n) return;
    if (s->opts.scoring == SOL_SCORING_NONE) { buf[0] = 0; return; }
    sol_format_score(s->score, s->opts.scoring == SOL_SCORING_VEGAS, s->currency, buf, n);
}

void sol_win_text(const SolSession *s, char *buf, size_t n)
{
    static const char stop[] = "Press Esc or a mouse button to stop...";
    if (!n) return;
    if (s->opts.scoring == SOL_SCORING_STANDARD) snprintf(buf, n, "Bonus: %d  %s", s->bonus, stop);
    else snprintf(buf, n, "%s", stop);
}
