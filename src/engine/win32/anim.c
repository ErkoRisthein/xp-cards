/*
 * Card engine, Win32 — frame-timed animation (see anim.h).
 */
#include "anim.h"

#include <mmsystem.h>
#include <string.h>

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

/* The rect a sprite of size (w, h) at (x, y) covers with its shadow. */
static CeRect cover(int x, int y, int w, int h, const CeImage *shadow, int sdx, int sdy)
{
    CeRect r = ce_rect(x, y, w, h);
    if (shadow)
        r = ce_rect_union(r, ce_rect(x + sdx, y + sdy, shadow->w, shadow->h));
    return r;
}

int ce_anim_fly(CeAnimClock *c, CeBackBuf *bb, HDC dc, CeRect from, CeRect to, int dur, CeEase ease, int frame_ms,
                const CeImage *s, const CeImage *shadow, int sdx, int sdy, CeAnimAbort abort, void *ctx,
                int *frames_out)
{
    CeRect prev = cover(from.x, from.y, from.w, from.h, shadow, sdx, sdy), cur;
    CeLayer ly[2];
    int drawn = 0, i = 1;
    DWORD start;
    ce_anim_begin(c);
    start = timeGetTime();
    while (!stop(bb, abort, ctx)) {
        LONG t = (LONG)(timeGetTime() - start) + frame_ms;     /* the position due one frame later */
        int p = ce_ease(ease, t, dur), x = ce_ease_lerp(from.x, to.x, p), y = ce_ease_lerp(from.y, to.y, p);
        cur = cover(x, y, from.w, from.h, shadow, sdx, sdy);
        ly[0].img = shadow;
        ly[0].x = x + sdx;
        ly[0].y = y + sdy;
        ly[1].img = s;
        ly[1].x = x;
        ly[1].y = y;
        ce_backbuf_present_layers(bb, dc, ce_rect_union(prev, cur), ly, 2);
        prev = cur;
        drawn++;
        if (t >= dur)
            break;                                   /* landed (that frame is never dropped) */
        i = ce_anim_frame_wait(start, i, frame_ms);
    }
    if (frames_out)
        *frames_out = drawn;
    return drawn;
}

/* ---- the flight scheduler -------------------------------------------------------------------------- */

void ce_flights_init(CeFlights *fl, CeAnimClock *clock, CeBackBuf *bb, HWND hwnd, int frame_ms, CeFlightLand land,
                     CeAnimAbort abort, void *ctx)
{
    ce_flights_drop(fl);
    fl->clock = clock;
    fl->bb = bb;
    fl->hwnd = hwnd;
    fl->frame_ms = frame_ms > 0 ? frame_ms : 10;
    fl->land = land;
    fl->abort = abort;
    fl->ctx = ctx;
    fl->frames = 0;
    fl->aborted = 0;
}

void ce_flights_drop(CeFlights *fl)
{
    int i;
    for (i = 0; i < fl->n; i++) {
        ce_image_free(fl->f[i].tmp);
        fl->f[i].tmp = NULL;
    }
    fl->n = 0;
    fl->ndirty = 0;
}

void ce_flights_dirty(CeFlights *fl, CeRect r)
{
    int i;
    if (r.w <= 0 || r.h <= 0)
        return;
    for (i = 0; i < fl->ndirty; i++) {               /* already covered, or covering an old one */
        CeRect *d = &fl->dirty[i];
        if (r.x >= d->x && r.y >= d->y && r.x + r.w <= d->x + d->w && r.y + r.h <= d->y + d->h)
            return;
        if (d->x >= r.x && d->y >= r.y && d->x + d->w <= r.x + r.w && d->y + d->h <= r.y + r.h) {
            *d = r;
            return;
        }
    }
    if (fl->ndirty < CE_MAX_FLIGHT_DIRTY) {
        fl->dirty[fl->ndirty++] = r;
        return;
    }
    fl->dirty[fl->ndirty - 1] = ce_rect_union(fl->dirty[fl->ndirty - 1], r);   /* (full: grow the last) */
}

void ce_flights_release(CeFlights *fl)
{
    int i;
    for (i = 0; i < fl->n; i++)
        fl->f[i].hold = 0;
}

DWORD ce_flights_next(const CeFlight *f)
{
    return f->t0 + (DWORD)ce_cascade_ms(f->dur);
}

