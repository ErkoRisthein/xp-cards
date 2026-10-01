/*
 * FreeCell HD — the board view: back buffer, incremental rendering, painting, card flights, cursors
 * and the "Cards Left: N" text in the menu bar.
 *
 * Rendering model. The back buffer (a 32-bpp top-down DIB section) always holds the board as last
 * rendered, and drawn_board/drawn_view record what that was. view_sync() compares that with the
 * session's current board and view and re-renders only the regions that differ (a column band, a
 * top-row cell, the king box) with fc_render_board_rect, then invalidates exactly those regions;
 * WM_PAINT just BitBlts from the back buffer. A layout change (resize) renders everything. The
 * session's invalidate() callback only marks the view dirty (its state may be mid-update); the window
 * procedure syncs after every session call, and a posted WM_APP_SYNC is the safety net.
 *
 * Animation (layout.md §7): the flying card is hidden from the board (FcView hide_*), the changed
 * regions are rendered into the back buffer, and each frame composes "back buffer region + card" in a
 * small scratch DIB which is BitBlt'ed to the window — only the union of the card's old and new
 * rectangles, so the back buffer stays clean and nothing flickers. Frames are paced against
 * timeGetTime at FC_FRAME_MS each (late frames are dropped, the landing frame never is), with the
 * system timer at 1 ms (timeBeginPeriod) while cards fly: a plain Sleep(10) lasts a whole clock tick
 * on XP (10–15.6 ms), which made flights up to twice as slow as designed.
 */
#include "app.h"

#include <mmsystem.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static HBRUSH green_brush;

/* ---- timing log (FCHD_TIMING_LOG) ------------------------------------------------------------ */

double now_ms(App *a)
{
    LARGE_INTEGER t;
    if (!a->qpf.QuadPart || !QueryPerformanceCounter(&t))
        return (double)GetTickCount();
    return (double)t.QuadPart * 1000.0 / (double)a->qpf.QuadPart;
}

void tlog(App *a, const char *fmt, ...)
{
    char buf[300];
    va_list ap;
    int n;
    DWORD w;
    if (!a->tlog || a->tlog == INVALID_HANDLE_VALUE)
        return;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > (int)sizeof buf - 3)
        n = (int)sizeof buf - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';
    WriteFile(a->tlog, buf, (DWORD)n, &w, NULL);
}

/* ---- DIB sections ------------------------------------------------------------------------------ */

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

/* Back buffer for a w x h client. While live-resizing it grows in 256-px steps and is reused when
 * shrinking; otherwise it is reallocated when it is much larger than needed. Returns 0 (keeping the
 * old buffer) if memory runs out. */
static int ensure_buffer(App *a, int w, int h)
{
    int reuse = a->dib && w <= a->buf_w && h <= a->buf_h;
    if (reuse && !a->in_sizemove && (double)a->buf_w * a->buf_h > 1.5 * (double)w * h + 65536.0)
        reuse = 0;
    if (!reuse) {
        int nw = w, nh = h;
        if (a->in_sizemove) {
            nw = (w + 255) & ~255;
            nh = (h + 255) & ~255;
        }
        if (!make_dib(&a->memdc, &a->dib, &a->old_bmp, &a->bits, nw, nh)) {
            if (nw == w && nh == h)
                return 0;
            nw = w;
            nh = h;
            if (!make_dib(&a->memdc, &a->dib, &a->old_bmp, &a->bits, nw, nh))
                return 0;
        }
        a->buf_w = nw;
        a->buf_h = nh;
    }
    a->fb = fc_image_wrap(w, h, a->buf_w, a->bits);
    return 1;
}

static int ensure_scratch(App *a, int w, int h)
{
    int nw, nh;
    if (a->sdib && w <= a->s_w && h <= a->s_h)
        return 1;
    nw = (w > a->s_w ? w : a->s_w) + 63;
    nh = (h > a->s_h ? h : a->s_h) + 63;
    if (!make_dib(&a->sdc, &a->sdib, &a->sold_bmp, &a->sbits, nw, nh))
        return 0;
    a->s_w = nw;
    a->s_h = nh;
    return 1;
}

