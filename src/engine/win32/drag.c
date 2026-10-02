/*
 * Card engine, Win32 — cards dragged with the mouse (see drag.h).
 */
#include "drag.h"

void ce_drag_begin(CeDrag *d, CeImage *sprite, int x, int y, int w, int h, int px, int py)
{
    ce_image_free(d->sprite);
    ce_image_free(d->shadow);
    d->sprite = sprite;
    d->shadow = NULL;
    d->sdx = d->sdy = 0;
    d->x = x;
    d->y = y;
    d->w = w;
    d->h = h;
    d->grab_dx = px - x;
    d->grab_dy = py - y;
}

CeRect ce_drag_rect(const CeDrag *d)
{
    return ce_rect(d->x, d->y, d->w, d->h);
}

void ce_drag_set_shadow(CeDrag *d, CeImage *shadow, int sdx, int sdy)
{
    ce_image_free(d->shadow);
    d->shadow = shadow;
    d->sdx = sdx;
    d->sdy = sdy;
}

CeRect ce_drag_cover(const CeDrag *d)
{
    CeRect r = ce_drag_rect(d);
    if (d->sprite && d->shadow)
        r = ce_rect_union(r, ce_rect(d->x + d->sdx, d->y + d->sdy, d->shadow->w, d->shadow->h));
    return r;
}

/* The stack (and its shadow) over region r of the back buffer, as one frame. */
static void present(const CeDrag *d, CeBackBuf *bb, HDC dc, CeRect r)
{
    CeLayer ly[2];
    ly[0].img = d->shadow;
    ly[0].x = d->x + d->sdx;
    ly[0].y = d->y + d->sdy;
    ly[1].img = d->sprite;
    ly[1].x = d->x;
    ly[1].y = d->y;
    ce_backbuf_present_layers(bb, dc, r, ly, 2);
}

CeRect ce_drag_move(CeDrag *d, CeBackBuf *bb, HWND hwnd, int px, int py)
{
    CeRect old = ce_drag_rect(d), oc = ce_drag_cover(d);
    d->x = px - d->grab_dx;
    d->y = py - d->grab_dy;
    if (d->sprite && hwnd && bb->bits) {
        HDC dc = GetDC(hwnd);
        if (dc) {
            present(d, bb, dc, ce_rect_union(oc, ce_drag_cover(d)));
            ReleaseDC(hwnd, dc);
        }
    }
    return old;
}

int ce_drag_paint(const CeDrag *d, CeBackBuf *bb, HDC dc, const RECT *rc)
{
    if (!d->sprite || !bb->bits)
        return 0;
    present(d, bb, dc, ce_rect(rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top));
    return 1;
}

int ce_drag_zip_back(CeDrag *d, CeAnimClock *anim, CeBackBuf *bb, HWND hwnd, int x0, int y0, int dur, CeEase ease,
                     int frame_ms, CeAnimAbort abort, void *ctx, int *frames)
{
    int drawn = 0;
    if (frames)
        *frames = 0;
    if (d->sprite && hwnd && bb->bits && (d->x != x0 || d->y != y0)) {
        HDC dc = GetDC(hwnd);
        if (dc) {
            drawn = ce_anim_fly(anim, bb, dc, ce_drag_rect(d), ce_rect(x0, y0, d->w, d->h), dur, ease, frame_ms,
                                d->sprite, d->shadow, d->sdx, d->sdy, abort, ctx, frames);
            ReleaseDC(hwnd, dc);
        }
    }
    if (!(abort && abort(ctx))) {
        d->x = x0;
        d->y = y0;
    }
    return drawn;
}

void ce_drag_end(CeDrag *d, HWND hwnd)
{
    if (d->sprite && hwnd && d->w > 0 && d->h > 0) {
        CeRect c = ce_drag_cover(d);
        RECT rc;
        rc.left = c.x;
        rc.top = c.y;
        rc.right = c.x + c.w;
        rc.bottom = c.y + c.h;
        InvalidateRect(hwnd, &rc, FALSE);
    }
    ce_drag_free(d);
}

void ce_drag_free(CeDrag *d)
{
    ce_image_free(d->sprite);
    ce_image_free(d->shadow);
    d->sprite = NULL;
    d->shadow = NULL;
}

int ce_drag_threshold_passed(int x0, int y0, int x, int y)
{
    int dx = x - x0, dy = y - y0;
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    return dx > GetSystemMetrics(SM_CXDRAG) || dy > GetSystemMetrics(SM_CYDRAG);
}
