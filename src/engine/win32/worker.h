/*
 * Card engine, Win32 — one background worker thread for long computations (FreeCell HD's solver).
 *
 * One job at a time: a newer request replaces a job not yet started (it is freed) and sets the
 * running job's cancel flag, which the job's code reads often. The thread is created on first use at
 * a chosen priority (below normal keeps the UI thread and its animations first on a single-core XP
 * machine) and owns a per-thread state made lazily by the job code (e.g. a solver's big tables,
 * allocated once). A finished job is posted to the window as message msg with the job in lParam
 * (unless the job says it is to be discarded, or the window is gone: then it is freed), and the
 * window's thread takes it from there (and frees it). A job carries its own copies of whatever it
 * needs: the worker never touches the game's state.
 */
#ifndef CE_WORKER_H
#define CE_WORKER_H

#include <windows.h>

/* Every job starts with this (the game's job struct embeds it as its first member). */
typedef struct CeJob {
    volatile int cancel;        /* set by the UI thread: stop as soon as possible */
} CeJob;

typedef struct CeWorkerDef {
    const char *name;                                  /* for the log: "<name> thread started" */
    int         priority;                              /* SetThreadPriority, e.g. THREAD_PRIORITY_BELOW_NORMAL */
    void      (*run)(CeJob *job, void **state);        /* worker thread: do the job; *state persists */
    void      (*state_free)(void *state);              /* worker thread, when it quits (may be NULL) */
    void      (*job_free)(CeJob *job);
    int       (*discard)(const CeJob *job);            /* after run: 1 = free it instead of posting (may be NULL) */
} CeWorkerDef;

typedef struct CeWorker {
    const CeWorkerDef *def;
    HWND             hwnd;
    UINT             msg;
    HANDLE           thread, wake;      /* wake: auto-reset event, "a job or quit" */
    CRITICAL_SECTION lock;              /* guards next, running and quit */
    int              lock_ok;
    CeJob           *next, *running;
    int              quit;
} CeWorker;

/* Set up (no thread yet): results go to hwnd as msg. */
void ce_worker_init(CeWorker *w, const CeWorkerDef *def, HWND hwnd, UINT msg);

/* Queue job (replacing a pending one, cancelling the running one). Returns 0 if the thread cannot be
 * started: the job was not taken (the caller still owns it). */
int  ce_worker_submit(CeWorker *w, CeJob *job);

/* Drop the pending job and cancel the running one (its result, if posted, is the caller's to ignore). */
void ce_worker_cancel(CeWorker *w);

/* WM_DESTROY: cancel everything and join the thread (up to timeout_ms; past that the process exit ends
 * it). */
void ce_worker_shutdown(CeWorker *w, DWORD timeout_ms);

#endif