/* ---- init / free --------------------------------------------------------------------------------- */

int view_init(App *a)
{
    a->cur_arrow = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    a->cur_up = LoadCursorW(NULL, (LPCWSTR)IDC_UPARROW);
    a->cur_wait = LoadCursorW(NULL, (LPCWSTR)IDC_WAIT);
    a->cur_busy = LoadCursorW(NULL, (LPCWSTR)IDC_APPSTARTING);
    a->cur_down = LoadCursorW(a->inst, RES_CUR_DOWNARROW);
    if (!a->cur_up)
        a->cur_up = a->cur_arrow;
    if (!a->cur_busy)
        a->cur_busy = a->cur_wait;
    if (!a->cur_down)
        a->cur_down = a->cur_arrow;
    a->cursor = a->cur_arrow;
    a->hide_col = a->hide_pos = -1;
    a->cl_prev_left = INT_MAX;
    a->menu_undo = a->menu_redo = a->menu_restart = -1;
    a->menu_hint = a->menu_finish = -1;
    a->quality = -1;
    green_brush = CreateSolidBrush(RGB(0, 127, 0));
    menubar_font_update(a);
    return 1;
}

void view_free(App *a)
{
    GdiFlush();
    free_dib(&a->memdc, &a->dib, &a->old_bmp);
    free_dib(&a->sdc, &a->sdib, &a->sold_bmp);
    a->bits = a->sbits = NULL;
    a->have_layout = 0;
    if (a->menu_font)
        DeleteObject(a->menu_font);
    a->menu_font = NULL;
    if (green_brush)
        DeleteObject(green_brush);
    green_brush = NULL;
}

/* ---- layout ---------------------------------------------------------------------------------------- */

void view_invalidate_all(App *a)
{
    a->drawn_valid = 0;
    a->dirty = 1;
    if (a->hwnd)
        InvalidateRect(a->hwnd, NULL, FALSE);
}

void view_resize(App *a, int w, int h)
{
    int q = a->in_sizemove ? 0 : 1;
    double t0, t1;
    if (w <= 0 || h <= 0)
        return;
    if (a->have_layout && a->L.client_w == w && a->L.client_h == h && a->quality == q)
        return;                                       /* e.g. WM_SIZE after a restore: nothing new */
    t0 = now_ms(a);
    if (!ensure_buffer(a, w, h))
        return;                                       /* out of memory: keep the old view */
    fc_layout_compute(&a->L, w, h);
    a->have_layout = 1;
    a->layout_gen++;
    a->quality = q;
    if (a->in_sizemove)
        a->sized_in_loop = 1;
    fc_render_prepare(a->cs, &a->L, q);
    t1 = now_ms(a);
    tlog(a, "resize %dx%d q%d: buffer+layout %.2f ms (s=%.3f, card %dx%d)", w, h, q, t1 - t0,
         a->L.s, a->L.cw, a->L.ch);
    view_invalidate_all(a);
}

/* The card set was decoded after the window appeared: size its sprites and repaint. */
void view_cardset_ready(App *a)
{
    if (a->have_layout)
        fc_render_prepare(a->cs, &a->L, a->quality);
    view_invalidate_all(a);
}

void view_exit_sizemove(App *a)
{
    RECT cr;
    a->in_sizemove = 0;
    if (!a->sized_in_loop)
        return;
    a->sized_in_loop = 0;
    if (IsIconic(a->hwnd) || !GetClientRect(a->hwnd, &cr) || cr.right <= 0 || cr.bottom <= 0)
        return;
    view_resize(a, cr.right, cr.bottom);              /* quality 1, exact buffer */
}

/* ---- what is shown ----------------------------------------------------------------------------------- */

static const FcBoard *view_board(App *a)
{
    return a->use_anim_board ? &a->anim_board : &a->s.board;
}

