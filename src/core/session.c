/*
 * FreeCell HD — the XP FreeCell controller. Section numbers refer to docs/xp-reference/rules.md.
 */
#include "session.h"
#include "assist.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Texts ------------------------------------------------------------------------------------- */

const char *fcs_string(int id)
{
    switch (id) {
    case 301: case 318: return "FreeCell";
    case 302: return "by Jim Horne";
    case 303: return "FreeCell Game #%d";
    case 304: return "Out of memory.  Close other applications and try again.";
    case 305: return "Do you want to resign this game?";
    case 306: return "That move is not allowed.";
    case 307: return "That move requires moving %u cards. You only have enough free space to move %u.";
    case 308: return "Cards Left: %u";
    case 309: return "Are you sure you want to delete all statistics?";
    case 310: return "1 win";
    case 311: return "1 loss";
    case 312: return "%u wins";
    case 313: return "%u losses";
    case FCS_STR_YOULOSE: return "Sorry, you lose. There are no more legal moves.\nDo you want to play again?";
    case FCS_STR_YOUWIN: return "Congratulations, you win!\n \nDo you want to play again?";
    case FCS_STR_CHEAT_CAPTION: return "User-Friendly User Interface";
    case FCS_STR_CHEAT_TEXT: return "Choose Abort to Win,\nRetry to Lose,\nor Ignore to Cancel.";
    case FCS_STR_HINT_NONE: return "No hint is available.";
    case FCS_STR_HINT_LOST: return "There are no more winning moves.";
    case FCS_STR_UNWINNABLE: return "This game can no longer be won. Use Undo to go back.";
    case FCS_STR_UNWINNABLE_DEAL: return "This game cannot be won.";
    }
    return "";
}

/* ---- Small helpers ------------------------------------------------------------------------------ */

#define UI(s, f) ((s)->ui.f)

static void invalidate(FcSession *s) { if (UI(s, invalidate)) UI(s, invalidate)(UI(s, ctx)); }

static void update_menu(FcSession *s)
{
    if (UI(s, menu_state))
        UI(s, menu_state)(UI(s, ctx), fcs_undo_enabled(s), fcs_restart_enabled(s), fcs_redo_enabled(s));
    if (UI(s, assist_menu))
        UI(s, assist_menu)(UI(s, ctx), fcs_hint_enabled(s), fcs_finish_enabled(s));
}

static void cards_left_changed(FcSession *s)
{
    if (UI(s, cards_left_changed)) UI(s, cards_left_changed)(UI(s, ctx), s->board.cards_left);
}

static void set_timer(FcSession *s, int id, int ms) { if (UI(s, set_timer)) UI(s, set_timer)(UI(s, ctx), id, ms); }

/* ---- Move counter and game clock (extras) ------------------------------------------------------- */

static void status_changed(FcSession *s) { if (UI(s, status_changed)) UI(s, status_changed)(UI(s, ctx)); }

static uint32_t clock_now(const FcSession *s) { return s->ui.now_ms ? s->ui.now_ms(s->ui.ctx) : 0; }

static void clock_start(FcSession *s)            /* the first counted move after a deal */
{
    if (s->clock_running || !s->in_progress) return;
    s->clock_start = clock_now(s) - s->clock_ms;
    s->clock_running = 1;
}

static void clock_stop(FcSession *s)             /* win / loss: the time stays shown */
{
    if (!s->clock_running) return;
    s->clock_ms = clock_now(s) - s->clock_start;
    s->clock_running = 0;
}

static void clock_reset(FcSession *s)            /* a deal */
{
    s->clock_running = 0;
    s->clock_ms = 0;
    s->moves = 0;
}

int fcs_moves(const FcSession *s) { return s->moves; }
int fcs_clock_running(const FcSession *s) { return s->clock_running; }
uint32_t fcs_elapsed_ms(const FcSession *s) { return s->clock_running ? clock_now(s) - s->clock_start : s->clock_ms; }

void fcs_format_time(uint32_t ms, char *buf, size_t n)
{
    uint32_t t = ms / 1000u;
    if (t >= 3600u) snprintf(buf, n, "%u:%02u:%02u", (unsigned)(t / 3600u), (unsigned)(t / 60u % 60u), (unsigned)(t % 60u));
    else snprintf(buf, n, "%u:%02u", (unsigned)(t / 60u), (unsigned)(t % 60u));
}

