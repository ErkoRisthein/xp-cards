/*
 * FreeCell HD — the background solver for the v1.2 extras (Hint, the unwinnable warning).
 *
 * One worker thread (the engine's, engine/win32/worker.h), created on first use at below-normal
 * priority (the UI thread and its card flights come first on a single-core XP machine), owns the
 * FcSolver (allocated once by the worker, about 9 MiB) and solves one job at a time. The session
 * asks through solve_start / solve_cancel (ui.c); a job carries its own copy of the board, so the
 * worker never touches the session. A newer request replaces a job not yet started and cancels the
 * running one (fc_solve reads the flag before every expansion, so it stops within microseconds).
 *
 * A finished job is posted to the window (WM_APP_SOLVED, the job in lParam) and handed to
 * fcs_solve_done on the window's thread, but only when no session call and no modal dialog is in
 * progress (a modal loop dispatches posted messages too): until then it is held in a->solve_ready and
 * retried after the next input (main.c after_input) or when the dialog closes (modal_end).
 */
#include "app.h"

#include <stdlib.h>
#include <string.h>

typedef struct SolveJob {
    CeJob        base;                    /* the cancel flag */
    uint32_t     id;
    int          std;
    FcBoard      board;
    int          status, nmoves;
    FcSolveMove *moves;                   /* malloc'ed copy of the solution */
    uint32_t     nodes;
    DWORD        ms;
} SolveJob;

static CeWorker W;
static int      W_ready;

static void job_free(SolveJob *j)
{
    if (j) {
        free(j->moves);
        free(j);
    }
}

static const char *status_name(int st)
{
    switch (st) {
    case FC_SOLVE_SOLVED: return "solved";
    case FC_SOLVE_UNSOLVABLE: return "unsolvable";
    case FC_SOLVE_CANCELLED: return "cancelled";
    default: return "gave up";
    }
}

/* ---- the worker's side ------------------------------------------------------------------------- */

static void run(CeJob *job, void **state)
{
    SolveJob *j = (SolveJob *)job;
    FcSolveResult r;
    DWORD t0 = GetTickCount();
    if (!*state)
        *state = fc_solver_new(0);
    if (!*state) {
        j->status = FC_SOLVE_GAVE_UP;         /* out of memory: no hint */
    } else {
        fc_solve(*state, &j->board, j->std, &j->base.cancel, &r);
        j->status = r.status;
        j->nodes = r.nodes;
        if (r.status == FC_SOLVE_SOLVED && r.nmoves > 0) {
            j->moves = malloc((size_t)r.nmoves * sizeof *j->moves);
            if (j->moves) {
                memcpy(j->moves, r.moves, (size_t)r.nmoves * sizeof *j->moves);
                j->nmoves = r.nmoves;
            } else {
                j->status = FC_SOLVE_GAVE_UP;
            }
        }
    }
    j->ms = GetTickCount() - t0;
}

static void solver_free(void *state) { fc_solver_free(state); }

static void free_job(CeJob *job) { job_free((SolveJob *)job); }

/* a cancelled job is of no use to anyone */
static int discard(const CeJob *job) { return ((const SolveJob *)job)->status == FC_SOLVE_CANCELLED; }

static const CeWorkerDef solver_def = {
    "solver", THREAD_PRIORITY_BELOW_NORMAL, run, solver_free, free_job, discard
};

/* ---- the UI thread's side ---------------------------------------------------------------------- */

void solver_request(App *a, uint32_t id, const FcBoard *b, int standard)
{
    SolveJob *j = calloc(1, sizeof *j);
    if (!j)
        return;                               /* out of memory: a hint times out ("No hint ...") */
    j->id = id;
    j->std = standard;
    j->board = *b;
    if (!W_ready) {
        ce_worker_init(&W, &solver_def, a->hwnd, WM_APP_SOLVED);
        W_ready = 1;
    }
    if (!ce_worker_submit(&W, &j->base)) {    /* no thread: answer "gave up" right away */
        j->status = FC_SOLVE_GAVE_UP;
        if (!a->hwnd || !PostMessageW(a->hwnd, WM_APP_SOLVED, 0, (LPARAM)j))
            job_free(j);
    }
}

void solver_cancel(App *a)
{
    (void)a;
    if (W_ready)
        ce_worker_cancel(&W);
}

void solver_received(App *a, LPARAM lp)
{
    SolveJob *j = (SolveJob *)lp;
    if (j) {
        ce_log("solve #%u: %s, %d moves, %u nodes, %lu ms", (unsigned)j->id, status_name(j->status),
               j->nmoves, (unsigned)j->nodes, (unsigned long)j->ms);
        job_free(a->solve_ready);             /* an older answer never delivered: superseded */
        a->solve_ready = j;
    }
    solver_deliver(a);
}

void solver_deliver(App *a)
{
    SolveJob *j = a->solve_ready;
    if (!j || a->in_modal || a->s.busy)
        return;
    a->solve_ready = NULL;                    /* fcs_solve_done may show a message box */
    if (!fcs_solve_done(&a->s, j->id, j->status, j->moves, j->nmoves)) {
        if (!a->solve_ready)
            a->solve_ready = j;               /* the session is busy after all: later */
        else
            job_free(j);
        return;
    }
    job_free(j);
    ce_anim_idle(&a->anim);
    view_sync(a);                             /* the hint's first flash step */
    view_refresh_cursor(a);                   /* APPSTARTING ends */
}

void solver_shutdown(App *a)
{
    if (W_ready)
        ce_worker_shutdown(&W, 5000);
    job_free(a->solve_ready);
    a->solve_ready = NULL;
}