static void view_state(App *a, FcView *v)
{
    FcsViewState vs;
    fcs_view_state(&a->s, &vs);
    fc_view_init(v);
    v->sel_col = vs.sel_col;
    v->sel_pos = vs.sel_pos;
    v->peek_col = vs.peek_col;
    v->peek_pos = vs.peek_pos;
    v->king = vs.king;
    v->big_king = vs.big_king;
    v->no_game = vs.no_game;
    v->hide_col = a->hide_col;
    v->hide_pos = a->hide_pos;
    v->hint_col = vs.hint_col;
    v->hint_pos = vs.hint_pos;
}

typedef struct Dirty {
    int      full, king;
    unsigned top, cols;      /* bit i: top-row cell i; bit c: column c */
} Dirty;

static void mark(Dirty *d, int col, int pos)
{
    if (col == 0 && pos >= 0 && pos < 8)
        d->top |= 1u << pos;
    else if (col >= 1 && col <= 8)
        d->cols |= 1u << col;
}

static void diff(App *a, const FcBoard *b, const FcView *v, Dirty *d)
{
    const FcBoard *ob = &a->drawn_board;
    const FcView *ov = &a->drawn_view;
    int i;
    memset(d, 0, sizeof *d);
    if (!a->drawn_valid || v->no_game != ov->no_game || v->big_king != ov->big_king) {
        d->full = 1;
        return;
    }
    d->king = v->king != ov->king;
    for (i = 0; i < 8; i++)
        if (b->board[0][i] != ob->board[0][i])
            d->top |= 1u << i;
    for (i = 1; i <= 8; i++)
        if (memcmp(b->board[i], ob->board[i], sizeof b->board[i]) != 0)
            d->cols |= 1u << i;
    if (v->sel_col != ov->sel_col || v->sel_pos != ov->sel_pos) {
        mark(d, ov->sel_col, ov->sel_pos);
        mark(d, v->sel_col, v->sel_pos);
    }
    if (v->peek_col != ov->peek_col || v->peek_pos != ov->peek_pos) {
        mark(d, ov->peek_col, ov->peek_pos);
        mark(d, v->peek_col, v->peek_pos);
    }
    if (v->hide_col != ov->hide_col || v->hide_pos != ov->hide_pos) {
        mark(d, ov->hide_col, ov->hide_pos);
        mark(d, v->hide_col, v->hide_pos);
    }
    if (v->hint_col != ov->hint_col || v->hint_pos != ov->hint_pos) {
        mark(d, ov->hint_col, ov->hint_pos);
        mark(d, v->hint_col, v->hint_pos);
    }
}

#define MAX_DIRTY 20

static int dirty_rects(App *a, const Dirty *d, FcRect *r)
{
    const FcLayout *l = &a->L;
    int n = 0, i;
    if (d->full) {
        r[0].x = r[0].y = 0;
        r[0].w = a->fb.w;
        r[0].h = a->fb.h;
        return 1;
    }
    if (d->king)
        r[n++] = l->king_frame;
    for (i = 0; i < 8; i++)
        if (d->top & (1u << i))
            r[n++] = l->top[i];
    for (i = 1; i <= 8; i++)
        if (d->cols & (1u << i)) {                    /* the whole column band (compression) */
            r[n].x = l->col_x[i];
            r[n].y = l->col_y0;
            r[n].w = l->cw;
            r[n].h = a->fb.h - l->col_y0;
            if (r[n].h > 0)
                n++;
        }
    return n;
}

/* Render what changed since the last sync into the back buffer; returns the regions. */
static int sync_render(App *a, FcRect *out)
{
    const FcBoard *b;
    FcView v;
    Dirty d;
    int n, i;
    a->dirty = 0;
    if (!a->have_layout || !a->bits)
        return 0;                                     /* (without a card set yet: table only) */
    b = view_board(a);
    view_state(a, &v);
    diff(a, b, &v, &d);
    n = dirty_rects(a, &d, out);
    if (n > 0) {
        double t0 = now_ms(a);
        GdiFlush();
        for (i = 0; i < n; i++)
            fc_render_board_rect(&a->fb, &a->L, b, &v, a->cs, out[i]);
        if (d.full)
            tlog(a, "full render %dx%d q%d: %.2f ms", a->fb.w, a->fb.h, a->quality, now_ms(a) - t0);
        else
            tlog(a, "partial render, %d region(s): %.2f ms", n, now_ms(a) - t0);
    }
    a->drawn_board = *b;
    a->drawn_view = v;
    a->drawn_valid = 1;
    return n;
}

