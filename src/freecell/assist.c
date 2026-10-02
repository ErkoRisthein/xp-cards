/*
 * FreeCell HD — Hint, the unwinnable warning and the solution cache (v1.2 extras, ROADMAP §1b).
 *
 * One background search at a time, for the current position: the session asks the UI to solve a copy
 * of the board (solve_start, tagged with an id) and the answer comes back through fcs_solve_done.
 * Every change of position replaces or drops the request, so an answer with an older id is simply
 * ignored. Hint and the warning share the request: a hint asked while the warning's search for the
 * same position runs waits for that search.
 *
 * What a search finds is kept: the last solution (the board before each of its moves) and the last
 * FCS_LOST_RING positions proven unwinnable, each for its supermove rule. A position on the cached
 * solution is winnable and its hint is the solution's next move; a position reached by a move from an
 * unwinnable one is unwinnable too (were it winnable, so would be the one before), so the warning
 * needs no search there.
 *
 * Hint cycling (2c): the hint shown last stays current until other input or a change of position;
 * Hint again then shows the next move of a ranked list: the solver's move first, then every other
 * action the session accepts, best first by the solver's estimate of the position it leads to, those
 * into a position known to be unwinnable left out; wrapping back to the solver's move.
 */
#include "assist.h"

#include <stdlib.h>
#include <string.h>

#define UI(s, f) ((s)->ui.f)

static void invalidate(FcSession *s) { if (UI(s, invalidate)) UI(s, invalidate)(UI(s, ctx)); }
static void set_timer(FcSession *s, int id, int ms) { if (UI(s, set_timer)) UI(s, set_timer)(UI(s, ctx), id, ms); }
static int active(const FcSession *s) { return s->game_number != 0 && s->in_progress; }
static int rule(const FcSession *s) { return s->extras.standard_supermove != 0; }
static int same(const FcBoard *x, const FcBoard *y) { return memcmp(x, y, sizeof *x) == 0; }

static void message(FcSession *s, int id)       /* an information box, as session.c show_message */
{
    s->busy++;
    if (UI(s, message)) UI(s, message)(UI(s, ctx), id, fcs_string(id));
    s->busy--;
}

/* ---- Queries ------------------------------------------------------------------------------------- */

int fcs_hint_enabled(const FcSession *s) { return active(s); }
int fcs_hint_waiting(const FcSession *s) { return s->as.hint == FCS_HINT_WAIT; }

int fcs_finish_enabled(const FcSession *s)
{
    return active(s) && s->board.cards_left > 0 && fc_sure_win(&s->board, NULL, NULL);
}

void fcs_assist_view(const FcSession *s, int *col, int *pos)
{
    const FcAssist *as = &s->as;
    *col = *pos = -1;
    if (as->hint != FCS_HINT_FLASH || (as->hint_step & 1)) return;
    if (as->hint_step < 4) { *col = as->hint_src_col; *pos = as->hint_src_pos; }
    else { *col = as->hint_dst_col; *pos = as->hint_dst_pos; }
}

/* ---- Cache --------------------------------------------------------------------------------------- */

static void sol_clear(FcAssist *as)
{
    free(as->sol_boards);
    free(as->sol);
    as->sol_boards = NULL;
    as->sol = NULL;
    as->sol_n = 0;
}

void fcs_assist_free(FcSession *s) { sol_clear(&s->as); }

/* Keep a solution from start: the moves and the board before each (replayed as the session plays). */
static void sol_store(FcSession *s, const FcBoard *start, int std, const FcSolveMove *mv, int n)
{
    FcAssist *as = &s->as;
    sol_clear(as);
    if (n <= 0 || !mv) return;
    FcBoard *bs = malloc((size_t)(n + 1) * sizeof *bs);
    FcSolveMove *m = malloc((size_t)n * sizeof *m);
    if (!bs || !m) { free(bs); free(m); return; }
    bs[0] = *start;
    for (int i = 0; i < n; i++) {
        m[i] = mv[i];
        bs[i + 1] = bs[i];
        if (!fc_solve_play(&bs[i + 1], &m[i], std)) { free(bs); free(m); return; }   /* cannot happen */
    }
    as->sol_boards = bs;
    as->sol = m;
    as->sol_n = n;
    as->sol_std = std;
}