/* ---- Won deals (extra) ------------------------------------------------------------------------------ */

int fcs_attach_won_deals(FcSession *s, const FcBlobIO *io)
{
    if (io) s->won_io = *io;
    else memset(&s->won_io, 0, sizeof s->won_io);
    return fc_won_load(&s->won, io);
}

int fcs_won_before(const FcSession *s, int game) { return fc_won_has(&s->won, game); }
uint32_t fcs_won_count(const FcSession *s) { return fc_won_count(&s->won); }

static void set_king(FcSession *s, int k)
{
    if (s->king != k) { s->king = k; invalidate(s); }
}

static void clear_selection(FcSession *s)
{
    if (s->sel) invalidate(s);
    s->sel = 0;
    s->sel_col = s->sel_pos = -1;
}

static void stack_clear(FcAction **st, int *n)
{
    for (int i = 0; i < *n; i++) free(st[i]);
    *n = 0;
}

static void hist_clear(FcSession *s) { stack_clear(s->hist, &s->nhist); }
static void redo_clear(FcSession *s) { stack_clear(s->redo, &s->nredo); }

/* Push an owned (malloc'ed) action; on out of memory the stack is dropped (it can't stay consistent). */
static void stack_push(FcAction ***st, int *n, int *cap, FcAction *a)
{
    if (*n == *cap) {
        int c = *cap ? *cap * 2 : 64;
        FcAction **h = realloc(*st, (size_t)c * sizeof *h);
        if (!h) { stack_clear(*st, n); free(a); return; }
        *st = h;
        *cap = c;
    }
    (*st)[(*n)++] = a;
}

static void hist_push(FcSession *s, const FcAction *a)
{
    size_t sz = offsetof(FcAction, steps) + (size_t)a->nsteps * sizeof(FcStep);
    FcAction *c = malloc(sz);
    if (!c) { hist_clear(s); return; }
    memcpy(c, a, sz);
    stack_push(&s->hist, &s->nhist, &s->hist_cap, c);
}

static int input_ok(const FcSession *s) { return !s->busy && !s->kbd_peek && s->game_number != 0; }

static void stop_kbd_peek(FcSession *s)
{
    if (!s->kbd_peek) return;
    set_timer(s, FCS_TIMER_PEEK, 0);
    s->kbd_peek = 0;
    s->peek_col = s->peek_pos = -1;
    invalidate(s);
}

static void run_command(FcSession *s, int cmd);

static void post_command(FcSession *s, int cmd)
{
    if (UI(s, post_command)) UI(s, post_command)(UI(s, ctx), cmd);
    else run_command(s, cmd);
}

/* RandomGameNumber with a fix: a repeated time seed (two requests in the same second) is bumped, and
 * the number never repeats the current game. Same 1..32767 range as XP, or 1..1000000 (extra). */
static int random_number(const FcSession *s, uint32_t t)
{
    return s->extras.full_range ? fc_random_game_number_full(t) : fc_random_game_number(t);
}

static int next_random(FcSession *s)
{
    uint32_t t = UI(s, now_seed) ? UI(s, now_seed)(UI(s, ctx)) : 0;
    if (s->seeded && t <= s->last_seed) t = s->last_seed + 1;
    int n = random_number(s, t);
    for (int i = 0; i < 16 && n == s->game_number; i++) n = random_number(s, ++t);
    s->last_seed = t;
    s->seeded = 1;
    return n;
}

/* ---- Init ---------------------------------------------------------------------------------------- */

void fcs_init(FcSession *s, const FcSessionUI *ui, const FcStore *store)
{
    memset(s, 0, sizeof *s);
    if (ui) s->ui = *ui;
    if (store) s->store = *store;
    fc_board_clear(&s->board);
    s->sel_col = s->sel_pos = -1;
    s->peek_col = s->peek_pos = -1;
    s->king = FCS_KING_RIGHT;
    fc_extras_default(&s->extras);
    fc_won_init(&s->won);
    fc_stats_init(&s->stats, store);
    fc_stats_migrate(&s->stats);
    fc_options_load(&s->opts, store);
}

void fcs_free(FcSession *s)
{
    hist_clear(s);
    free(s->hist);
    s->hist = NULL;
    s->hist_cap = 0;
    redo_clear(s);
    free(s->redo);
    s->redo = NULL;
    s->redo_cap = 0;
    fc_won_free(&s->won);
    fcs_assist_free(s);
}