CeFlight *ce_flights_add(CeFlights *fl, const CeImage *img, CeRect from, CeRect to, int dur, CeEase ease, int src,
                         int dst, int user)
{
    CeFlight *f;
    DWORD now, end_prev = 0;
    int i, have = 0;
    if (fl->n >= CE_MAX_FLIGHTS)
        ce_flights_land_all(fl);
    if (fl->n >= CE_MAX_FLIGHTS)
        return NULL;
    now = timeGetTime();
    for (i = 0; i < fl->n; i++)
        if (dst >= 0 && fl->f[i].dst == dst) {
            DWORD e = fl->f[i].t0 + (DWORD)fl->f[i].dur;
            if (!have || (LONG)(e - end_prev) > 0)
                end_prev = e;
            have = 1;
        }
    f = &fl->f[fl->n++];
    memset(f, 0, sizeof *f);
    f->img = img;
    f->kind = CE_FLIGHT_MOVE;
    f->from = from;
    f->to = to;
    f->dur = dur > 0 ? dur : 0;
    f->ease = ease;
    f->src = src;
    f->dst = dst;
    f->user = user;
    f->hold = 1;
    f->early = 1;
    f->t0 = now;
    if (have)                                         /* lands after the cards already bound for dst */
        f->t0 = now + (DWORD)ce_flight_delay(f->dur, (int)(LONG)(end_prev - now));
    return f;
}

/* A flip's width at time t: the first side narrows to nothing (accelerating), the second widens from it
 * (decelerating), as a card turning on its vertical axis looks. Returns the side to draw. */
static const CeImage *flip_side(const CeFlight *f, LONG t, int *w)
{
    int half = f->dur / 2, full = f->from.w;
    if (t < half) {
        *w = full - (int)((long long)full * ce_ease(CE_EASE_ACCEL, (int)t, half) / CE_EASE_ONE);
        return f->img;
    }
    *w = (int)((long long)full * ce_ease(CE_EASE_DECEL, (int)(t - half), f->dur - half) / CE_EASE_ONE);
    return f->img2;
}

/* The squeezed side, w columns sampled from the whole side (nearest), into the flight's tmp image. */
static const CeImage *squeeze(CeFlight *f, const CeImage *side, int w)
{
    int x, y, sw;
    if (!side || w <= 0)
        return NULL;
    if (w >= side->w)
        return side;
    if (!f->tmp || f->tmp->w < side->w || f->tmp->h < side->h) {
        ce_image_free(f->tmp);
        f->tmp = ce_image_new(side->w, side->h);
        if (!f->tmp)
            return NULL;
    }
    sw = side->w;
    for (y = 0; y < side->h; y++) {
        const uint32_t *s = side->px + (size_t)y * side->stride;
        uint32_t *d = f->tmp->px + (size_t)y * f->tmp->stride;
        for (x = 0; x < w; x++)
            d[x] = s[(x * sw + sw / 2) / w];
    }
    f->view = ce_image_wrap(w, side->h, f->tmp->stride, f->tmp->px);
    return &f->view;
}

/* Where flight f is at time t (ms since its start, >= 0): sets drawn / lx / ly; returns its cover. */
static CeRect place(CeFlight *f, LONG t)
{
    if (f->kind == CE_FLIGHT_FLIP) {
        int w;
        const CeImage *side = flip_side(f, t, &w);
        f->drawn = squeeze(f, side, w);
        f->lx = f->from.x + (f->from.w - (f->drawn ? f->drawn->w : 0)) / 2;
        f->ly = f->from.y;
        return f->drawn ? ce_rect(f->lx, f->ly, f->drawn->w, f->drawn->h) : ce_rect(f->lx, f->ly, 0, 0);
    } else {
        int p = ce_ease(f->ease, (int)t, f->dur), w = f->img ? f->img->w : f->from.w, h = f->img ? f->img->h : f->from.h;
        f->drawn = f->img;
        f->lx = ce_ease_lerp(f->from.x, f->to.x, p);
        f->ly = ce_ease_lerp(f->from.y, f->to.y, p);
        return cover(f->lx, f->ly, w, h, f->shadow, f->sdx, f->sdy);
    }
}

static int layers(const CeFlights *fl, CeLayer *ly)
{
    int i, n = 0;
    for (i = 0; i < fl->n; i++) {
        const CeFlight *f = &fl->f[i];
        if (!f->on || !f->drawn)
            continue;
        if (f->shadow && f->kind == CE_FLIGHT_MOVE) {
            ly[n].img = f->shadow;
            ly[n].x = f->lx + f->sdx;
            ly[n].y = f->ly + f->sdy;
            n++;
        }
        ly[n].img = f->drawn;
        ly[n].x = f->lx;
        ly[n].y = f->ly;
        n++;
    }
    return n;
}

static long area(CeRect r) { return (long)r.w * r.h; }

/* Present rects r[0..n) with the layers: overlapping rects are merged first when that costs no more than
 * presenting both (a cascade's cards often share a region). */
static void present(CeFlights *fl, HDC dc, CeRect *r, int n)
{
    CeLayer ly[2 * CE_MAX_FLIGHTS];
    int nl = layers(fl, ly), i, j, merged = 1;
    while (merged) {
        merged = 0;
        for (i = 0; i < n && !merged; i++)
            for (j = i + 1; j < n; j++) {
                CeRect u = ce_rect_union(r[i], r[j]);
                if (area(u) <= area(r[i]) + area(r[j])) {
                    r[i] = u;
                    r[j] = r[--n];
                    merged = 1;
                    break;
                }
            }
    }
    for (i = 0; i < n; i++)
        if (r[i].w > 0 && r[i].h > 0)
            ce_backbuf_present_layers(fl->bb, dc, r[i], ly, nl);
}