void view_sync(App *a)
{
    FcRect r[MAX_DIRTY];
    int n = sync_render(a, r), i;
    for (i = 0; i < n; i++) {
        RECT rc;
        rc.left = r[i].x;
        rc.top = r[i].y;
        rc.right = r[i].x + r[i].w;
        rc.bottom = r[i].y + r[i].h;
        InvalidateRect(a->hwnd, &rc, FALSE);
    }
}

void view_sync_now(App *a)
{
    view_sync(a);
    if (a->hwnd)
        UpdateWindow(a->hwnd);
}

void view_paint(App *a)
{
    PAINTSTRUCT ps;
    HDC dc;
    RECT rc;
    view_sync(a);                                     /* adds the changed regions to the update region */
    dc = BeginPaint(a->hwnd, &ps);
    if (!dc)
        return;
    rc = ps.rcPaint;
    if (a->have_layout && a->bits) {
        int bw = a->fb.w, bh = a->fb.h;
        int r = rc.right < bw ? rc.right : bw, btm = rc.bottom < bh ? rc.bottom : bh;
        GdiFlush();
        if (r > rc.left && btm > rc.top)
            BitBlt(dc, rc.left, rc.top, r - rc.left, btm - rc.top, a->memdc, rc.left, rc.top, SRCCOPY);
        if (rc.right > bw) {                          /* only if the buffer could not grow */
            RECT f = { bw > rc.left ? bw : rc.left, rc.top, rc.right, rc.bottom };
            FillRect(dc, &f, green_brush);
        }
        if (rc.bottom > bh) {
            RECT f = { rc.left, bh > rc.top ? bh : rc.top, r, rc.bottom };
            if (f.right > f.left)
                FillRect(dc, &f, green_brush);
        }
    } else {
        FillRect(dc, &rc, green_brush);
    }
    EndPaint(a->hwnd, &ps);
}

/* Is the mouse pointer over our client area? (client coordinates in *pt) */
static int mouse_over(App *a, POINT *pt)
{
    POINT p;
    RECT cr;
    if (!a->hwnd || !GetCursorPos(&p) || WindowFromPoint(p) != a->hwnd)
        return 0;
    ScreenToClient(a->hwnd, &p);
    GetClientRect(a->hwnd, &cr);
    if (!PtInRect(&cr, p))
        return 0;
    if (pt)
        *pt = p;
    return 1;
}

/* ---- card flight ----------------------------------------------------------------------------------- */

static FcRect rect_union(FcRect p, FcRect q)
{
    FcRect r;
    int x2 = p.x + p.w > q.x + q.w ? p.x + p.w : q.x + q.w;
    int y2 = p.y + p.h > q.y + q.h ? p.y + p.h : q.y + q.h;
    r.x = p.x < q.x ? p.x : q.x;
    r.y = p.y < q.y ? p.y : q.y;
    r.w = x2 - r.x;
    r.h = y2 - r.y;
    return r;
}

static int clip_client(App *a, FcRect *r)
{
    if (r->x < 0) { r->w += r->x; r->x = 0; }
    if (r->y < 0) { r->h += r->y; r->y = 0; }
    if (r->x + r->w > a->fb.w) r->w = a->fb.w - r->x;
    if (r->y + r->h > a->fb.h) r->h = a->fb.h - r->y;
    return r->w > 0 && r->h > 0;
}

static void blit_back(App *a, HDC dc, int x, int y, int w, int h)
{
    if (w > 0 && h > 0)
        BitBlt(dc, x, y, w, h, a->memdc, x, y, SRCCOPY);
}