/* ---- Queries ------------------------------------------------------------------------------------ */

int fcs_has_selection(const FcSession *s) { return s->sel; }
int fcs_undo_enabled(const FcSession *s) { return s->nhist > 0 && s->in_progress && s->game_number != 0; }
int fcs_redo_enabled(const FcSession *s) { return s->nredo > 0 && s->in_progress && s->game_number != 0; }
int fcs_restart_enabled(const FcSession *s) { return s->dealt; }

void fcs_view_state(const FcSession *s, FcsViewState *v)
{
    v->sel_col = s->sel ? s->sel_col : -1;
    v->sel_pos = s->sel ? s->sel_pos : -1;
    v->peek_col = s->peek_col;
    v->peek_pos = s->peek_pos;
    v->king = s->king;
    v->big_king = s->big_king;
    v->no_game = !s->dealt;
    fcs_assist_view(s, &v->hint_col, &v->hint_pos);
}

/* ---- Game over ------------------------------------------------------------------------------------ */

static void win(FcSession *s)                    /* CommitMoves win path, §6.3 */
{
    fcs_assist_stop(s);
    hist_clear(s);
    redo_clear(s);
    s->in_progress = 0;
    s->cheat = 0;
    fc_stats_record_win(&s->stats, s->game_number);
    if (fc_won_add(&s->won, s->game_number)) fc_won_save(&s->won, &s->won_io);
    clock_stop(s);
    status_changed(s);
    s->king = FCS_KING_BLANK;
    s->big_king = 1;
    invalidate(s);
    update_menu(s);
    int sel = s->select_flag;
    int yes = UI(s, you_win) ? UI(s, you_win)(UI(s, ctx), &sel) : 0;
    s->game_number = 0;                          /* frozen until the next game */
    update_menu(s);
    if (yes) {
        s->select_flag = sel != 0;
        post_command(s, sel ? FCS_CMD_SELECT : FCS_CMD_NEW);
    }
}

static void check_no_moves(FcSession *s)         /* CheckNoMoves, §6.1 */
{
    if (s->cheat != FCS_CHEAT_LOSE) {
        int m = fc_count_moves_xp(&s->board);
        if (m > 1) return;
        if (m == 1) {                            /* "one move left": FlashWindow x4, 400 ms */
            s->flash_left = 4;
            set_timer(s, FCS_TIMER_FLASH, 400);
            return;
        }
    }
    fcs_assist_stop(s);
    hist_clear(s);
    redo_clear(s);
    s->in_progress = 0;                          /* YouLose WM_INITDIALOG, §6.2 */
    fc_stats_record_loss(&s->stats, s->game_number);
    clock_stop(s);
    status_changed(s);
    update_menu(s);
    int same = 1;
    int yes = UI(s, you_lose) ? UI(s, you_lose)(UI(s, ctx), &same) : 0;
    s->game_number = 0;
    s->cheat = 0;
    update_menu(s);
    if (yes) post_command(s, same ? FCS_CMD_RESTART : s->select_flag ? FCS_CMD_SELECT : FCS_CMD_NEW);
}

/* ---- Commit: replay the built action card by card (§4.2), record it, check win / no moves ------ */

static void replay_forward(FcSession *s, const FcAction *a)    /* ReplayMove for each logged step */
{
    for (int i = 0; i < a->nsteps; i++) {
        const FcStep *st = &a->steps[i];
        if (UI(s, animate_step)) UI(s, animate_step)(UI(s, ctx), st, 1);
        int left = s->board.cards_left;
        fc_step_apply(&s->board, st);
        if (st->dst_col == 0) s->king = st->dst_pos < 4 ? FCS_KING_LEFT : FCS_KING_RIGHT;  /* 0x1004EB1 */
        invalidate(s);
        if (s->board.cards_left != left) cards_left_changed(s);
    }
}

static void finish(FcSession *s, int counted);

/* After a committed action or Redo that did not win (moved: it changed the board; before = the board
 * before it). Finish automatically (extra) comes before CheckNoMoves: a sure win always has a move, so
 * CheckNoMoves could only start its "one move left" flash for a game that is won at once (the cheat's
 * "lose" still loses through CheckNoMoves). Otherwise CheckNoMoves, then, unless the game ended or a
 * new one began meanwhile (deals), the warning's check of the new position. */
