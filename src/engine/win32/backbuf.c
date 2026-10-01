/*
 * Card engine, Win32 — the back buffer (see backbuf.h).
 */
#include "backbuf.h"

#include <string.h>

/* (Re)create a 32-bpp top-down DIB of w x h selected into *dc. On failure the old one is kept. */
static int make_dib(HDC *dc, HBITMAP *bmp, HBITMAP *old, uint32_t **bits, int w, int h)
{
    BITMAPINFO bi;
    void *p = NULL;
    HBITMAP b, prev;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    b = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &p, NULL, 0);
    if (!b || !p) {
        if (b)
            DeleteObject(b);
        return 0;
    }
    if (!*dc) {
        *dc = CreateCompatibleDC(NULL);
        if (!*dc) {
            DeleteObject(b);
            return 0;
        }
    }
    GdiFlush();
    prev = (HBITMAP)SelectObject(*dc, b);
    if (*bmp)
        DeleteObject(*bmp);              /* prev == *bmp */
    else
        *old = prev;                     /* the DC's original 1x1 bitmap, restored on cleanup */
    *bmp = b;
    *bits = (uint32_t *)p;
    return 1;
}

static void free_dib(HDC *dc, HBITMAP *bmp, HBITMAP *old)
{
    if (*dc) {
        if (*bmp)
            SelectObject(*dc, *old);
        DeleteDC(*dc);
    }
    if (*bmp)
        DeleteObject(*bmp);
    *dc = NULL;
    *bmp = NULL;
}

int ce_backbuf_ensure(CeBackBuf *bb, int w, int h)
{
    int reuse = bb->dib && w <= bb->buf_w && h <= bb->buf_h;
    if (reuse && !bb->in_sizemove && (double)bb->buf_w * bb->buf_h > 1.5 * (double)w * h + 65536.0)
        reuse = 0;
    if (!reuse) {
        int nw = w, nh = h;
        if (bb->in_sizemove) {
            nw = (w + 255) & ~255;
            nh = (h + 255) & ~255;
        }
        if (!make_dib(&bb->memdc, &bb->dib, &bb->old_bmp, &bb->bits, nw, nh)) {
            if (nw == w && nh == h)
                return 0;
            nw = w;
            nh = h;
            if (!make_dib(&bb->memdc, &bb->dib, &bb->old_bmp, &bb->bits, nw, nh))
                return 0;
        }
        bb->buf_w = nw;
        bb->buf_h = nh;
    }
    bb->fb = ce_image_wrap(w, h, bb->buf_w, bb->bits);
    return 1;
}

static int ensure_scratch(CeBackBuf *bb, int w, int h)
{
    int nw, nh;
    if (bb->sdib && w <= bb->s_w && h <= bb->s_h)
        return 1;
    nw = (w > bb->s_w ? w : bb->s_w) + 63;
    nh = (h > bb->s_h ? h : bb->s_h) + 63;
    if (!make_dib(&bb->sdc, &bb->sdib, &bb->sold_bmp, &bb->sbits, nw, nh))
        return 0;
    bb->s_w = nw;
    bb->s_h = nh;
    return 1;
}

void ce_backbuf_free(CeBackBuf *bb)
{
    GdiFlush();
    free_dib(&bb->memdc, &bb->dib, &bb->old_bmp);
    free_dib(&bb->sdc, &bb->sdib, &bb->sold_bmp);
    bb->bits = bb->sbits = NULL;
    bb->buf_w = bb->buf_h = bb->s_w = bb->s_h = 0;
}

/* ---- live resize ------------------------------------------------------------------------------- */

void ce_backbuf_enter_sizemove(CeBackBuf *bb)
{
    bb->in_sizemove = 1;
    bb->sized_in_loop = 0;
}

void ce_backbuf_note_layout(CeBackBuf *bb)
{
    if (bb->in_sizemove)
        bb->sized_in_loop = 1;
}