/* Show region r of the back buffer with card c drawn at (cx, cy) on top. Only the part under the
 * card goes through the (card-sized) scratch DIB; the rest is copied straight from the back buffer.
 * Every pixel is written once, so nothing flickers. */
static void blit_with_card(App *a, HDC dc, FcRect r, Card c, int cx, int cy)
{
    FcRect k;
    int x2, y2;
    FcImage s;
    if (!clip_client(a, &r))
        return;
    k.x = cx > r.x ? cx : r.x;                        /* k = r intersected with the card */
    k.y = cy > r.y ? cy : r.y;
    x2 = cx + a->L.cw < r.x + r.w ? cx + a->L.cw : r.x + r.w;
    y2 = cy + a->L.ch < r.y + r.h ? cy + a->L.ch : r.y + r.h;
    k.w = x2 - k.x;
    k.h = y2 - k.y;
    if (k.w <= 0 || k.h <= 0 || !ensure_scratch(a, k.w, k.h)) {
        blit_back(a, dc, r.x, r.y, r.w, r.h);
        return;
    }
    blit_back(a, dc, r.x, r.y, r.w, k.y - r.y);                              /* above the card */
    blit_back(a, dc, r.x, k.y + k.h, r.w, r.y + r.h - (k.y + k.h));          /* below */
    blit_back(a, dc, r.x, k.y, k.x - r.x, k.h);                              /* left */
    blit_back(a, dc, k.x + k.w, k.y, r.x + r.w - (k.x + k.w), k.h);          /* right */
    s = fc_image_wrap(k.w, k.h, a->s_w, a->sbits);
    GdiFlush();
    fc_copy_rect(&s, 0, 0, &a->fb, k.x, k.y, k.w, k.h);
    fc_render_card(&s, a->cs, c, cx - k.x, cy - k.y, 0);
    BitBlt(dc, k.x, k.y, k.w, k.h, a->sdc, 0, 0, SRCCOPY);
}

#define FC_FRAME_MS 10                          /* DESIGN.md: ~10 ms per frame */

/* 1-ms system timer resolution from the first flight until the replay is over (view_anim_idle). */
static void anim_clock_begin(App *a)
{
    if (!a->anim_period && timeBeginPeriod(1) == TIMERR_NOERROR)
        a->anim_period = 1;
}

void view_anim_idle(App *a)
{
    if (a->anim_period) {
        timeEndPeriod(1);
        a->anim_period = 0;
    }
}

/* Frame i (1-based) of a flight that started at t0 is on screen until t0 + i * FC_FRAME_MS: wait for
 * that, let Windows see us retrieving messages (a long autoplay must not look "Not Responding";
 * PM_NOREMOVE dispatches only sent messages, posted input waits until the move is done), and return
 * the next frame to draw — the one due now, so a slow frame does not slow the flight down. */
static int frame_wait(DWORD t0, int i)
{
    MSG m;
    LONG left;
    int next;
    GdiFlush();
    left = (LONG)(t0 + (DWORD)i * FC_FRAME_MS - timeGetTime());
    if (left > 0)
        Sleep((DWORD)left);
    PeekMessageW(&m, NULL, 0, 0, PM_NOREMOVE | PM_NOYIELD);
    next = (int)((timeGetTime() - t0) / FC_FRAME_MS) + 1;
    return next > i ? next : i + 1;
}