static void after_commit(FcSession *s, int moved, const FcBoard *before)
{
    if (moved && s->extras.auto_finish && s->cheat != FCS_CHEAT_LOSE && fc_sure_win(&s->board, NULL, NULL)) {
        finish(s, 0);                            /* like autoplay: not a move of its own */
        return;
    }
    unsigned deals = s->as.deals;
    check_no_moves(s);
    if (moved && s->as.deals == deals && s->in_progress && s->game_number != 0)
        fcs_assist_changed(s, FCS_AS_MOVE, before);
}

static void commit(FcSession *s, const FcAction *a, const FcBoard *after)
{
    int counted = a->nsteps > 0 && a->counted;
    if (counted) clock_start(s);                 /* the clock runs from the first committed move */
    if (a->nsteps > 0 && !s->kbd_peek && s->peek_col >= 0) {
        /* a right-button peek held through a move ends with it: XP only drew the card, and the
         * replay repaints the column normally (the peek would otherwise cover a card that lands) */
        s->peek_col = s->peek_pos = -1;
        invalidate(s);
    }
    s->busy++;
    replay_forward(s, a);
    s->board = *after;                           /* identical; keeps the replay honest */
    s->busy--;
    if (a->nsteps > 0) {                         /* a new action: the undone future is gone */
        hist_push(s, a);
        redo_clear(s);
    }
    if (counted) {
        s->moves++;
        status_changed(s);
    }
    update_menu(s);
    if (s->board.cards_left == 0) {
        win(s);
        return;
    }
    FcBoard before = a->before;                  /* a may be s->work, which finish() reuses */
    after_commit(s, a->nsteps > 0, &before);
}

/* ---- Clicks ---------------------------------------------------------------------------------------- */

static void click_select(FcSession *s, int col, int pos)   /* ClickSelect, §4.1 (keeps the history) */
{
    if (col == 0) {
        if (pos < 0 || pos > 3 || s->board.board[0][pos] == FC_EMPTY) return;   /* never a home card */
        s->sel = 1; s->sel_col = 0; s->sel_pos = pos;
        s->king = FCS_KING_LEFT;
    } else if (col >= 1 && col <= 8) {
        int last = fc_last_index(&s->board, col);
        if (last < 0) return;
        s->sel = 1; s->sel_col = col; s->sel_pos = last;          /* any card selects the bottom one */
    } else {
        return;
    }
    invalidate(s);
}

/* CheckTopRowOrEmptyColMove (0x1003850) for the selected card -> (col, pos). Not used for
 * tableau -> non-empty tableau. dry = 1 (cursor): never shows the MoveCol dialog. */
static int check_target(const FcSession *s, const FcBoard *b, int col, int pos, int *movecol, int dry)
{
    int sc = s->sel_col, sp = s->sel_pos;
    *movecol = FCS_MOVECOL_SINGLE;
    if (sc == 0 && col == 0 && sp == pos) return 1;                /* free cell to itself: deselect */
    Card src = b->board[sc][sp];
    if (col != 0) {
        if (b->board[col][0] != FC_EMPTY) return fc_can_stack(src, b->board[col][fc_last_index(b, col)]);
        if (sc == 0) return 1;                                     /* free cell -> empty column */
        int n = fc_cards_to_move(b, sc, col);                      /* run at the bottom, §2.5 */
        if (s->extras.standard_supermove) {                        /* extra: (f+1)*2^(e-1) */
            int m = fc_max_to_empty(b, 1);
            if (n > m) n = m;
        } else if (fc_free_cells_empty(b) == 0 && n > 1) n = 1;
        if (n <= 1 || dry) return n >= 1;
        int r = UI(s, ask_move_column) ? UI(s, ask_move_column)(UI(s, ctx)) : FCS_MOVECOL_COLUMN;
        *movecol = r;
        return r != FCS_MOVECOL_CANCEL;
    }
    if (pos < 0 || pos > 7) return 0;
    Card dst = b->board[0][pos];
    if (pos < 4) return dst == FC_EMPTY;                            /* free cell: only if empty */
    if (dst == FC_EMPTY) return fc_rank(src) == 0;                  /* any empty home takes any ace */
    return fc_suit(dst) == fc_suit(src) && fc_rank(src) == fc_rank(dst) + 1;
}