/* One frame at time now: every flight that has started (or shows early) is drawn where it is due one
 * frame later; the flights that are due and not held land after it (removed, then the game's callback). */
static void frame(CeFlights *fl, HDC dc, DWORD now)
{
    CeRect r[CE_MAX_FLIGHTS + CE_MAX_FLIGHT_DIRTY];
    CeFlight landed[CE_MAX_FLIGHTS];
    int nr = 0, nland = 0, i, k;
    for (i = 0; i < fl->n; i++) {
        CeFlight *f = &fl->f[i];
        LONG t = (LONG)(now + (DWORD)fl->frame_ms - f->t0);
        CeRect vis;
        if (t < 0 && !f->early)
            continue;
        if (t < 0)
            t = 0;
        vis = place(f, t);
        if (f->on)
            r[nr++] = vis.w > 0 ? ce_rect_union(f->shown, vis) : f->shown;
        else if (vis.w > 0)
            r[nr++] = vis;
        f->shown = vis;
        f->on = 1;
    }
    for (i = 0; i < fl->ndirty; i++)
        r[nr++] = fl->dirty[i];
    fl->ndirty = 0;
    present(fl, dc, r, nr);
    fl->frames++;
    /* the arrivals: out of the list first (the game's view of what is in the air), then told in order */
    for (i = 0, k = 0; i < fl->n; i++) {
        CeFlight *f = &fl->f[i];
        LONG t = (LONG)(now + (DWORD)fl->frame_ms - f->t0);
        if (f->on && !f->hold && t >= f->dur)
            landed[nland++] = *f;
        else
            fl->f[k++] = *f;
    }
    fl->n = k;
    for (i = 0; i < nland; i++) {
        if (fl->land)
            fl->land(fl->ctx, &landed[i]);
        ce_image_free(landed[i].tmp);
    }
}

/* What is dirty, with the flights where they were drawn last. */
static void flush(CeFlights *fl, HDC dc)
{
    CeRect r[CE_MAX_FLIGHT_DIRTY];
    int n = fl->ndirty;
    memcpy(r, fl->dirty, sizeof r[0] * (size_t)n);
    fl->ndirty = 0;
    present(fl, dc, r, n);
}

enum { RUN_UNTIL, RUN_ALL, RUN_KEY };

static int done(const CeFlights *fl, int mode, DWORD until, int key)
{
    int i;
    if (fl->n == 0)
        return 1;
    if (mode == RUN_UNTIL)
        return (LONG)(timeGetTime() - until) >= 0;
    if (mode == RUN_KEY) {
        for (i = 0; i < fl->n; i++)
            if (fl->f[i].dst == key)
                return 0;
        return 1;
    }
    return 0;
}

static int run(CeFlights *fl, int mode, DWORD until, int key)
{
    HDC dc;
    DWORD start;
    int i = 1, rc = 0, first = 1;
    fl->aborted = 0;
    if (mode != RUN_UNTIL)
        for (i = 0; i < fl->n; i++)
            fl->f[i].hold = 0;
    i = 1;
    if (fl->n == 0 && fl->ndirty == 0)
        return 0;
    if (!fl->hwnd || !fl->bb || !fl->bb->bits || stop(fl->bb, fl->abort, fl->ctx)) {
        ce_flights_drop(fl);
        fl->aborted = 1;
        return -1;
    }
    dc = GetDC(fl->hwnd);
    if (!dc) {
        ce_flights_drop(fl);
        fl->aborted = 1;
        return -1;
    }
    ce_anim_begin(fl->clock);
    start = timeGetTime();
    for (;;) {
        if (stop(fl->bb, fl->abort, fl->ctx)) {
            ce_flights_drop(fl);
            fl->aborted = 1;
            rc = -1;
            break;
        }
        /* a run with cards in the air draws at least one frame (a card just added shows at once) */
        if (!(first && fl->n > 0) && done(fl, mode, until, key))
            break;
        first = 0;
        frame(fl, dc, timeGetTime());
        if (fl->n == 0 || (mode == RUN_UNTIL && done(fl, mode, until, key)))
            break;
        i = ce_anim_frame_wait(start, i, fl->frame_ms);
    }
    if (rc == 0 && fl->ndirty)
        flush(fl, dc);
    ReleaseDC(fl->hwnd, dc);
    return rc;
}

int ce_flights_settle(CeFlights *fl, int src)
{
    int i;
    for (i = 0; i < fl->n; i++)
        if (src >= 0 && fl->f[i].dst == src)
            return run(fl, RUN_KEY, 0, src);
    return 0;
}

int ce_flights_run(CeFlights *fl, DWORD until)
{
    return run(fl, RUN_UNTIL, until, -1);
}

int ce_flights_land_all(CeFlights *fl)
{
    return run(fl, RUN_ALL, 0, -1);
}