void view_animate_step(App *a, const FcStep *st, int forward)
{
    const FcBoard *b = &a->s.board;
    FcBoard after;
    FcRect from, to, prev, r[MAX_DIRTY];
    int fcol, fpos, tcol, tpos, n, i, dx, dy, frames, dist, drawn = 0;
    unsigned gen;
    HDC dc;
    double t0;
    DWORD start;
    if (a->s.opts.quick || !a->have_layout || !a->cs || !a->bits || !a->hwnd || IsIconic(a->hwnd) ||
        !IsWindowVisible(a->hwnd))
        return;
    t0 = now_ms(a);
    fcol = forward ? st->src_col : st->dst_col;
    fpos = forward ? st->src_pos : st->dst_pos;
    tcol = forward ? st->dst_col : st->src_col;
    tpos = forward ? st->dst_pos : st->src_pos;
    after = *b;
    if (forward)
        fc_step_apply(&after, st);
    else
        fc_step_unapply(&after, st);
    from = fc_layout_card_rect(&a->L, b, fcol, fpos);
    to = fc_layout_card_rect(&a->L, &after, tcol, tpos);   /* where it lands, after the move */

    if (fcol >= 1 && fcol <= 8 && fpos != fc_last_index(b, fcol)) {
        /* a buried card (cheat sweep): the column closes up while it flies */
        a->anim_board = *b;
        for (i = fpos; i < FC_COLLEN - 1; i++)
            a->anim_board.board[fcol][i] = a->anim_board.board[fcol][i + 1];
        a->anim_board.board[fcol][FC_COLLEN - 1] = FC_EMPTY;
        a->use_anim_board = 1;
    } else {
        a->hide_col = fcol;
        a->hide_pos = fpos;
    }
    a->cursor = a->cur_wait;                          /* input is blocked while the card flies */
    if (mouse_over(a, NULL))
        SetCursor(a->cursor);

    gen = a->layout_gen;
    n = sync_render(a, r);                            /* board without the card, + what changed */
    dc = GetDC(a->hwnd);
    frames = 0;
    if (dc) {
        for (i = 0; i < n; i++)                       /* ... with the card still at its source */
            blit_with_card(a, dc, r[i], st->card, from.x, from.y);
        dx = to.x - from.x;
        dy = to.y - from.y;
        dist = (int)sqrt((double)dx * dx + (double)dy * dy);
        frames = dist / a->L.anim_px_per_frame;
        if (frames < 1)
            frames = 1;
        prev = from;
        anim_clock_begin(a);
        start = timeGetTime();
        /* XP's AnimateCard: frames i = 1..N-1 at from + d*i/N, then the destination */
        for (i = 1; i < frames && a->layout_gen == gen && a->bits;) {
            FcRect cur = from;
            cur.x = from.x + dx * i / frames;
            cur.y = from.y + dy * i / frames;
            blit_with_card(a, dc, rect_union(prev, cur), st->card, cur.x, cur.y);
            prev = cur;
            drawn++;
            i = frame_wait(start, i);
        }
        if (a->layout_gen == gen && a->bits) {
            blit_with_card(a, dc, rect_union(prev, to), st->card, to.x, to.y);
            drawn++;
            frame_wait(start, frames);
        }
        ReleaseDC(a->hwnd, dc);
    }
    a->hide_col = a->hide_pos = -1;
    a->use_anim_board = 0;
    if (a->layout_gen != gen)
        view_invalidate_all(a);                       /* resized mid-flight: start over cleanly */
    tlog(a, "flight %d,%d -> %d,%d: %d frames (%d drawn), %.1f ms", fcol, fpos, tcol, tpos, frames, drawn,
         now_ms(a) - t0);
}

/* ---- mouse / cursor ------------------------------------------------------------------------------- */

int view_hit(App *a, int x, int y, int mode, int *col, int *pos)
{
    *col = *pos = -1;
    if (!a->have_layout)
        return 0;
    return fc_layout_hit(&a->L, &a->s.board, x, y, mode, col, pos);
}

static HCURSOR cursor_for(App *a, int c)
{
    switch (c) {
    case FCS_CURSOR_DOWNARROW: return a->cur_down;
    case FCS_CURSOR_UPARROW: return a->cur_up;
    case FCS_CURSOR_WAIT: return a->cur_wait;
    case FCS_CURSOR_APPSTARTING: return a->cur_busy;
    default: return a->cur_arrow;
    }
}