static void show_message(FcSession *s, int id, int n, int max)
{
    char buf[160];
    if (id == 307) snprintf(buf, sizeof buf, fcs_string(307), (unsigned)n, (unsigned)max);
    else snprintf(buf, sizeof buf, "%s", fcs_string(id));
    s->busy++;
    if (UI(s, message)) UI(s, message)(UI(s, ctx), id, buf);
    s->busy--;
}

static void click_move(FcSession *s, int col, int pos)     /* ClickMove, §2.4 */
{
    int sc = s->sel_col, sp = s->sel_pos;
    if (col < 0 || col > 8) { col = sc; pos = sp; }              /* miss: click on the selection */
    else if (col == 0) { if (pos < 0 || pos > 7) { col = sc; pos = sp; } }
    else { pos = fc_last_index(&s->board, col); if (pos < 0) pos = 0; }

    FcBoard b = s->board;
    FcAction *a = &s->work;
    fc_action_begin(a, &s->board);
    int std = s->extras.standard_supermove, msgs = s->opts.messages && !s->quiet;
    if (sc != 0 && col != 0 && b.board[col][0] != FC_EMPTY) {    /* tableau -> non-empty tableau */
        int n = fc_cards_to_move(&b, sc, col), max = fc_max_movable_rule(&b, std);
        if (n == 0) {
            if (!msgs) return;                                     /* silent, selection kept */
            show_message(s, 306, 0, 0);
        } else if (n <= max) {
            if (std) fc_move_cards_std(&b, a, sc, col, n);
            else fc_supermove(&b, a, sc, col);
        } else {
            if (!msgs) return;
            show_message(s, 307, n, max);
        }
    } else {
        int movecol;
        s->busy++;
        int ok = check_target(s, &b, col, pos, &movecol, 0);
        s->busy--;
        if (ok) {
            if (movecol == FCS_MOVECOL_COLUMN && std) {
                int n = fc_cards_to_move(&b, sc, col), m = fc_max_to_empty(&b, 1);
                fc_move_cards_std(&b, a, sc, col, n < m ? n : m);
            } else if (movecol == FCS_MOVECOL_COLUMN) {
                fc_move_run_via_free_cells(&b, a, sc, col);
            } else {
                fc_queue(&b, a, sc, sp, col, pos);
            }
        } else if (movecol != FCS_MOVECOL_CANCEL) {               /* cancel: plain deselect */
            if (!msgs) return;
            show_message(s, 306, 0, 0);
        }
    }
    a->counted = a->nsteps > 0;                                    /* the user's part, before autoplay */
    fc_autoplay(&b, a, s->cheat == FCS_CHEAT_WIN);
    clear_selection(s);
    commit(s, a, &b);
}

static void do_click(FcSession *s, int col, int pos)
{
    if (s->sel) click_move(s, col, pos);
    else click_select(s, col, pos);
}

void fcs_click(FcSession *s, int col, int pos)
{
    if (s->swallow_click) { s->swallow_click = 0; return; }       /* activation click, §4.6 */
    if (!input_ok(s)) return;
    fcs_assist_input(s);
    do_click(s, col, pos);
}

void fcs_mouse_activate(FcSession *s) { s->swallow_click = 1; }

void fcs_dblclick(FcSession *s, int col, int pos)  /* §4.4; no XP repost quirk: else a plain click */
{
    if (s->swallow_click) { s->swallow_click = 0; return; }
    if (!input_ok(s)) return;
    fcs_assist_input(s);
    if (s->opts.dblclick && s->sel && s->sel_col >= 1 && col == s->sel_col) {
        int f = -1;
        for (int i = 3; i >= 0; i--)
            if (s->board.board[0][i] == FC_EMPTY) f = i;         /* leftmost empty free cell */
        if (f >= 0) {                                              /* DblClickToFreeCell, 0x1003CFB */
            FcBoard b = s->board;
            FcAction *a = &s->work;
            fc_action_begin(a, &s->board);
            fc_queue(&b, a, s->sel_col, s->sel_pos, 0, f);
            a->counted = a->nsteps > 0;
            fc_autoplay(&b, a, s->cheat == FCS_CHEAT_WIN);
            clear_selection(s);
            commit(s, a, &b);
            return;
        }
    }
    do_click(s, col, pos);
}

