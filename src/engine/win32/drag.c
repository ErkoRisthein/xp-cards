/*
 * Card engine, Win32 — cards dragged with the mouse (see drag.h).
 */
#include "drag.h"

void ce_drag_begin(CeDrag *d, CeImage *sprite, int x, int y, int w, int h, int px, int py)
{
    ce_image_free(d->sprite);
    d->sprite = sprite;
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

CeRect ce_drag_move(CeDrag *d, CeBackBuf *bb, HWND hwnd, int px, int py)
{
    CeRect old = ce_drag_rect(d);
    d->x = px - d->grab_dx;
    d->y = py - d->grab_dy;
    if (d->sprite && hwnd && bb->bits) {
        HDC dc = GetDC(hwnd);
        if (dc) {
            ce_backbuf_present(bb, dc, ce_rect_union(old, ce_drag_rect(d)), d->sprite, d->x, d->y, d->w, d->h);
            ReleaseDC(hwnd, dc);
        }
    }
    return old;
}

int ce_drag_paint(const CeDrag *d, CeBackBuf *bb, HDC dc, const RECT *rc)
{
    if (!d->sprite || !bb->bits)
        return 0;
    ce_backbuf_present(bb, dc, ce_rect(rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top), d->sprite,
                       d->x, d->y, d->w, d->h);
    return 1;
}

int ce_drag_zip_back(CeDrag *d, CeAnimClock *anim, CeBackBuf *bb, HWND hwnd, int x0, int y0, int px_per_frame,
                     int frame_ms, CeAnimAbort abort, void *ctx, int *frames)
{
    int drawn = 0;
    if (frames)
        *frames = 0;
    if (d->sprite && hwnd && bb->bits && (d->x != x0 || d->y != y0)) {
        HDC dc = GetDC(hwnd);
        if (dc) {
            drawn = ce_anim_fly(anim, bb, dc, ce_drag_rect(d), ce_rect(x0, y0, d->w, d->h), px_per_frame, frame_ms,
                                d->sprite, abort, ctx, frames);
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
        RECT rc;
        rc.left = d->x;
        rc.top = d->y;
        rc.right = d->x + d->w;
        rc.bottom = d->y + d->h;
        InvalidateRect(hwnd, &rc, FALSE);
    }
    ce_drag_free(d);
}

void ce_drag_free(CeDrag *d)
{
    ce_image_free(d->sprite);
    d->sprite = NULL;
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