static int cursor_at(App *a, int x, int y, int with_king)
{
    int dcol, dpos, scol, spos;
    int dh = view_hit(a, x, y, FC_HIT_DEST, &dcol, &dpos);
    int on = view_hit(a, x, y, FC_HIT_SOURCE, &scol, &spos);
    if (!dh)
        dcol = FCS_MISS;
    return with_king ? fcs_mouse_move(&a->s, dcol, dpos, on) : fcs_cursor(&a->s, dcol, dpos, on);
}

void view_mouse_move(App *a, int x, int y)
{
    if (!a->have_layout)
        return;
    a->cursor = cursor_for(a, cursor_at(a, x, y, 1));
    SetCursor(a->cursor);
}

/* After a state change (selection, busy, peek) show the right cursor without waiting for a move. */
void view_refresh_cursor(App *a)
{
    POINT p;
    if (!a->have_layout)
        return;
    if (!mouse_over(a, &p)) {                         /* for the next WM_SETCURSOR */
        a->cursor = cursor_for(a, fcs_cursor(&a->s, FCS_MISS, -1, 0));   /* wait / app starting / arrow */
        return;
    }
    a->cursor = cursor_for(a, cursor_at(a, p.x, p.y, 0));
    SetCursor(a->cursor);
}

/* ---- "Cards Left: N" in the menu bar (layout.md §8), and the extras "Moves" / "Time" ------------- */

void menubar_font_update(App *a)
{
    NONCLIENTMETRICSW ncm;
    HFONT f = NULL;
    memset(&ncm, 0, sizeof ncm);
    ncm.cbSize = sizeof ncm;                          /* XP size: built with WINVER 0x0501 */
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0))
        f = CreateFontIndirectW(&ncm.lfMenuFont);
    if (a->menu_font)
        DeleteObject(a->menu_font);
    a->menu_font = f;
}

void menubar_reset(App *a)
{
    a->cl_prev_left = INT_MAX;
}

/* The texts to try, most complete first: "Moves: M    Time: T    Cards Left: N" (extra option), then
 * without Time, then Cards Left alone (XP). Returns the number of candidates. */
static int menubar_texts(App *a, WCHAR out[3][192])
{
    WCHAR fmt[64], cl[96], mv[96], tm[96], t[32];
    char tb[32];
    int n = 0, moves = fcs_moves(&a->s);
    load_wstr(a, IDS_CARDSLEFT, fmt, 64, L"Cards Left: %u");
    wsprintfW(cl, fmt, (unsigned)a->cards_left);
    if (a->s.extras.show_time_moves) {
        load_wstr(a, IDS_MOVES, fmt, 64, L"Moves: %u");
        wsprintfW(mv, fmt, (unsigned)(moves > 0 ? moves : 0));
        fcs_format_time(fcs_elapsed_ms(&a->s), tb, sizeof tb);
        to_wide(tb, t, 32);
        load_wstr(a, IDS_TIME, fmt, 64, L"Time: %s");
        wsprintfW(tm, fmt, t);
        wsprintfW(out[n++], L"%s    %s    %s", mv, tm, cl);
        wsprintfW(out[n++], L"%s    %s", mv, cl);
    }
    lstrcpyW(out[n++], cl);
    return n;
}