/* ---- Keyboard (OnChar, 0x10043C9, §4.7) ---------------------------------------------------------- */

void fcs_char(FcSession *s, int ch)
{
    if (ch == 'h' || ch == 'H') {                                  /* extra: Hint */
        fcs_assist_hint(s);
        return;
    }
    if (ch < '0' || ch > '9' || !input_ok(s)) return;
    fcs_assist_input(s);
    const FcBoard *b = &s->board;
    if (ch == '9') {                                               /* selected card -> its home slot */
        if (!s->sel) return;
        int slot = b->suit_home_slot[fc_suit(b->board[s->sel_col][s->sel_pos])];
        do_click(s, 0, slot < 0 ? 4 : slot);
        return;
    }
    if (ch == '0') {
        int i;
        if (!s->sel) {                                             /* select the first free-cell card */
            for (i = 0; i < 4 && b->board[0][i] == FC_EMPTY; i++) {}
            if (i < 4) do_click(s, 0, i);
        } else if (s->sel_col == 0) {                              /* step to the next free-cell card */
            int from = s->sel_pos;
            /* XP turns the messages option off around this click; we only silence it, because the
             * click can open YouWin/YouLose, and a session end meanwhile saves s->opts */
            s->quiet++;
            do_click(s, 0, from);                                  /* deselect (autoplay, commit) */
            s->quiet--;
            if (!input_ok(s)) return;
            for (i = from + 1; i < 4 && b->board[0][i] == FC_EMPTY; i++) {}
            if (i < 4) do_click(s, 0, i);
        } else {                                                   /* tableau card -> first empty cell */
            for (i = 0; i < 4 && b->board[0][i] != FC_EMPTY; i++) {}
            do_click(s, 0, i < 4 ? i : 0);
        }
        return;
    }
    int col = ch - '0';
    if (s->sel && s->sel_col == col && b->board[col][1] != FC_EMPTY) {   /* peek the whole column */
        s->kbd_peek = 1;
        s->peek_col = col;
        s->peek_pos = 0;
        invalidate(s);
        set_timer(s, FCS_TIMER_PEEK, 300);
        return;
    }
    do_click(s, col, 0);
}

/* ---- Right button peek (§4.5) ------------------------------------------------------------------- */

void fcs_rbutton_down(FcSession *s, int col, int pos)
{
    if (!input_ok(s) || col < 1 || col > 8) return;
    fcs_assist_input(s);
    if (pos >= 0 && pos < fc_last_index(&s->board, col)) {        /* covered cards only */
        s->peek_col = col;
        s->peek_pos = pos;
        invalidate(s);
    }
}

void fcs_rbutton_up(FcSession *s)
{
    if (s->kbd_peek || s->peek_col < 0) return;
    s->peek_col = s->peek_pos = -1;
    invalidate(s);
}

/* ---- Mouse move: king and cursor (§4.8, layout.md §4, §6) --------------------------------------- */

int fcs_cursor(const FcSession *s, int col, int pos, int on_card)
{
    if (s->kbd_peek || s->busy) return FCS_CURSOR_WAIT;
    if (s->as.hint == FCS_HINT_WAIT) return FCS_CURSOR_APPSTARTING;
    if (!s->sel || col < 0 || col > 8) return FCS_CURSOR_ARROW;
    const FcBoard *b = &s->board;
    int mc;
    if (col == 0) {
        if (pos < 0 || pos > 7 || (s->sel_col == 0 && s->sel_pos == pos)) return FCS_CURSOR_ARROW;
        return check_target(s, b, 0, pos, &mc, 1) ? FCS_CURSOR_UPARROW : FCS_CURSOR_ARROW;
    }
    if (b->board[col][0] == FC_EMPTY) return FCS_CURSOR_UPARROW;     /* no legality check (XP) */
    if (!on_card || s->sel_col == col) return FCS_CURSOR_ARROW;
    if (s->sel_col != 0) {
        int n = fc_cards_to_move(b, s->sel_col, col);
        return n > 0 && n <= fc_max_movable_rule(b, s->extras.standard_supermove) ? FCS_CURSOR_DOWNARROW
                                                                                   : FCS_CURSOR_ARROW;
    }
    return check_target(s, b, col, 0, &mc, 1) ? FCS_CURSOR_DOWNARROW : FCS_CURSOR_ARROW;
}

