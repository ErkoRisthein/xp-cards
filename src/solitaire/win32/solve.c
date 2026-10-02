/*
 * Solitaire HD — the background solver of the "Warn when the game can't be won" extra (FreeCell HD's
 * pattern, src/freecell/win32/solve.c).
 *
 * One worker thread (the engine's CeWorker), created on first use at below-normal priority, owns the
 * full-information solver (sol_solver_new once: about 10 MiB) and solves one position at a time. The
 * session asks through ui.solve_start / solve_cancel; a job carries its own copy of the board, so the
 * worker never touches the session. A newer request replaces a job not yet started and cancels the
 * running one (sol_solve reads the flag before every expansion).
 *
 * A finished job is posted to the window (WM_APP_SOLVED, the job in lParam) and handed to
 * sol_solve_done on the window's thread, but only when no modal dialog is up and the session is not
 * busy or dragging cards (sol_solve_done refuses it then): until then it is held in a->solve_ready and
 * retried after the next input (the drop) or when a dialog closes.
 */
#include "app.h"

#include <stdlib.h>
#include <string.h>

#include "solitaire/solver.h"

typedef struct SolveJob {
    CeJob    base;                       /* the cancel flag */
    uint32_t id;
    SolBoard board;
    int      draw, left;
    int      status;
    uint32_t nodes;
    DWORD    ms;
} SolveJob;

static CeWorker W;
static int      W_ready;

static const char *status_name(int st)
{
    switch (st) {
    case SOL_SOLVE_SOLVED: return "solved";
    case SOL_SOLVE_UNSOLVABLE: return "unsolvable";
    case SOL_SOLVE_CANCELLED: return "cancelled";
    case SOL_SOLVE_INVALID: return "invalid";
    default: return "gave up";
    }
}

static void run(CeJob *job, void **state)
{
    SolveJob *j = (SolveJob *)job;
    SolSolveResult r;
    DWORD t0 = GetTickCount();
    if (!*state)
        *state = sol_solver_new(0);
    if (!*state) {
        j->status = SOL_SOLVE_GAVE_UP;       /* out of memory: nothing is known */
    } else {
        sol_solve(*state, &j->board, j->draw, j->left, &j->base.cancel, &r);
        j->status = r.status;
        j->nodes = r.nodes;
    }
    j->ms = GetTickCount() - t0;
}

static void solver_free(void *state) { sol_solver_free(state); }
static void free_job(CeJob *job) { free(job); }
static int discard(const CeJob *job) { return ((const SolveJob *)job)->status == SOL_SOLVE_CANCELLED; }

static const CeWorkerDef solver_def = {
    "solver", THREAD_PRIORITY_BELOW_NORMAL, run, solver_free, free_job, discard
};

void solver_request(App *a, uint32_t id, const SolBoard *b, int draw, int left)
{
    SolveJob *j = calloc(1, sizeof *j);
    if (!j)
        return;                              /* out of memory: no warning */
    j->id = id;
    j->board = *b;
    j->draw = draw;
    j->left = left;
    if (!W_ready) {
        ce_worker_init(&W, &solver_def, a->hwnd, WM_APP_SOLVED);
        W_ready = 1;
    }
    if (!ce_worker_submit(&W, &j->base))
        free(j);                             /* no thread: the warning stays silent */
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
        ce_log("solve #%u: %s, %u nodes, %lu ms", (unsigned)j->id, status_name(j->status), (unsigned)j->nodes,
               (unsigned long)j->ms);
        free(a->solve_ready);                /* an older answer never delivered: superseded */
        a->solve_ready = j;
    }
    solver_deliver(a);
}

void solver_deliver(App *a)
{
    SolveJob *j = a->solve_ready;
    if (!j || a->in_modal || a->s.busy)
        return;
    a->solve_ready = NULL;                   /* sol_solve_done may show a message box */
    if (!sol_solve_done(&a->s, j->id, j->status)) {
        if (!a->solve_ready)
            a->solve_ready = j;              /* busy after all: later */
        else
            free(j);
        return;
    }
    free(j);
    view_sync(a);
}

void solver_shutdown(App *a)
{
    if (W_ready)
        ce_worker_shutdown(&W, 5000);
    W_ready = 0;
    free(a->solve_ready);
    a->solve_ready = NULL;
}
