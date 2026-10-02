/*
 * FreeCell HD — native tests of the v1.2 solver extras in the controller (src/freecell/assist.c and the
 * Finish action in session.c): Hint (request, wait cursor, flash steps, cache, cancel, time limit,
 * unwinnable / gave up; 2c: hint cycling), the unwinnable warning (once per game, derivation, reset through Undo, the
 * deal itself), Finish and Finish automatically, the menu states and the extras' store. A fake UI
 * records everything; the "background" solver is synchronous: solve_start only records the request
 * and the test answers it with the real solver (or a scripted status), as the Win32 worker's posted
 * message would, outside any session call.
 */
#include "freecell/session.h"
#include "freecell/solver.h"
#include "freecell/stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (a), _b = (b); checks++; if (_a != _b) { fails++; \
    printf("FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

static FcSolver *sv;

/* ---- fake UI -------------------------------------------------------------------------------------- */

typedef struct Fake {
    FcSession *s;
    int nmsg, msg[64];
    int timer[8];                      /* ms by timer id (0 = not running) */
    int hint_en, finish_en, nmenu;
    int nreq, ncancel, req_open, req_std;
    uint32_t req_id;
    FcBoard req_board;
    int movecol, gn;
    int nwin, nlose, nfwd, ninval, nflash;
    int busy_seen;                     /* s->busy while a message was up */
} Fake;

static void ui_message(void *c, int id, const char *t)
{
    Fake *f = c;
    (void)t;
    if (f->nmsg < 64) f->msg[f->nmsg] = id;
    f->nmsg++;
    f->busy_seen = f->s->busy;
}
static int ui_movecol(void *c) { return ((Fake *)c)->movecol; }
static int ui_gamenum(void *c, int init, int *v) { (void)init; *v = ((Fake *)c)->gn; return 1; }
static int ui_win(void *c, int *sel) { (void)sel; ((Fake *)c)->nwin++; return 0; }
static int ui_lose(void *c, int *same) { (void)same; ((Fake *)c)->nlose++; return 0; }
static void ui_anim(void *c, const FcStep *st, int fwd) { (void)st; if (fwd) ((Fake *)c)->nfwd++; }
static void ui_inval(void *c) { ((Fake *)c)->ninval++; }
static void ui_timer(void *c, int id, int ms) { ((Fake *)c)->timer[id & 7] = ms; }
static void ui_flash(void *c, int invert) { if (invert) ((Fake *)c)->nflash++; }
static void ui_solve_start(void *c, uint32_t id, const FcBoard *b, int std)
{
    Fake *f = c;
    f->nreq++;
    f->req_open = 1;
    f->req_id = id;
    f->req_board = *b;
    f->req_std = std;
}
static void ui_solve_cancel(void *c) { Fake *f = c; f->ncancel++; f->req_open = 0; }
static void ui_assist_menu(void *c, int h, int fin) { Fake *f = c; f->hint_en = h; f->finish_en = fin; f->nmenu++; }

static void setup(FcSession *s, Fake *f, int with_solver)
{
    FcSessionUI u;
    memset(f, 0, sizeof *f);
    memset(&u, 0, sizeof u);
    f->s = s;
    f->movecol = FCS_MOVECOL_COLUMN;
    u.ctx = f;
    u.message = ui_message;
    u.ask_move_column = ui_movecol;
    u.ask_game_number = ui_gamenum;
    u.you_win = ui_win;
    u.you_lose = ui_lose;
    u.animate_step = ui_anim;
    u.invalidate = ui_inval;
    u.set_timer = ui_timer;
    u.flash = ui_flash;
    u.assist_menu = ui_assist_menu;
    if (with_solver) {
        u.solve_start = ui_solve_start;
        u.solve_cancel = ui_solve_cancel;
    }
    fcs_init(s, &u, NULL);
}

static void start_game(FcSession *s, Fake *f, int n)
{
    f->gn = n;
    fcs_command(s, FCS_CMD_SELECT);
}

/* Answer the open request with the real solver (as the worker would). Returns the status, -1 if no
 * request was open. */
static int pump(FcSession *s, Fake *f)
{
    FcSolveResult r;
    if (!f->req_open) return -1;
    f->req_open = 0;
    fc_solve(sv, &f->req_board, f->req_std, NULL, &r);
    CHECK_EQ(fcs_solve_done(s, f->req_id, r.status, r.moves, r.nmoves), 1);
    return r.status;
}

static void answer(FcSession *s, Fake *f, int status)   /* a scripted answer without moves */
{
    CHECK(f->req_open);
    f->req_open = 0;
    CHECK_EQ(fcs_solve_done(s, f->req_id, status, NULL, 0), 1);
}

static void tick(FcSession *s, Fake *f, int id)
{
    CHECK(f->timer[id] > 0);
    fcs_timer(s, id);
}

static void view(const FcSession *s, int *col, int *pos)
{
    FcsViewState v;
    fcs_view_state(s, &v);
    *col = v.hint_col;
    *pos = v.hint_pos;
}

static int same_board(const FcBoard *a, const FcBoard *b) { return memcmp(a, b, sizeof *a) == 0; }

static Card card_of(const char *p)
{
    const char *ranks = "A23456789TJQK", *suits = "CDHS";
    return (int)(strchr(ranks, p[0]) - ranks) * 4 + (int)(strchr(suits, p[1]) - suits);
}

/* cols: 8 strings of cards deepest first ("KD QD 5D"); fcs: 4 cards or NULL; homes: top card per suit
 * C D H S (or NULL), in home slots 4..7 in that order. */
static void make_board(FcBoard *b, const char *const cols[8], const char *const fcs[4], const char *const homes[4])
{
    int home = 0, k = 0;
    fc_board_clear(b);
    for (int s = 0; s < 4; s++)
        if (homes[s]) {
            Card c = card_of(homes[s]);
            b->home_rank[fc_suit(c)] = (int8_t)fc_rank(c);
            b->suit_home_slot[fc_suit(c)] = (int8_t)(4 + k);
            b->board[0][4 + k++] = c;
            home += fc_rank(c) + 1;
        }
    for (int i = 0; i < 4; i++) b->board[0][i] = fcs[i] ? card_of(fcs[i]) : FC_EMPTY;
    for (int c = 1; c <= 8; c++)
        for (const char *p = cols[c - 1]; *p; p += p[2] ? 3 : 2) b->board[c][fc_last_index(b, c) + 1] = card_of(p);
    b->cards_left = 52 - home;
}

/* The cells a hint for m flashes on b: source (cards from *sp on) and destination. */
static void expected_cells(const FcBoard *b, const FcSolveMove *m, int *sc, int *sp, int *dc, int *dp)
{
    *sc = m->src_col;
    *sp = m->src_pos;
    if (m->src_col >= 1) *sp = fc_last_index(b, m->src_col) - (m->ncards > 1 ? m->ncards : 1) + 1;
    *dc = m->dst_col;
    *dp = m->dst_pos;
    if (m->kind == FC_SM_COLUMN) *dp = fc_last_index(b, m->dst_col);
    else if (m->kind == FC_SM_EMPTY) *dp = -1;
    else if (m->kind == FC_SM_AUTOPLAY) { *dc = m->src_col; *dp = m->src_col ? fc_last_index(b, m->src_col) : m->src_pos; }
}

/* Run a whole flash: on, off, on, off for the source, then for the destination; the board stays as
 * it was and nothing gets selected. */
static void check_flash(FcSession *s, Fake *f, int sc, int sp, int dc, int dp)
{
    FcBoard b0 = s->board;
    int col, pos;
    CHECK_EQ(s->as.hint, FCS_HINT_FLASH);
    CHECK_EQ(f->timer[FCS_TIMER_HINT], FCS_HINT_STEP_MS);
    for (int step = 0; step < 8; step++) {
        view(s, &col, &pos);
        if (step & 1) {
            CHECK_EQ(col, -1);
        } else if (step < 4) {
            CHECK_EQ(col, sc); CHECK_EQ(pos, sp);
        } else {
            CHECK_EQ(col, dc); CHECK_EQ(pos, dp);
        }
        CHECK_EQ(s->sel, 0);
        tick(s, f, FCS_TIMER_HINT);
    }
    view(s, &col, &pos);
    CHECK_EQ(col, -1);
    CHECK_EQ(s->as.hint, FCS_HINT_IDLE);
    CHECK_EQ(f->timer[FCS_TIMER_HINT], 0);
    CHECK(same_board(&s->board, &b0));
}

/* Play a solver move through the session's clicks (MoveCol answered as the move says). */
static void play(FcSession *s, Fake *f, const FcSolveMove *m)
{
    fcs_click(s, m->src_col, m->src_pos);
    f->movecol = m->movecol == FC_SM_NODIALOG ? FCS_MOVECOL_CANCEL : m->movecol;
    fcs_click(s, m->dst_col, m->dst_pos);
    f->movecol = FCS_MOVECOL_COLUMN;
}

/* ---- Hint ------------------------------------------------------------------------------------------- */

static void test_hint_basic(void)
{
    FcSession s; Fake f;
    FcSolveResult r;
    FcBoard deal;
    setup(&s, &f, 1);
    CHECK_EQ(fcs_hint_enabled(&s), 0);
    fcs_char(&s, 'h');                                   /* no game: nothing */
    CHECK_EQ(f.nreq, 0);
    start_game(&s, &f, 1);
    CHECK_EQ(f.nreq, 0);                                 /* the warning is off: no search */
    CHECK_EQ(f.hint_en, 1);
    CHECK_EQ(f.finish_en, 0);
    deal = s.board;
    fc_deal(&deal, 1);
    CHECK(same_board(&s.board, &deal));

    fcs_char(&s, 'h');
    CHECK_EQ(f.nreq, 1);
    CHECK(f.req_open);
    CHECK(same_board(&f.req_board, &s.board));
    CHECK_EQ(f.req_std, 0);
    CHECK_EQ(fcs_hint_waiting(&s), 1);
    CHECK_EQ(fcs_cursor(&s, FCS_MISS, -1, 0), FCS_CURSOR_APPSTARTING);
    CHECK_EQ(f.timer[FCS_TIMER_HINT_WAIT], FCS_HINT_TIMEOUT_MS);
    fcs_char(&s, 'H');                                   /* while waiting: ignored */
    CHECK_EQ(f.nreq, 1);
    CHECK_EQ(pump(&s, &f), FC_SOLVE_SOLVED);
    CHECK_EQ(f.timer[FCS_TIMER_HINT_WAIT], 0);
    CHECK_EQ(fcs_cursor(&s, FCS_MISS, -1, 0), FCS_CURSOR_ARROW);
    CHECK_EQ(f.nmsg, 0);

    fc_solve(sv, &deal, 0, NULL, &r);                    /* the same search, independently */
    CHECK_EQ(r.status, FC_SOLVE_SOLVED);
    CHECK_EQ(s.as.hint_move.card, r.moves[0].card);
    int sc, sp, dc, dp;
    expected_cells(&s.board, &r.moves[0], &sc, &sp, &dc, &dp);
    CHECK_EQ(sc, 6); CHECK_EQ(sp, 5);                    /* game #1: 3D (column 6) to free cell 1 */
    CHECK_EQ(dc, 0); CHECK_EQ(dp, 0);
    check_flash(&s, &f, sc, sp, dc, dp);

    /* again: from the cache, at once, the same move; also with a card selected (it is deselected) */
    fcs_click(&s, 3, 0);
    CHECK_EQ(s.sel, 1);
    fcs_command(&s, FCS_CMD_HINT);
    CHECK_EQ(f.nreq, 1);
    CHECK_EQ(s.sel, 0);
    check_flash(&s, &f, sc, sp, dc, dp);
    fcs_click(&s, FCS_MISS, -1);                        /* other input: the next Hint is the best again
                                                           (not the next one: hint cycling, 2c) */

    /* follow the hints to the win: one search in all, every hint shows the solution's next move */
    FcSolveMove sol[128];
    int n = r.nmoves;
    memcpy(sol, r.moves, (size_t)n * sizeof sol[0]);
    for (int i = 0; i < n && f.nwin == 0; i++) {
        fcs_char(&s, 'h');
        CHECK_EQ(s.as.hint, FCS_HINT_FLASH);
        CHECK_EQ(s.as.hint_move.card, sol[i].card);
        CHECK_EQ(s.as.hint_move.kind, sol[i].kind);
        expected_cells(&s.board, &sol[i], &sc, &sp, &dc, &dp);
        if (i < 6 || i == n - 1) check_flash(&s, &f, sc, sp, dc, dp);
        play(&s, &f, &sol[i]);                           /* the first click ends the flash */
        CHECK_EQ(f.timer[FCS_TIMER_HINT], 0);
    }
    CHECK_EQ(f.nwin, 1);
    CHECK_EQ(f.nreq, 1);
    CHECK_EQ(f.nmsg, 0);
    CHECK_EQ(f.hint_en, 0);                              /* the game is over */
    fcs_char(&s, 'h');
    CHECK_EQ(f.nreq, 1);

    /* Restart (same deal): the cache still knows the way */
    start_game(&s, &f, 1);
    fcs_char(&s, 'h');
    CHECK_EQ(f.nreq, 1);
    CHECK_EQ(s.as.hint_move.card, sol[0].card);
    fcs_free(&s);
}

/* 2c: Hint again while the last hint is current shows the next move: the solver's first, then every
 * other action by the solver's estimate of where it leads, wrapping; other input or a move starts over. */
static void test_hint_cycle(void)
{
    FcSession s; Fake f;
    FcSolveMove mv[FC_SOLVE_MAX_MOVES], first, seen[FC_SOLVE_MAX_MOVES + 1];
    int n, nseen = 0, k, est_prev = -1, order_ok = 1;
    setup(&s, &f, 1);
    start_game(&s, &f, 1);
    fcs_char(&s, 'h');
    CHECK_EQ(pump(&s, &f), FC_SOLVE_SOLVED);
    first = s.as.hint_move;
    seen[nseen++] = first;
    n = fc_solve_moves(&s.board, 0, mv);                 /* game #1: no bare deselect in the list */
    /* the alternatives, one per press, each flashed like a hint, each a legal action */
    for (k = 0; k < n + 3; k++) {
        tick(&s, &f, FCS_TIMER_HINT);                    /* (the flash's own steps keep the cycle) */
        fcs_char(&s, k & 1 ? 'H' : 'h');
        CHECK_EQ(s.as.hint, FCS_HINT_FLASH);
        if (!memcmp(&s.as.hint_move, &first, sizeof first)) break;   /* wrapped */
        FcBoard b = s.board;
        CHECK(fc_solve_play(&b, &s.as.hint_move, 0));
        int e = fc_solve_estimate(&b);
        if (e < est_prev) order_ok = 0;
        est_prev = e;
        for (int j = 0; j < nseen; j++)
            if (!memcmp(&seen[j], &s.as.hint_move, sizeof first)) order_ok = 0;   /* each once */
        seen[nseen++] = s.as.hint_move;
    }
    CHECK(order_ok);
    CHECK(k < n + 3);                                    /* back to the solver's move */
    CHECK_EQ(nseen, n);                                  /* every action but the solver's own, once */
    CHECK_EQ(f.nreq, 1);                                 /* no new search */
    /* the first alternative is the best estimate of all the others */
    {
        int best = 1 << 30;
        for (int i = 0; i < n; i++) {
            FcBoard b = s.board;
            if (!memcmp(&mv[i], &first, sizeof first) || !fc_solve_play(&b, &mv[i], 0)) continue;
            FcBoard c = s.board;
            fc_solve_play(&c, &first, 0);
            if (same_board(&b, &c)) continue;
            if (fc_solve_estimate(&b) < best) best = fc_solve_estimate(&b);
        }
        FcBoard b = s.board;
        fc_solve_play(&b, &seen[1], 0);
        CHECK_EQ(fc_solve_estimate(&b), best);
    }
    /* other input starts over: a click, a digit, a command */
    fcs_char(&s, 'h');
    CHECK(memcmp(&s.as.hint_move, &first, sizeof first) != 0);
    fcs_click(&s, FCS_MISS, -1);
    fcs_char(&s, 'h');
    CHECK(!memcmp(&s.as.hint_move, &first, sizeof first));
    fcs_char(&s, 'h');
    fcs_command(&s, FCS_CMD_REDO);                       /* (nothing to redo) */
    fcs_char(&s, 'h');
    CHECK(!memcmp(&s.as.hint_move, &first, sizeof first));
    /* a move: the next position's own best (from the cached solution) */
    fcs_char(&s, 'h');
    play(&s, &f, &first);
    fcs_char(&s, 'h');
    CHECK_EQ(f.nreq, 1);
    CHECK_EQ(s.as.hint, FCS_HINT_FLASH);
    CHECK(s.as.cyc_idx == 0);
    /* an alternative into a position known to be unwinnable is left out */
    {
        FcBoard here = s.board;
        FcSolveMove alt;
        fcs_char(&s, 'h');
        alt = s.as.hint_move;                            /* the best alternative */
        FcBoard after = here;
        fc_solve_play(&after, &alt, 0);
        s.as.lost[0] = after;                            /* pretend a search proved it unwinnable */
        s.as.lost_std[0] = 0;
        if (s.as.nlost == 0) s.as.nlost = 1;
        fcs_click(&s, FCS_MISS, -1);
        fcs_char(&s, 'h');                               /* the solver's move */
        fcs_char(&s, 'h');
        CHECK(memcmp(&s.as.hint_move, &alt, sizeof alt) != 0);
    }
    fcs_free(&s);
}

static void test_hint_cancel_and_failures(void)
{
    FcSession s; Fake f;
    int col, pos;
    setup(&s, &f, 1);
    start_game(&s, &f, 2);

    /* a click during the flash ends it and does its job */
    fcs_char(&s, 'h');
    CHECK_EQ(pump(&s, &f), FC_SOLVE_SOLVED);
    CHECK_EQ(s.as.hint, FCS_HINT_FLASH);
    tick(&s, &f, FCS_TIMER_HINT);
    fcs_click(&s, 1, 0);
    CHECK_EQ(s.as.hint, FCS_HINT_IDLE);
    CHECK_EQ(f.timer[FCS_TIMER_HINT], 0);
    view(&s, &col, &pos);
    CHECK_EQ(col, -1);
    CHECK_EQ(s.sel, 1);
    fcs_click(&s, 1, 0);                                 /* deselect (nothing moves: no autoplay) */
    CHECK_EQ(s.sel, 0);

    /* a digit key during the wait: the search is cancelled, its late answer ignored */
    fcs_command(&s, FCS_CMD_SELECT);                     /* game #2 again (f.gn): a fresh deal */
    s.board.board[0][0] = s.board.board[1][fc_last_index(&s.board, 1)];
    s.board.board[1][fc_last_index(&s.board, 1)] = FC_EMPTY;
    fcs_char(&s, 'h');
    CHECK_EQ(f.req_open, 1);
    uint32_t old = f.req_id;
    int nc = f.ncancel;
    fcs_char(&s, '5');
    CHECK_EQ(f.ncancel, nc + 1);
    CHECK_EQ(f.req_open, 0);
    CHECK_EQ(s.as.hint, FCS_HINT_IDLE);
    CHECK_EQ(f.timer[FCS_TIMER_HINT_WAIT], 0);
    FcSolveMove dummy = s.as.hint_move;
    CHECK_EQ(fcs_solve_done(&s, old, FC_SOLVE_SOLVED, &dummy, 1), 1);
    CHECK_EQ(s.as.hint, FCS_HINT_IDLE);
    CHECK_EQ(s.sel, 1);                                  /* the key did its job */
    s.sel = 0;
    s.sel_col = s.sel_pos = -1;

    /* the time limit: the search is stopped, "No hint is available." */
    s.board.board[0][1] = s.board.board[2][fc_last_index(&s.board, 2)];
    s.board.board[2][fc_last_index(&s.board, 2)] = FC_EMPTY;
    fcs_char(&s, 'h');
    CHECK_EQ(f.req_open, 1);
    nc = f.ncancel;
    int nm = f.nmsg;
    tick(&s, &f, FCS_TIMER_HINT_WAIT);
    CHECK_EQ(f.ncancel, nc + 1);
    CHECK_EQ(f.nmsg, nm + 1);
    CHECK_EQ(f.msg[nm], FCS_STR_HINT_NONE);
    CHECK_EQ(f.busy_seen, 1);                            /* input is blocked while the box is up */
    CHECK_EQ(s.as.hint, FCS_HINT_IDLE);
    CHECK_EQ(f.timer[FCS_TIMER_HINT_WAIT], 0);
    CHECK_EQ(fcs_cursor(&s, FCS_MISS, -1, 0), FCS_CURSOR_ARROW);

    /* the search gave up (or was cancelled): "No hint is available." */
    fcs_char(&s, 'h');
    answer(&s, &f, FC_SOLVE_GAVE_UP);
    CHECK_EQ(f.nmsg, nm + 2);
    CHECK_EQ(f.msg[nm + 1], FCS_STR_HINT_NONE);
    fcs_char(&s, 'h');                                   /* not cached: searched again */
    answer(&s, &f, FC_SOLVE_CANCELLED);
    CHECK_EQ(f.msg[nm + 2], FCS_STR_HINT_NONE);

    /* an answer while the session is inside a call waits */
    fcs_char(&s, 'h');
    s.busy = 1;
    CHECK_EQ(fcs_solve_done(&s, f.req_id, FC_SOLVE_GAVE_UP, NULL, 0), 0);
    CHECK_EQ(s.as.hint, FCS_HINT_WAIT);
    s.busy = 0;
    answer(&s, &f, FC_SOLVE_GAVE_UP);
    CHECK_EQ(s.as.hint, FCS_HINT_IDLE);

    /* a new game during the wait drops it */
    fcs_char(&s, 'h');
    CHECK_EQ(f.req_open, 1);
    nc = f.ncancel;
    start_game(&s, &f, 3);
    CHECK_EQ(f.ncancel, nc + 1);
    CHECK_EQ(s.as.hint, FCS_HINT_IDLE);
    CHECK_EQ(f.req_open, 0);

    /* a hint waiting for the warning's search, the warning turned off meanwhile (Options), then a key
     * ends the hint: nobody needs the search any more, so it is cancelled too */
    s.extras.warn_unwinnable = 1;
    start_game(&s, &f, 4);
    CHECK_EQ(f.req_open, 1);                             /* the warning's check of the deal */
    fcs_char(&s, 'h');
    CHECK_EQ(s.as.hint, FCS_HINT_WAIT);                  /* waits for that search */
    s.extras.warn_unwinnable = 0;
    fcs_options_changed(&s);
    CHECK_EQ(f.req_open, 1);                             /* still wanted by the hint */
    nc = f.ncancel;
    fcs_char(&s, '5');
    CHECK_EQ(s.as.hint, FCS_HINT_IDLE);
    CHECK_EQ(f.ncancel, nc + 1);
    CHECK_EQ(f.req_open, 0);
    CHECK_EQ(s.as.req_pending, 0);
    fcs_free(&s);

    /* no solver at all */
    setup(&s, &f, 0);
    start_game(&s, &f, 1);
    fcs_char(&s, 'h');
    CHECK_EQ(f.nmsg, 1);
    CHECK_EQ(f.msg[0], FCS_STR_HINT_NONE);
    CHECK_EQ(s.as.hint, FCS_HINT_IDLE);
    fcs_free(&s);
}

/* Game #1 after its column 2's three bottom cards went to the free cells (9C 8S TD): unwinnable. */
static void lose_game1(FcSession *s, Fake *f, int npump)
{
    for (int i = 0; i < 3; i++) {
        fcs_click(s, 2, 0);
        fcs_click(s, 0, i);
        if (npump) CHECK_EQ(pump(s, f), i < 2 ? FC_SOLVE_SOLVED : FC_SOLVE_UNSOLVABLE);
    }
}

static void test_hint_unwinnable(void)
{
    FcSession s; Fake f;
    setup(&s, &f, 1);
    start_game(&s, &f, 1);
    lose_game1(&s, &f, 0);
    CHECK_EQ(f.nreq, 0);
    fcs_char(&s, 'h');
    CHECK_EQ(pump(&s, &f), FC_SOLVE_UNSOLVABLE);
    CHECK_EQ(f.nmsg, 1);
    CHECK_EQ(f.msg[0], FCS_STR_HINT_LOST);
    CHECK_EQ(s.as.hint, FCS_HINT_IDLE);
    fcs_char(&s, 'h');                                   /* known: at once */
    CHECK_EQ(f.nreq, 1);
    CHECK_EQ(f.nmsg, 2);
    CHECK_EQ(f.msg[1], FCS_STR_HINT_LOST);
    fcs_free(&s);
}

/* ---- Warning ---------------------------------------------------------------------------------------- */

static void test_warning(void)
{
    FcSession s; Fake f;
    setup(&s, &f, 1);
    s.extras.warn_unwinnable = 1;
    start_game(&s, &f, 1);
    CHECK_EQ(f.nreq, 1);                                 /* the deal is checked */
    CHECK_EQ(pump(&s, &f), FC_SOLVE_SOLVED);
    CHECK_EQ(f.nmsg, 0);

    /* a hint now comes from that search's solution */
    fcs_char(&s, 'h');
    CHECK_EQ(f.nreq, 1);
    CHECK_EQ(s.as.hint, FCS_HINT_FLASH);
    for (int i = 0; i < 8; i++) tick(&s, &f, FCS_TIMER_HINT);
    CHECK_EQ(s.as.hint, FCS_HINT_IDLE);

    lose_game1(&s, &f, 1);                               /* the third move loses the game */
    CHECK_EQ(f.nreq, 4);
    CHECK_EQ(f.nmsg, 1);
    CHECK_EQ(f.msg[0], FCS_STR_UNWINNABLE);
    CHECK_EQ(f.busy_seen, 1);
    CHECK_EQ(s.as.warned, 1);

    /* a hint here: known, at once */
    fcs_char(&s, 'h');
    CHECK_EQ(f.nreq, 4);
    CHECK_EQ(f.msg[1], FCS_STR_HINT_LOST);

    /* a further move: unwinnable without a search, and no second warning */
    FcBoard lost3 = s.board;
    fcs_click(&s, 1, 0);
    fcs_click(&s, 0, 3);                                 /* 1's bottom card to the last free cell */
    CHECK(!same_board(&s.board, &lost3));
    CHECK_EQ(f.nlose, 0);
    CHECK_EQ(f.nreq, 4);
    CHECK_EQ(f.req_open, 0);
    CHECK_EQ(f.nmsg, 2);

    /* Undo: still unwinnable (known), quiet; Undo again: winnable (the cached solution), the warning
     * re-arms; Redo into the unwinnable position warns again */
    fcs_command(&s, FCS_CMD_UNDO);
    CHECK(same_board(&s.board, &lost3));
    CHECK_EQ(f.nreq, 4);
    CHECK_EQ(f.nmsg, 2);
    CHECK_EQ(s.as.warned, 1);
    fcs_command(&s, FCS_CMD_UNDO);
    CHECK_EQ(f.nreq, 4);
    CHECK_EQ(s.as.warned, 0);
    CHECK_EQ(f.nmsg, 2);
    fcs_command(&s, FCS_CMD_REDO);
    CHECK_EQ(f.nreq, 4);
    CHECK_EQ(f.nmsg, 3);
    CHECK_EQ(f.msg[2], FCS_STR_UNWINNABLE);

    /* undo to the deal (the cache keeps one solution: the deal is searched again) and play the same
     * losing line again: warned again, without a search for the known unwinnable position */
    fcs_command(&s, FCS_CMD_UNDO);
    fcs_command(&s, FCS_CMD_UNDO);
    fcs_command(&s, FCS_CMD_UNDO);
    CHECK_EQ(s.nhist, 0);
    CHECK_EQ(f.nmsg, 3);
    CHECK_EQ(pump(&s, &f), FC_SOLVE_SOLVED);
    for (int i = 0; i < 3; i++) {
        fcs_click(&s, 2, 0);
        fcs_click(&s, 0, i);
        if (i < 2) CHECK_EQ(pump(&s, &f), FC_SOLVE_SOLVED);
    }
    CHECK_EQ(f.req_open, 0);
    CHECK_EQ(f.nmsg, 4);
    CHECK_EQ(f.msg[3], FCS_STR_UNWINNABLE);
    int nreq;

    /* a new game re-arms it; turning the warning off stops the searches */
    start_game(&s, &f, 2);
    CHECK_EQ(s.as.warned, 0);
    CHECK_EQ(f.req_open, 1);
    nreq = f.nreq;
    int nc = f.ncancel;
    s.extras.warn_unwinnable = 0;
    fcs_options_changed(&s);
    CHECK_EQ(f.ncancel, nc + 1);
    CHECK_EQ(f.req_open, 0);
    fcs_click(&s, 1, 0);
    fcs_click(&s, 0, 0);
    CHECK_EQ(f.nreq, nreq);
    fcs_free(&s);
}

/* A late answer for an older position is ignored; answers for the deal and a hint share a search. */
static void test_warning_async(void)
{
    FcSession s; Fake f;
    setup(&s, &f, 1);
    s.extras.warn_unwinnable = 1;
    start_game(&s, &f, 1);
    uint32_t deal_id = f.req_id;
    FcBoard deal_board = f.req_board;
    fcs_char(&s, 'h');                                   /* waits for the deal's search */
    CHECK_EQ(f.nreq, 1);
    CHECK_EQ(s.as.hint, FCS_HINT_WAIT);
    CHECK_EQ(pump(&s, &f), FC_SOLVE_SOLVED);
    CHECK_EQ(s.as.hint, FCS_HINT_FLASH);
    CHECK_EQ(f.nmsg, 0);

    /* move on before the next answer: the answer for the old position changes nothing */
    lose_game1(&s, &f, 0);                               /* three requests, each replacing the last */
    CHECK_EQ(f.nreq, 4);
    FcSolveResult r;
    fc_solve(sv, &deal_board, 0, NULL, &r);
    CHECK_EQ(fcs_solve_done(&s, deal_id, r.status, r.moves, r.nmoves), 1);
    CHECK_EQ(f.nmsg, 0);
    CHECK_EQ(pump(&s, &f), FC_SOLVE_UNSOLVABLE);
    CHECK_EQ(f.nmsg, 1);

    /* the supermove rule changes while a search runs: searched again under the new rule */
    start_game(&s, &f, 3);
    CHECK_EQ(f.req_std, 0);
    int nreq = f.nreq;
    s.extras.standard_supermove = 1;
    fcs_options_changed(&s);
    CHECK_EQ(f.nreq, nreq + 1);
    CHECK_EQ(f.req_std, 1);
    CHECK_EQ(pump(&s, &f), FC_SOLVE_SOLVED);
    fcs_char(&s, 'h');                                   /* cached for this rule */
    CHECK_EQ(f.nreq, nreq + 1);
    CHECK_EQ(s.as.hint, FCS_HINT_FLASH);
    s.extras.standard_supermove = 0;                     /* not for the other one */
    fcs_char(&s, 'h');
    CHECK_EQ(f.nreq, nreq + 2);
    CHECK_EQ(f.req_std, 0);
    fcs_free(&s);
}

/* The deal itself cannot be won (XP's game -1): the first move says so. */
static void test_warning_deal(void)
{
    FcSession s; Fake f;
    FcSolveMove mv[FC_SOLVE_MAX_MOVES];
    setup(&s, &f, 1);
    s.extras.warn_unwinnable = 1;
    start_game(&s, &f, -1);
    CHECK_EQ(pump(&s, &f), FC_SOLVE_UNSOLVABLE);
    CHECK_EQ(f.nmsg, 0);                                 /* nothing yet */
    CHECK_EQ(s.as.deal_lost, 1);
    int n = fc_solve_moves(&s.board, 0, mv), k = 0;
    while (k < n && mv[k].kind == FC_SM_AUTOPLAY) k++;
    CHECK(k < n);
    int nreq = f.nreq;
    play(&s, &f, &mv[k]);
    CHECK_EQ(f.nreq, nreq);                              /* derived: no search */
    CHECK_EQ(f.nmsg, 1);
    CHECK_EQ(f.msg[0], FCS_STR_UNWINNABLE_DEAL);
    fcs_free(&s);
}

/* ---- Finish ------------------------------------------------------------------------------------------ */

/* The first index of the solver's solution of deal g after which the rest is a sure win. */
static int sure_win_index(int g, FcSolveMove *sol, int *n)
{
    FcSolveResult r;
    FcBoard b;
    fc_deal(&b, g);
    if (fc_solve(sv, &b, 0, NULL, &r) != FC_SOLVE_SOLVED) return -1;
    memcpy(sol, r.moves, (size_t)r.nmoves * sizeof sol[0]);
    *n = r.nmoves;
    for (int i = 0; i < r.nmoves; i++) {
        if (fc_sure_win(&b, NULL, NULL)) return i;
        fc_solve_play(&b, &sol[i], 0);
    }
    return -1;
}

static void test_finish(void)
{
    FcSession s; Fake f;
    FcSolveMove sol[128];
    int n = 0, k = sure_win_index(31364, sol, &n);
    CHECK_EQ(k, 21);                                     /* the e2e scenario relies on it */

    /* by hand: grayed until the position is a sure win, then one counted action of single-card
     * flights home and the win */
    setup(&s, &f, 1);
    start_game(&s, &f, 31364);
    for (int i = 0; i < k; i++) {
        CHECK_EQ(f.finish_en, 0);
        CHECK_EQ(fcs_finish_enabled(&s), 0);
        if (i == 3) {                                    /* not a sure win: the command does nothing */
            int fwd = f.nfwd;
            fcs_command(&s, FCS_CMD_FINISH);
            CHECK_EQ(f.nfwd, fwd);
        }
        play(&s, &f, &sol[i]);
    }
    CHECK_EQ(f.nlose, 0);
    CHECK_EQ(f.finish_en, 1);
    CHECK_EQ(fcs_finish_enabled(&s), 1);
    CHECK_EQ(f.nwin, 0);
    int left = s.board.cards_left, moves = s.moves, fwd = f.nfwd;
    fcs_command(&s, FCS_CMD_UNDO);                       /* Undo: no longer a sure win */
    CHECK_EQ(f.finish_en, 0);
    fcs_command(&s, FCS_CMD_REDO);                       /* Redo: again (auto-finish is off) */
    CHECK_EQ(f.finish_en, 1);
    CHECK_EQ(f.nwin, 0);
    fwd = f.nfwd;
    moves = s.moves;
    fcs_char(&s, 'h');                                   /* a hint is shown ... */
    CHECK_EQ(pump(&s, &f), FC_SOLVE_SOLVED);
    CHECK_EQ(s.as.hint, FCS_HINT_FLASH);
    fcs_command(&s, FCS_CMD_FINISH);                     /* ... and ends with the command */
    CHECK_EQ(s.as.hint, FCS_HINT_IDLE);
    CHECK_EQ(f.nwin, 1);
    CHECK_EQ(s.board.cards_left, 0);
    CHECK_EQ(f.nfwd - fwd, left);                        /* one flight per card */
    CHECK_EQ(s.moves, moves + 1);                        /* one move */
    CHECK_EQ(f.finish_en, 0);
    CHECK_EQ(f.hint_en, 0);
    fcs_free(&s);

    /* automatically: right after the move that makes it a sure win; not a move of its own */
    setup(&s, &f, 1);
    s.extras.auto_finish = 1;
    start_game(&s, &f, 31364);
    for (int i = 0; i < k; i++) {
        CHECK_EQ(f.nwin, 0);
        play(&s, &f, &sol[i]);
    }
    CHECK_EQ(f.nwin, 1);
    CHECK_EQ(s.board.cards_left, 0);
    CHECK_EQ(s.moves, k);
    fcs_free(&s);

    /* with the warning on too: no search is started for the finished game */
    setup(&s, &f, 1);
    s.extras.auto_finish = 1;
    s.extras.warn_unwinnable = 1;
    start_game(&s, &f, 31364);
    pump(&s, &f);
    for (int i = 0; i < k; i++) {
        play(&s, &f, &sol[i]);
        pump(&s, &f);
    }
    CHECK_EQ(f.nwin, 1);
    CHECK_EQ(f.req_open, 0);
    CHECK_EQ(f.nmsg, 0);
    fcs_free(&s);

    /* Quick play: no flights, the same result */
    setup(&s, &f, 1);
    s.opts.quick = 1;                                    /* (the fake animates anyway; the UI skips) */
    start_game(&s, &f, 31364);
    for (int i = 0; i < k; i++) play(&s, &f, &sol[i]);
    fcs_command(&s, FCS_CMD_FINISH);
    CHECK_EQ(f.nwin, 1);
    fcs_free(&s);
}

/* Finish automatically after a move into a sure win whose only legal move (XP's count) is a card home
 * that autoplay does not take: XP's CheckNoMoves would start its "one move left" window flash, and it
 * ran on through the win. The game is won at once instead, without the flash; with the cheat's "lose"
 * armed the move still loses; with the option off the flash is XP's, and Finish by hand wins. */
static void test_auto_finish_one_move_left(void)
{
    /* KD (column 8) to the empty free cell leaves: free cells full, no empty column, exposed TC TD TH
     * TS QC QD QH QS; only TC can move (home; not safe with the red homes at 7) */
    static const char *const cols[8] = { "JC 9H 8H 9D 8D TC", "9S TD", "TH", "TS", "JD QC", "JH QD", "JS QH",
                                         "QS KD" };
    static const char *const fcs[4] = { "KC", "KS", "KH", NULL };
    static const char *const homes[4] = { "9C", "7D", "7H", "8S" };
    for (int mode = 0; mode < 3; mode++) {               /* option off, on, on with the cheat "lose" */
        FcSession s; Fake f;
        FcBoard b, t;
        make_board(&b, cols, fcs, homes);
        CHECK_EQ(b.cards_left, 21);
        t = b;
        fc_autoplay(&t, NULL, 0);
        CHECK(same_board(&t, &b));                       /* a position the session can show */
        fc_queue(&t, NULL, 8, 1, 0, 3);                  /* after the move: */
        fc_autoplay(&t, NULL, 0);
        CHECK_EQ(t.cards_left, 21);
        CHECK_EQ(fc_count_moves_xp(&t), 1);              /* one move left (XP) */
        CHECK_EQ(fc_sure_win(&t, NULL, NULL), 1);        /* and a sure win */
        setup(&s, &f, 1);
        s.extras.auto_finish = mode > 0;
        start_game(&s, &f, 1);
        s.board = b;
        if (mode == 2) s.cheat = FCS_CHEAT_LOSE;
        fcs_click(&s, 8, 1);
        fcs_click(&s, 0, 3);
        if (mode == 0) {
            CHECK_EQ(f.nwin, 0);
            CHECK_EQ(s.flash_left, 4);                   /* XP: one move left */
            CHECK_EQ(f.timer[FCS_TIMER_FLASH], 400);
            CHECK_EQ(f.finish_en, 1);
            fcs_command(&s, FCS_CMD_FINISH);
            CHECK_EQ(f.nwin, 1);
            CHECK_EQ(s.moves, 2);
        } else if (mode == 1) {
            CHECK_EQ(f.nwin, 1);
            CHECK_EQ(s.board.cards_left, 0);
            CHECK_EQ(s.moves, 1);                        /* the finish is not a move */
            CHECK_EQ(s.flash_left, 0);                   /* no "one move left" for a won game */
            CHECK_EQ(f.timer[FCS_TIMER_FLASH], 0);
            CHECK_EQ(f.nflash, 0);
        } else {
            CHECK_EQ(f.nwin, 0);
            CHECK_EQ(f.nlose, 1);
            CHECK_EQ(s.board.cards_left, 21);
        }
        fcs_free(&s);
    }
}

/* ---- Store ------------------------------------------------------------------------------------------ */

typedef struct Reg { const char *name[16]; uint32_t v[16]; int n; } Reg;
static int reg_get(void *c, const char *name, uint32_t *v)
{
    Reg *r = c;
    for (int i = 0; i < r->n; i++) if (!strcmp(r->name[i], name)) { *v = r->v[i]; return 1; }
    return 0;
}
static void reg_set(void *c, const char *name, uint32_t v)
{
    Reg *r = c;
    for (int i = 0; i < r->n; i++) if (!strcmp(r->name[i], name)) { r->v[i] = v; return; }
    r->name[r->n] = name;
    r->v[r->n++] = v;
}

static void test_store(void)
{
    Reg r = { { 0 }, { 0 }, 0 };
    CeStore st = { &r, reg_get, reg_set, NULL, NULL, NULL };
    FcExtras x;
    uint32_t v = 9;
    fc_extras_load(&x, &st);
    CHECK_EQ(x.warn_unwinnable, 0);
    CHECK_EQ(x.auto_finish, 0);
    x.warn_unwinnable = 1;
    fc_extras_save(&x, &st);
    CHECK(reg_get(&r, "WarnUnwinnable", &v) && v == 1);
    CHECK(reg_get(&r, "AutoFinish", &v) && v == 0);
    reg_set(&r, "AutoFinish", 5);
    fc_extras_load(&x, &st);
    CHECK_EQ(x.warn_unwinnable, 1);
    CHECK_EQ(x.auto_finish, 1);
}

int main(void)
{
    sv = fc_solver_new(0);
    if (!sv) { printf("test_assist: out of memory\n"); return 1; }
    test_hint_basic();
    test_hint_cycle();
    test_hint_cancel_and_failures();
    test_hint_unwinnable();
    test_warning();
    test_warning_async();
    test_warning_deal();
    test_finish();
    test_auto_finish_one_move_left();
    test_store();
    fc_solver_free(sv);
    printf("test_assist: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