int fcs_mouse_move(FcSession *s, int col, int pos, int on_card)
{
    if (!s->kbd_peek && !s->busy && col == 0 && pos >= 0 && pos <= 7)
        set_king(s, pos < 4 ? FCS_KING_LEFT : FCS_KING_RIGHT);
    return fcs_cursor(s, col, pos, on_card);
}

/* ---- Timers ---------------------------------------------------------------------------------------- */

void fcs_timer(FcSession *s, int id)
{
    if (id == FCS_TIMER_FLASH) {                                   /* 0x1003ED3 */
        if (s->flash_left <= 0) { set_timer(s, FCS_TIMER_FLASH, 0); return; }
        if (UI(s, flash)) UI(s, flash)(UI(s, ctx), 1);
        if (--s->flash_left <= 0) {
            if (UI(s, flash)) UI(s, flash)(UI(s, ctx), 0);
            set_timer(s, FCS_TIMER_FLASH, 0);
        }
    } else if (id == FCS_TIMER_PEEK) {                             /* 0x1003F0D */
        if (!s->kbd_peek) { set_timer(s, FCS_TIMER_PEEK, 0); return; }
        int col = s->peek_col;
        if (++s->peek_pos < fc_last_index(&s->board, col)) { invalidate(s); return; }
        stop_kbd_peek(s);                                          /* bottom card reached */
        if (input_ok(s)) do_click(s, col, 0);                      /* XP posts a click: deselect */
    } else if (id == FCS_TIMER_HINT || id == FCS_TIMER_HINT_WAIT) {
        fcs_assist_timer(s, id);
    }
}

/* ---- Commands ---------------------------------------------------------------------------------- */

static void deal(FcSession *s, int n)
{
    char title[64];
    fc_deal(&s->board, n);
    s->game_number = n;
    s->in_progress = 1;
    s->dealt = 1;
    s->sel = 0;
    s->sel_col = s->sel_pos = -1;
    s->peek_col = s->peek_pos = -1;
    s->king = FCS_KING_RIGHT;
    s->big_king = 0;
    hist_clear(s);                                                 /* fixes XP's stale undo, §7.1 */
    redo_clear(s);
    clock_reset(s);
    status_changed(s);
    snprintf(title, sizeof title, fcs_string(303), n);
    if (UI(s, set_title)) UI(s, set_title)(UI(s, ctx), title);
    cards_left_changed(s);
    update_menu(s);
    invalidate(s);
    fcs_assist_changed(s, FCS_AS_DEAL, NULL);
}

static void new_game(FcSession *s, int cmd)       /* common New/Select/Restart path, §7.2 */
{
    int n = 0, resign = 0;
    stop_kbd_peek(s);
    if (cmd == FCS_CMD_NEW) n = next_random(s);
    if (s->in_progress) {
        if (UI(s, confirm_resign) && !UI(s, confirm_resign)(UI(s, ctx))) return;
        resign = 1;
    }
    if (cmd == FCS_CMD_SELECT) {
        int init = next_random(s), v = 0;
        for (;;) {
            if (!UI(s, ask_game_number) || !UI(s, ask_game_number)(UI(s, ctx), init, &v))
                return;                                            /* Cancel: nothing changes (fix) */
            if (v >= FC_GAME_MIN && v <= FC_GAME_MAX && v != 0) break;
            init = 0;                                              /* invalid: re-show with "0" */
        }
        n = v;
    } else if (cmd == FCS_CMD_RESTART) {
        n = s->in_progress ? s->game_number : s->stats.last_recorded;
        if (n == 0) return;
    }
    if (resign) fc_stats_record_loss(&s->stats, s->game_number);
    if (cmd == FCS_CMD_NEW) s->select_flag = 0;
    else if (cmd == FCS_CMD_SELECT) s->select_flag = 1;
    deal(s, n);
}