/* The index of the cached solution's move to play from b, or -1. */
static int sol_find(const FcSession *s, const FcBoard *b)
{
    const FcAssist *as = &s->as;
    if (!as->sol || as->sol_std != rule(s)) return -1;
    for (int i = 0; i < as->sol_n; i++)
        if (same(&as->sol_boards[i], b)) return i;
    return -1;
}

static int lost_find(const FcSession *s, const FcBoard *b)
{
    for (int i = 0; i < s->as.nlost; i++)
        if (s->as.lost_std[i] == rule(s) && same(&s->as.lost[i], b)) return 1;
    return 0;
}

static void lost_add(FcSession *s, const FcBoard *b, int std)
{
    FcAssist *as = &s->as;
    for (int i = 0; i < as->nlost; i++)
        if (as->lost_std[i] == std && same(&as->lost[i], b)) return;
    as->lost[as->lost_next] = *b;
    as->lost_std[as->lost_next] = std;
    as->lost_next = (as->lost_next + 1) % FCS_LOST_RING;
    if (as->nlost < FCS_LOST_RING) as->nlost++;
}

/* FC_SOLVE_SOLVED or FC_SOLVE_UNSOLVABLE when the cache knows b, else -1. */
static int known(const FcSession *s, const FcBoard *b)
{
    if (sol_find(s, b) >= 0) return FC_SOLVE_SOLVED;
    if (lost_find(s, b)) return FC_SOLVE_UNSOLVABLE;
    return -1;
}

/* ---- The background search ------------------------------------------------------------------------ */

static void req_drop(FcSession *s)
{
    if (!s->as.req_pending) return;
    s->as.req_pending = 0;
    if (UI(s, solve_cancel)) UI(s, solve_cancel)(UI(s, ctx));
}

/* Ask the UI to solve the current position (replacing any earlier request). 0 = there is no solver. */
static int req_start(FcSession *s, int cause)
{
    FcAssist *as = &s->as;
    as->req_pending = 0;
    if (!UI(s, solve_start)) return 0;
    if (++as->req_id == 0) as->req_id = 1;
    as->req_pending = 1;
    as->req_cause = cause;
    as->req_std = rule(s);
    as->req_board = s->board;
    UI(s, solve_start)(UI(s, ctx), as->req_id, &as->req_board, as->req_std);
    return 1;
}

/* ---- Hint ------------------------------------------------------------------------------------------- */

/* Stop showing or waiting for a hint (the search itself is the caller's business). */
static void hint_end(FcSession *s)
{
    FcAssist *as = &s->as;
    if (as->hint == FCS_HINT_FLASH) {
        set_timer(s, FCS_TIMER_HINT, 0);
        invalidate(s);
    } else if (as->hint == FCS_HINT_WAIT) {
        set_timer(s, FCS_TIMER_HINT_WAIT, 0);
    }
    as->hint = FCS_HINT_IDLE;
}

/* Flash move m on the current board: the cards it moves (a column's run from its first card, a free
 * cell), then where they go (the home or free cell, the destination column's exposed card, the empty
 * column's slot; a bare "click it again" shows the card twice over). */
static void hint_flash(FcSession *s, const FcSolveMove *m)
{
    FcAssist *as = &s->as;
    const FcBoard *b = &s->board;
    as->hint_move = *m;
    as->hint_src_col = m->src_col;
    as->hint_src_pos = m->src_pos;
    if (m->src_col >= 1 && m->src_col <= 8) {
        int last = fc_last_index(b, m->src_col), k = m->ncards > 1 ? m->ncards : 1;
        as->hint_src_pos = last - k + 1 > 0 ? last - k + 1 : 0;
    }
    as->hint_dst_col = m->dst_col;
    switch (m->kind) {
    case FC_SM_HOME: case FC_SM_FREECELL: as->hint_dst_pos = m->dst_pos; break;
    case FC_SM_COLUMN: as->hint_dst_pos = fc_last_index(b, m->dst_col); break;
    case FC_SM_EMPTY: as->hint_dst_pos = -1; break;
    default:
        as->hint_dst_col = m->src_col;
        as->hint_dst_pos = m->src_col == 0 ? m->src_pos : fc_last_index(b, m->src_col);
        break;
    }
    as->hint = FCS_HINT_FLASH;
    as->hint_step = 0;
    set_timer(s, FCS_TIMER_HINT, FCS_HINT_STEP_MS);
    invalidate(s);
}

