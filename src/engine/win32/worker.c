/*
 * Card engine, Win32 — one background worker thread (see worker.h).
 */
#include "worker.h"
#include "winutil.h"

#include <string.h>

void ce_worker_init(CeWorker *w, const CeWorkerDef *def, HWND hwnd, UINT msg)
{
    memset(w, 0, sizeof *w);
    w->def = def;
    w->hwnd = hwnd;
    w->msg = msg;
}

static void job_free(CeWorker *w, CeJob *j)
{
    if (j)
        w->def->job_free(j);
}

static DWORD WINAPI worker_main(LPVOID param)
{
    CeWorker *w = param;
    void *state = NULL;
    for (;;) {
        CeJob *j;
        EnterCriticalSection(&w->lock);
        while (!w->quit && !w->next) {
            LeaveCriticalSection(&w->lock);
            WaitForSingleObject(w->wake, INFINITE);
            EnterCriticalSection(&w->lock);
        }
        if (w->quit) {
            LeaveCriticalSection(&w->lock);
            break;
        }
        j = w->next;
        w->next = NULL;
        w->running = j;
        LeaveCriticalSection(&w->lock);

        w->def->run(j, &state);

        EnterCriticalSection(&w->lock);
        w->running = NULL;
        LeaveCriticalSection(&w->lock);
        /* a discarded (e.g. cancelled) job is of no use to anyone; the window may be gone at exit */
        if ((w->def->discard && w->def->discard(j)) || !PostMessageW(w->hwnd, w->msg, 0, (LPARAM)j))
            job_free(w, j);
    }
    if (w->def->state_free)
        w->def->state_free(state);
    return 0;
}

static int start(CeWorker *w)
{
    DWORD tid;
    if (w->thread)
        return 1;
    if (!w->lock_ok) {
        InitializeCriticalSection(&w->lock);
        w->lock_ok = 1;
    }
    if (!w->wake && !(w->wake = CreateEventW(NULL, FALSE, FALSE, NULL)))
        return 0;
    w->quit = 0;
    w->thread = CreateThread(NULL, 0, worker_main, w, 0, &tid);
    if (!w->thread)
        return 0;
    SetThreadPriority(w->thread, w->def->priority);
    ce_log("%s thread started", w->def->name);
    return 1;
}

int ce_worker_submit(CeWorker *w, CeJob *job)
{
    CeJob *old;
    if (!start(w))
        return 0;
    EnterCriticalSection(&w->lock);
    old = w->next;
    w->next = job;
    if (w->running)
        w->running->cancel = 1;
    LeaveCriticalSection(&w->lock);
    job_free(w, old);                         /* never started */
    SetEvent(w->wake);
    return 1;
}

void ce_worker_cancel(CeWorker *w)
{
    CeJob *old;
    if (!w->thread)
        return;
    EnterCriticalSection(&w->lock);
    old = w->next;
    w->next = NULL;
    if (w->running)
        w->running->cancel = 1;
    LeaveCriticalSection(&w->lock);
    job_free(w, old);
}

void ce_worker_shutdown(CeWorker *w, DWORD timeout_ms)
{
    if (!w->thread)
        return;
    EnterCriticalSection(&w->lock);
    w->quit = 1;
    job_free(w, w->next);
    w->next = NULL;
    if (w->running)
        w->running->cancel = 1;
    LeaveCriticalSection(&w->lock);
    SetEvent(w->wake);
    if (WaitForSingleObject(w->thread, timeout_ms) == WAIT_OBJECT_0) {
        CloseHandle(w->thread);
        w->thread = NULL;
        CloseHandle(w->wake);
        w->wake = NULL;
        DeleteCriticalSection(&w->lock);
        w->lock_ok = 0;
    }                                         /* else the process exit ends it */
    ce_log("%s thread stopped", w->def->name);
}