static void undo(FcSession *s)                    /* §7.1, one history entry per call */
{
    if (!fcs_undo_enabled(s)) return;
    stop_kbd_peek(s);
    s->peek_col = s->peek_pos = -1;
    clear_selection(s);
    FcAction *a = s->hist[--s->nhist];
    s->busy++;
    for (int i = a->nsteps - 1; i >= 0; i--) {
        const FcStep *st = &a->steps[i];
        if (UI(s, animate_step)) UI(s, animate_step)(UI(s, ctx), st, 0);
        int left = s->board.cards_left;
        fc_step_unapply(&s->board, st);
        invalidate(s);
        if (s->board.cards_left != left) cards_left_changed(s);
    }
    fc_undo_action(&s->board, a);                  /* exact restore (no autoplay after undo) */
    s->busy--;
    if (a->counted) {
        s->moves--;
        status_changed(s);
    }
    stack_push(&s->redo, &s->nredo, &s->redo_cap, a);   /* now owned by the redo stack */
    invalidate(s);
    update_menu(s);
    fcs_assist_changed(s, FCS_AS_UNDO, NULL);
}

static void redo(FcSession *s)                    /* extra: re-apply the last undone action */
{
    if (!fcs_redo_enabled(s)) return;
    stop_kbd_peek(s);
    s->peek_col = s->peek_pos = -1;
    clear_selection(s);
    FcAction *a = s->redo[--s->nredo];
    if (memcmp(&s->board, &a->before, sizeof s->board) != 0) {     /* stale (cannot happen): drop */
        free(a);
        redo_clear(s);
        update_menu(s);
        return;
    }
    int counted = a->counted;
    FcBoard before = a->before;    /* a is freed by stack_push on out of memory, or if the game is lost */
    if (counted) clock_start(s);
    s->busy++;
    replay_forward(s, a);                          /* same steps, same animation as the original */
    s->busy--;
    stack_push(&s->hist, &s->nhist, &s->hist_cap, a);   /* back onto the undo history */
    if (counted) {
        s->moves++;
        status_changed(s);
    }
    invalidate(s);
    update_menu(s);
    if (s->board.cards_left == 0) {                /* as after the original commit */
        win(s);
        return;
    }
    after_commit(s, 1, &before);
}

/* Extra (v1.2): every remaining card home as one action (fc_sure_win's moves, each followed by
 * autoplay as any move), animated card by card, then the normal win. counted: Game > Finish is a
 * move, the automatic finish is not (like autoplay). */
static void finish(FcSession *s, int counted)
{
    FcSolveMove mv[52];
    int n = 0;
    if (!fcs_finish_enabled(s) || !fc_sure_win(&s->board, mv, &n) || n <= 0) return;
    stop_kbd_peek(s);
    s->peek_col = s->peek_pos = -1;
    clear_selection(s);
    FcBoard b = s->board;
    FcAction *a = &s->work;
    fc_action_begin(a, &s->board);
    for (int i = 0; i < n; i++) {
        fc_queue(&b, a, mv[i].src_col, mv[i].src_pos, 0, mv[i].dst_pos);
        fc_autoplay(&b, a, 0);
    }
    a->counted = counted;
    commit(s, a, &b);                              /* cards_left 0: the win flow */
}

static void run_command(FcSession *s, int cmd)
{
    switch (cmd) {
    case FCS_CMD_NEW: case FCS_CMD_SELECT: case FCS_CMD_RESTART:
        new_game(s, cmd);
        break;
    case FCS_CMD_UNDO:
        undo(s);
        break;
    case FCS_CMD_REDO:
        redo(s);
        break;
    case FCS_CMD_HINT:
        fcs_assist_hint(s);
        break;
    case FCS_CMD_FINISH:
        finish(s, 1);
        break;
    case FCS_CMD_CHEAT: {                         /* Ctrl+Shift+F10, §10 */
        int r = UI(s, cheat_prompt) ? UI(s, cheat_prompt)(UI(s, ctx)) : FCS_CHEAT_NONE;
        s->cheat = r == FCS_CHEAT_WIN || r == FCS_CHEAT_LOSE ? r : FCS_CHEAT_NONE;
        break;
    }
    }
}

void fcs_command(FcSession *s, int cmd)
{
    if (s->busy) return;
    if (cmd != FCS_CMD_HINT) fcs_assist_input(s);
    run_command(s, cmd);
}

int fcs_close(FcSession *s)                       /* WM_CLOSE, §7.2 */
{
    if (s->busy) return 0;
    if (s->in_progress) {
        if (UI(s, confirm_resign) && !UI(s, confirm_resign)(UI(s, ctx))) return 0;
        fc_stats_record_loss(&s->stats, s->game_number);
        s->in_progress = 0;
    }
    fcs_assist_stop(s);
    stop_kbd_peek(s);
    fc_options_save(&s->opts, &s->store);
    return 1;
}