/* ---- Hint cycling (2c) ------------------------------------------------------------------------------ */

/* The solver's move m was just shown for the current position: the cycle starts with it. */
static void cyc_start(FcSession *s, const FcSolveMove *m)
{
    FcAssist *as = &s->as;
    as->cyc_on = 1;
    as->cyc_idx = 0;
    as->cyc_n = 0;
    as->cyc_std = rule(s);
    as->cyc_board = s->board;
    as->cyc[0] = *m;
}

/* Hint again while the cycle is current: the next move (the alternatives are ranked on the first
 * repeat). NULL when it is not current. */
static const FcSolveMove *cyc_next(FcSession *s)
{
    FcAssist *as = &s->as;
    if (!as->cyc_on || as->cyc_std != rule(s) || !same(&as->cyc_board, &s->board)) return NULL;
    if (as->cyc_n == 0) {
        FcSolveMove mv[FC_SOLVE_MAX_MOVES];
        int est[FC_SOLVE_MAX_MOVES + 1], n, k = 1;
        FcBoard first = s->board;
        if (!fc_solve_play(&first, &as->cyc[0], rule(s))) return NULL;   /* cannot happen */
        n = fc_solve_moves(&s->board, rule(s), mv);
        for (int i = 0; i < n; i++) {
            FcBoard b = s->board;
            int e, j;
            if (mv[i].kind == FC_SM_AUTOPLAY || !fc_solve_play(&b, &mv[i], rule(s))) continue;
            if (same(&b, &first) || same(&b, &s->board) || lost_find(s, &b)) continue;
            e = fc_solve_estimate(&b);
            for (j = k; j > 1 && est[j - 1] > e; j--) {   /* stable: equal estimates keep their order */
                as->cyc[j] = as->cyc[j - 1];
                est[j] = est[j - 1];
            }
            as->cyc[j] = mv[i];
            est[j] = e;
            k++;
        }
        as->cyc_n = k;
    }
    as->cyc_idx = (as->cyc_idx + 1) % as->cyc_n;
    return &as->cyc[as->cyc_idx];
}

void fcs_assist_hint(FcSession *s)
{
    FcAssist *as = &s->as;
    if (s->busy || s->kbd_peek || !active(s) || as->hint == FCS_HINT_WAIT) return;
    hint_end(s);                                     /* pressed again while flashing: the next one */
    if (s->sel) {                                    /* the hint leaves nothing selected */
        s->sel = 0;
        s->sel_col = s->sel_pos = -1;
        invalidate(s);
    }
    const FcSolveMove *next = cyc_next(s);
    if (next) { hint_flash(s, next); return; }
    as->cyc_on = 0;
    int i = sol_find(s, &s->board);
    if (i >= 0) {
        hint_flash(s, &as->sol[i]);
        cyc_start(s, &as->sol[i]);
        return;
    }
    if (lost_find(s, &s->board)) { message(s, FCS_STR_HINT_LOST); return; }
    if (!(as->req_pending && as->req_std == rule(s) && same(&as->req_board, &s->board)) &&
        !req_start(s, FCS_AS_HINT)) {
        message(s, FCS_STR_HINT_NONE);              /* no solver */
        return;
    }
    as->hint = FCS_HINT_WAIT;                        /* a search runs (ours, or the warning's) */
    set_timer(s, FCS_TIMER_HINT_WAIT, FCS_HINT_TIMEOUT_MS);
}

void fcs_assist_input(FcSession *s)
{
    FcAssist *as = &s->as;
    int waiting = as->hint == FCS_HINT_WAIT;
    as->cyc_on = 0;                                  /* other input: the next Hint starts from the best */
    if (as->hint == FCS_HINT_IDLE) return;
    hint_end(s);
    if (waiting && as->req_pending && (as->req_cause == FCS_AS_HINT || !s->extras.warn_unwinnable))
        req_drop(s);                                 /* nobody else needs it (the warning may be off by now) */
}

void fcs_assist_timer(FcSession *s, int id)
{
    FcAssist *as = &s->as;
    if (id == FCS_TIMER_HINT) {
        if (as->hint != FCS_HINT_FLASH) { set_timer(s, id, 0); return; }
        if (++as->hint_step >= 8) hint_end(s);
        else invalidate(s);
    } else if (id == FCS_TIMER_HINT_WAIT) {
        if (as->hint != FCS_HINT_WAIT) { set_timer(s, id, 0); return; }
        if (s->busy) return;                         /* a dialog is up: the timer comes again */
        hint_end(s);
        req_drop(s);                                 /* too slow: give up */
        message(s, FCS_STR_HINT_NONE);
    }
}

