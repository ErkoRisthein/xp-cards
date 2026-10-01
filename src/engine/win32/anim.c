/*
 * Card engine, Win32 — frame-timed animation (see anim.h).
 */
#include "anim.h"

#include <mmsystem.h>
#include <math.h>

void ce_anim_begin(CeAnimClock *c)
{
    if (!c->period && timeBeginPeriod(1) == TIMERR_NOERROR)
        c->period = 1;
}

void ce_anim_idle(CeAnimClock *c)
{
    if (c->period) {
        timeEndPeriod(1);
        c->period = 0;
    }
}

int ce_anim_frame_wait(DWORD t0, int i, int frame_ms)
{
    MSG m;
    LONG left;
    int next;
    GdiFlush();
    left = (LONG)(t0 + (DWORD)i * (DWORD)frame_ms - timeGetTime());
    if (left > 0)
        Sleep((DWORD)left);
    PeekMessageW(&m, NULL, 0, 0, PM_NOREMOVE | PM_NOYIELD);
    next = (int)((timeGetTime() - t0) / (DWORD)frame_ms) + 1;
    return next > i ? next : i + 1;
}

static int stop(CeBackBuf *bb, CeAnimAbort abort, void *ctx)
{
    return (abort && abort(ctx)) || !bb->bits;
}

int ce_anim_fly(CeAnimClock *c, CeBackBuf *bb, HDC dc, CeRect from, CeRect to, int px_per_frame,
                int frame_ms, const CeImage *s, CeAnimAbort abort, void *ctx, int *frames_out)
{
    CeRect prev;
    int dx = to.x - from.x, dy = to.y - from.y, dist, frames, i, drawn = 0;
    DWORD start;
    dist = (int)sqrt((double)dx * dx + (double)dy * dy);
    frames = px_per_frame > 0 ? dist / px_per_frame : 1;
    if (frames < 1)
        frames = 1;
    if (frames_out)
        *frames_out = frames;
    prev = from;
    ce_anim_begin(c);
    start = timeGetTime();
    for (i = 1; i < frames && !stop(bb, abort, ctx);) {
        CeRect cur = from;
        cur.x = from.x + dx * i / frames;
        cur.y = from.y + dy * i / frames;
        ce_backbuf_present(bb, dc, ce_rect_union(prev, cur), s, cur.x, cur.y, from.w, from.h);
        prev = cur;
        drawn++;
        i = ce_anim_frame_wait(start, i, frame_ms);
    }
    if (!stop(bb, abort, ctx)) {
        ce_backbuf_present(bb, dc, ce_rect_union(prev, to), s, to.x, to.y, from.w, from.h);
        drawn++;
        ce_anim_frame_wait(start, frames, frame_ms);
    }
    return drawn;
}