int ce_backbuf_exit_sizemove(CeBackBuf *bb, HWND hwnd, int *w, int *h)
{
    RECT cr;
    bb->in_sizemove = 0;
    if (!bb->sized_in_loop)
        return 0;
    bb->sized_in_loop = 0;
    if (IsIconic(hwnd) || !GetClientRect(hwnd, &cr) || cr.right <= 0 || cr.bottom <= 0)
        return 0;
    *w = cr.right;
    *h = cr.bottom;
    return 1;
}

int ce_backbuf_quality(const CeBackBuf *bb)
{
    return bb->in_sizemove ? 0 : 1;
}

/* ---- painting ---------------------------------------------------------------------------------- */

void ce_backbuf_paint(CeBackBuf *bb, HDC dc, const RECT *prc, int ready, HBRUSH bg)
{
    RECT rc = *prc;
    if (ready && bb->bits) {
        int bw = bb->fb.w, bh = bb->fb.h;
        int r = rc.right < bw ? rc.right : bw, btm = rc.bottom < bh ? rc.bottom : bh;
        GdiFlush();
        if (r > rc.left && btm > rc.top)
            BitBlt(dc, rc.left, rc.top, r - rc.left, btm - rc.top, bb->memdc, rc.left, rc.top, SRCCOPY);
        if (rc.right > bw) {                          /* only if the buffer could not grow */
            RECT f = { bw > rc.left ? bw : rc.left, rc.top, rc.right, rc.bottom };
            FillRect(dc, &f, bg);
        }
        if (rc.bottom > bh) {
            RECT f = { rc.left, bh > rc.top ? bh : rc.top, r, rc.bottom };
            if (f.right > f.left)
                FillRect(dc, &f, bg);
        }
    } else {
        FillRect(dc, &rc, bg);
    }
}

void ce_backbuf_blit(CeBackBuf *bb, HDC dc, int x, int y, int w, int h)
{
    if (w > 0 && h > 0)
        BitBlt(dc, x, y, w, h, bb->memdc, x, y, SRCCOPY);
}

void ce_backbuf_present(CeBackBuf *bb, HDC dc, CeRect r, const CeImage *s, int sx, int sy, int sw, int sh)
{
    CeRect k;
    int x2, y2;
    CeImage scratch;
    if (!ce_rect_clip(&r, bb->fb.w, bb->fb.h))
        return;
    k.x = sx > r.x ? sx : r.x;                        /* k = r intersected with the sprite */
    k.y = sy > r.y ? sy : r.y;
    x2 = sx + sw < r.x + r.w ? sx + sw : r.x + r.w;
    y2 = sy + sh < r.y + r.h ? sy + sh : r.y + r.h;
    k.w = x2 - k.x;
    k.h = y2 - k.y;
    if (k.w <= 0 || k.h <= 0 || !ensure_scratch(bb, k.w, k.h)) {
        ce_backbuf_blit(bb, dc, r.x, r.y, r.w, r.h);
        return;
    }
    ce_backbuf_blit(bb, dc, r.x, r.y, r.w, k.y - r.y);                              /* above the sprite */
    ce_backbuf_blit(bb, dc, r.x, k.y + k.h, r.w, r.y + r.h - (k.y + k.h));          /* below */
    ce_backbuf_blit(bb, dc, r.x, k.y, k.x - r.x, k.h);                              /* left */
    ce_backbuf_blit(bb, dc, k.x + k.w, k.y, r.x + r.w - (k.x + k.w), k.h);          /* right */
    scratch = ce_image_wrap(k.w, k.h, bb->s_w, bb->sbits);
    GdiFlush();
    ce_copy_rect(&scratch, 0, 0, &bb->fb, k.x, k.y, k.w, k.h);
    if (s)
        ce_blit(&scratch, s, sx - k.x, sy - k.y);
    BitBlt(dc, k.x, k.y, k.w, k.h, bb->sdc, 0, 0, SRCCOPY);
}