/* ---- Warning ------------------------------------------------------------------------------------------ */

/* What is now known about the current position, and why it was looked at. */
static void warn(FcSession *s, int status, int cause)
{
    FcAssist *as = &s->as;
    if (status == FC_SOLVE_SOLVED) { as->warned = 0; return; }
    if (status != FC_SOLVE_UNSOLVABLE) return;      /* gave up: nothing is known */
    if (cause == FCS_AS_DEAL) as->deal_lost = 1;
    if (cause != FCS_AS_MOVE || as->warned || !s->extras.warn_unwinnable) return;
    as->warned = 1;
    message(s, as->deal_lost ? FCS_STR_UNWINNABLE_DEAL : FCS_STR_UNWINNABLE);
}

void fcs_assist_changed(FcSession *s, int cause, const FcBoard *before)
{
    FcAssist *as = &s->as;
    hint_end(s);
    as->cyc_on = 0;
    if (cause == FCS_AS_DEAL) {
        as->warned = 0;
        as->deal_lost = 0;
        as->deals++;
    }
    if (!active(s)) { req_drop(s); return; }
    int k = known(s, &s->board);
    if (k < 0 && cause == FCS_AS_MOVE && before && lost_find(s, before)) {
        lost_add(s, &s->board, rule(s));             /* a move from an unwinnable position */
        k = FC_SOLVE_UNSOLVABLE;
    }
    if (!s->extras.warn_unwinnable) { req_drop(s); return; }
    if (k >= 0) {
        req_drop(s);
        warn(s, k, cause);
    } else {
        req_start(s, cause);                         /* replaces the request for the old position */
    }
}

void fcs_assist_stop(FcSession *s)
{
    hint_end(s);
    s->as.cyc_on = 0;
    req_drop(s);
}

int fcs_solve_done(FcSession *s, uint32_t id, int status, const FcSolveMove *moves, int n)
{
    FcAssist *as = &s->as;
    if (s->busy) return 0;
    if (!as->req_pending || id != as->req_id) return 1;   /* superseded or cancelled */
    as->req_pending = 0;
    int cause = as->req_cause;
    if (status == FC_SOLVE_SOLVED && n > 0) sol_store(s, &as->req_board, as->req_std, moves, n);
    else if (status == FC_SOLVE_UNSOLVABLE) lost_add(s, &as->req_board, as->req_std);
    if (!active(s) || as->req_std != rule(s) || !same(&as->req_board, &s->board)) {
        hint_end(s);                                 /* cannot happen: every change replaces it */
        return 1;
    }
    if (as->hint != FCS_HINT_WAIT) {
        warn(s, status, cause);
        return 1;
    }
    hint_end(s);
    if (status == FC_SOLVE_SOLVED && n > 0) {
        as->warned = 0;
        hint_flash(s, &moves[0]);
        cyc_start(s, &moves[0]);
    } else if (status == FC_SOLVE_UNSOLVABLE) {
        if (cause == FCS_AS_DEAL) as->deal_lost = 1;
        if (s->extras.warn_unwinnable) as->warned = 1;   /* this message says it already */
        message(s, FCS_STR_HINT_LOST);
    } else {
        message(s, FCS_STR_HINT_NONE);
    }
    return 1;
}

void fcs_options_changed(FcSession *s)
{
    FcAssist *as = &s->as;
    if (!as->req_pending) return;
    int hint = as->hint == FCS_HINT_WAIT;
    if (as->req_std != rule(s)) {                    /* the supermove rule changed: search again */
        int cause = as->req_cause;
        req_drop(s);
        if (!hint && !s->extras.warn_unwinnable) return;
        int k = known(s, &s->board);
        if (k < 0 && req_start(s, cause)) return;
        if (hint) {                                  /* the cache knows (or there is no solver) */
            hint_end(s);
            fcs_assist_hint(s);
        } else {
            warn(s, k, cause);
        }
    } else if (!hint && !s->extras.warn_unwinnable) {
        req_drop(s);                                 /* only the warning wanted it */
    }
}