void menubar_draw(App *a)
{
    MENUBARINFO mbi;
    RECT wr, cr, bar, er;
    WCHAR texts[3][192];
    const WCHAR *text = NULL;
    SIZE sz;
    TEXTMETRICW tm;
    HDC dc;
    HGDIOBJ of;
    int right, x = 0, y, len = 0, items, i, n, nt;
    BOOL flat = FALSE;
    HWND h = a->hwnd;
    if (!h || !a->menu || IsIconic(h) || !IsWindowVisible(h))
        return;
    memset(&mbi, 0, sizeof mbi);
    mbi.cbSize = sizeof mbi;
    if (!GetMenuBarInfo(h, OBJID_MENU, 0, &mbi) || !GetWindowRect(h, &wr) || !GetClientRect(h, &cr))
        return;
    bar = mbi.rcBar;
    OffsetRect(&bar, -wr.left, -wr.top);              /* window-DC coordinates */
    if (bar.bottom <= bar.top)
        return;
    right = cr.right;                                 /* XP: x = clientWidth - textWidth, window DC */
    if (right > bar.right)
        right = bar.right;
    nt = menubar_texts(a, texts);
    dc = GetWindowDC(h);
    if (!dc)
        return;
    of = SelectObject(dc, a->menu_font ? (HGDIOBJ)a->menu_font : GetStockObject(DEFAULT_GUI_FONT));
    GetTextExtentPoint32W(dc, texts[nt - 1], lstrlenW(texts[nt - 1]), &sz);
    if (!GetTextMetricsW(dc, &tm))
        tm.tmHeight = sz.cy;
    /* XP: y = SM_CYFRAME + SM_CYCAPTION + (SM_CYMENU - tmHeight) / 2, i.e. from the top of the menu
     * bar of a captioned window; in full screen (no frame, no caption) the same from the bar's top;
     * else centred in the bar */
    if (a->fullscreen)
        y = bar.top + (GetSystemMetrics(SM_CYMENU) - tm.tmHeight) / 2;
    else
        y = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CYCAPTION) +
            (GetSystemMetrics(SM_CYMENU) - tm.tmHeight) / 2;
    if (y < bar.top - 1 || y + sz.cy > bar.bottom)
        y = bar.top + (bar.bottom - bar.top - sz.cy) / 2;
    /* never overlap the menu items ("Game", "Help") on the text's row */
    items = bar.left;
    n = GetMenuItemCount(a->menu);
    for (i = 0; i < n; i++) {
        RECT ir;
        if (!GetMenuItemRect(h, a->menu, (UINT)i, &ir))
            continue;
        OffsetRect(&ir, -wr.left, -wr.top);
        if (ir.top < y + sz.cy && ir.bottom > y && ir.right > items)
            items = ir.right;
    }
    for (i = 0; i < nt && !text; i++) {               /* the longest text that fits: drop Time, then Moves */
        SIZE ts;
        int l = lstrlenW(texts[i]);
        if (!GetTextExtentPoint32W(dc, texts[i], l, &ts))
            continue;
        if (right - ts.cx >= items + sz.cy) {         /* a gap of about two characters */
            text = texts[i];
            len = l;
            x = right - ts.cx;
        }
    }
    if (text) {
        SystemParametersInfoW(SPI_GETFLATMENU, 0, &flat, 0);
        er.left = x < a->cl_prev_left ? x : a->cl_prev_left;   /* erase a longer previous text */
        if (er.left < items + 1)
            er.left = items + 1;
        er.right = right;
        er.top = y > bar.top ? y : bar.top;
        er.bottom = y + sz.cy < bar.bottom ? y + sz.cy : bar.bottom;
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, GetSysColor(flat ? COLOR_MENUBAR : COLOR_MENU));
        SetTextColor(dc, GetSysColor(COLOR_MENUTEXT));
        ExtTextOutW(dc, x, y, ETO_OPAQUE | ETO_CLIPPED, &er, text, (UINT)len, NULL);
        a->cl_prev_left = x;
    }
    SelectObject(dc, of);
    ReleaseDC(h, dc);
}

/* "Time" ticks once a second while the clock runs and is shown: a one-shot-like WM_TIMER re-armed for
 * just after the next whole second, so the display changes on time without a faster timer. */
void clock_update(App *a)
{
    int want = a->hwnd && a->s.extras.show_time_moves && fcs_clock_running(&a->s) && !IsIconic(a->hwnd);
    if (want) {
        uint32_t ms = fcs_elapsed_ms(&a->s);
        SetTimer(a->hwnd, FC_TIMER_CLOCK, 1000u - ms % 1000u + 15u, NULL);
        a->clock_timer = 1;
    } else if (a->clock_timer) {
        if (a->hwnd)
            KillTimer(a->hwnd, FC_TIMER_CLOCK);
        a->clock_timer = 0;
    }
}
