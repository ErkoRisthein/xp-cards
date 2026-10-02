/*
 * FreeCell HD — the board view: incremental rendering, painting, card flights, cursors and the
 * "Cards Left: N" text in the menu bar, on the engine's back buffer, animation clock and menu-bar text
 * (engine/win32/backbuf.h, anim.h, menubar.h).
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
 * rectangles, so the back buffer stays clean and nothing flickers (ce_anim_fly). Frames are paced
 * against timeGetTime at FC_FRAME_MS each (late frames are dropped, the landing frame never is), with
 * the system timer at 1 ms (timeBeginPeriod) while cards fly: a plain Sleep(10) lasts a whole clock
 * tick on XP (10–15.6 ms), which made flights up to twice as slow as designed.
 *
 * Drag and drop (v1.4, extras.drag_drop) is the engine's (engine/win32/drag.h, Solitaire HD's drag):
 * the lifted cards are hidden from the board (FcView hide_*: the column from the first lifted card on,
 * or the free cell) and float over the back buffer as one sprite (fc_render_stack); a refused drop
 * slides them back at the flights' speed. When the drop is followed by autoplay, the first flight
 * puts the dropped cards down (they are already at their place in the board).
 */
#include "app.h"

#include <string.h>

static HBRUSH green_brush;

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
    a->menu_undo = a->menu_redo = a->menu_restart = -1;
    a->menu_hint = a->menu_finish = -1;
    a->quality = -1;
    green_brush = CreateSolidBrush(RGB(0, 127, 0));
    ce_menubar_init(&a->mbt);
    return 1;
}

void view_free(App *a)
{
    ce_backbuf_free(&a->bb);
    a->have_layout = 0;
    ce_menubar_free(&a->mbt);
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
    int q = ce_backbuf_quality(&a->bb);
    double t0, t1;
    if (w <= 0 || h <= 0)
        return;
    if (a->have_layout && a->L.client_w == w && a->L.client_h == h && a->quality == q)
        return;                                       /* e.g. WM_SIZE after a restore: nothing new */
    if (a->drag_on || a->press_armed) {               /* v1.4: a drag cannot survive a new layout */
        ce_drag_free(&a->drag);
        a->drag_on = a->press_armed = 0;
        if (a->lcapture) {
            a->lcapture = 0;
            ReleaseCapture();
        }
        ce_log("drag abandoned (new layout)");
    }
    t0 = ce_now_ms();
    if (!ce_backbuf_ensure(&a->bb, w, h))
        return;                                       /* out of memory: keep the old view */
    fc_layout_compute(&a->L, w, h);
    a->have_layout = 1;
    a->layout_gen++;
    a->quality = q;
    ce_backbuf_note_layout(&a->bb);
    fc_render_prepare(a->cs, &a->L, q);
    t1 = ce_now_ms();
    ce_log("resize %dx%d q%d: buffer+layout %.2f ms (s=%.3f, card %dx%d)", w, h, q, t1 - t0,
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
    int w, h;
    if (ce_backbuf_exit_sizemove(&a->bb, a->hwnd, &w, &h))
        view_resize(a, w, h);                         /* quality 1, exact buffer */
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
    if (a->drag_on && a->hide_col < 0) {             /* v1.4: the lifted cards */
        v->hide_col = a->drag_col;
        v->hide_pos = a->drag_first;
    }
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

static int dirty_rects(App *a, const Dirty *d, CeRect *r)
{
    const FcLayout *l = &a->L;
    int n = 0, i;
    if (d->full) {
        r[0].x = r[0].y = 0;
        r[0].w = a->bb.fb.w;
        r[0].h = a->bb.fb.h;
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
            r[n].h = a->bb.fb.h - l->col_y0;
            if (r[n].h > 0)
                n++;
        }
    return n;
}

/* Render what changed since the last sync into the back buffer; returns the regions. */
static int sync_render(App *a, CeRect *out)
{
    const FcBoard *b;
    FcView v;
    Dirty d;
    int n, i;
    a->dirty = 0;
    if (!a->have_layout || !a->bb.bits)
        return 0;                                     /* (without a card set yet: table only) */
    b = view_board(a);
    view_state(a, &v);
    diff(a, b, &v, &d);
    n = dirty_rects(a, &d, out);
    if (n > 0) {
        double t0 = ce_now_ms();
        GdiFlush();
        for (i = 0; i < n; i++)
            fc_render_board_rect(&a->bb.fb, &a->L, b, &v, a->cs, out[i]);
        if (d.full)
            ce_log("full render %dx%d q%d: %.2f ms", a->bb.fb.w, a->bb.fb.h, a->quality, ce_now_ms() - t0);
        else
            ce_log("partial render, %d region(s): %.2f ms", n, ce_now_ms() - t0);
    }
    a->drawn_board = *b;
    a->drawn_view = v;
    a->drawn_valid = 1;
    return n;
}

void view_sync(App *a)
{
    CeRect r[MAX_DIRTY];
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
    view_sync(a);                                     /* adds the changed regions to the update region */
    dc = BeginPaint(a->hwnd, &ps);
    if (!dc)
        return;
    if (!(a->drag_on && a->have_layout && ce_drag_paint(&a->drag, &a->bb, dc, &ps.rcPaint)))
        ce_backbuf_paint(&a->bb, dc, &ps.rcPaint, a->have_layout, green_brush);
    EndPaint(a->hwnd, &ps);
}

/* ---- card flight ----------------------------------------------------------------------------------- */

#define FC_FRAME_MS 10                          /* DESIGN.md: ~10 ms per frame */

void view_anim_idle(App *a)
{
    ce_anim_idle(&a->anim);
}

/* A flight stops when the layout changes under it (resized mid-flight: the sprite and the rects are
 * stale). */
typedef struct FlightCtx { App *a; unsigned gen; } FlightCtx;

static int flight_abort(void *ctx)
{
    FlightCtx *f = ctx;
    return f->a->layout_gen != f->gen;
}

void view_animate_step(App *a, const FcStep *st, int forward)
{
    const FcBoard *b = &a->s.board;
    const CeImage *sprite;
    FcBoard after;
    CeRect from, to, r[MAX_DIRTY];
    int fcol, fpos, tcol, tpos, n, i, frames = 0, drawn = 0;
    FlightCtx fl;
    HDC dc;
    double t0;
    int dropped = 0;
    CeRect drop_r = ce_rect(0, 0, 0, 0);
    if (a->s.opts.quick || !a->have_layout || !a->cs || !a->bb.bits || !a->hwnd || IsIconic(a->hwnd) ||
        !IsWindowVisible(a->hwnd))
        return;
    t0 = ce_now_ms();
    if (a->drag_on) {                                 /* v1.4: autoplay after a drop: the dropped cards are */
        drop_r = ce_drag_rect(&a->drag);              /* in the board now; the first frame shows them there */
        ce_drag_free(&a->drag);
        a->drag_on = 0;
        dropped = 1;
    }
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
    if (ce_mouse_over(a->hwnd, NULL))
        SetCursor(a->cursor);

    fl.a = a;
    fl.gen = a->layout_gen;
    n = sync_render(a, r);                            /* board without the card, + what changed */
    sprite = fc_cardset_card(a->cs, st->card);
    dc = GetDC(a->hwnd);
    if (dc) {
        for (i = 0; i < n; i++)                       /* ... with the card still at its source */
            ce_backbuf_present(&a->bb, dc, r[i], sprite, from.x, from.y, a->L.cw, a->L.ch);
        if (dropped && ce_rect_clip(&drop_r, a->bb.fb.w, a->bb.fb.h))
            ce_backbuf_present(&a->bb, dc, drop_r, sprite, from.x, from.y, a->L.cw, a->L.ch);
        /* XP's AnimateCard: frames i = 1..N-1 at from + d*i/N, then the destination */
        drawn = ce_anim_fly(&a->anim, &a->bb, dc, from, to, a->L.anim_px_per_frame, FC_FRAME_MS, sprite,
                            flight_abort, &fl, &frames);
        ReleaseDC(a->hwnd, dc);
    }
    a->hide_col = a->hide_pos = -1;
    a->use_anim_board = 0;
    if (a->layout_gen != fl.gen)
        view_invalidate_all(a);                       /* resized mid-flight: start over cleanly */
    ce_log("flight %d,%d -> %d,%d: %d frames (%d drawn), %.1f ms", fcol, fpos, tcol, tpos, frames, drawn,
           ce_now_ms() - t0);
}

/* ---- drag and drop (v1.4) ---------------------------------------------------------------------------- */

int view_drag_begin(App *a, int col, int first, int px, int py)
{
    CeImage *sp;
    CeRect r;
    if (!a->have_layout || !a->cs || !a->bb.bits || !a->hwnd)
        return 0;
    sp = fc_render_stack(a->cs, &a->L, &a->s.board, col, first);
    if (!sp)
        return 0;
    r = fc_layout_card_rect(&a->L, &a->s.board, col, first);
    ce_drag_begin(&a->drag, sp, r.x, r.y, sp->w, sp->h, px, py);
    a->drag_on = 1;
    a->drag_col = col;
    a->drag_first = first;
    view_sync(a);                                     /* the board without them; WM_PAINT adds the sprite */
    ce_log("drag begin: col %d pos %d at %d,%d grab %d,%d", col, first, r.x, r.y, a->drag.grab_dx,
           a->drag.grab_dy);
    return 1;
}

void view_drag_move(App *a, int x, int y)
{
    if (!a->drag_on)
        return;
    ce_drag_move(&a->drag, &a->bb, a->hwnd, x, y);
    view_mouse_move(a, x, y);                         /* XP's destination cursors, the king */
    if (a->dirty)
        view_sync(a);
}

void view_drag_zip_back(App *a)
{
    CeRect to;
    FlightCtx fl;
    int frames = 0, drawn;
    double t0 = ce_now_ms();
    if (!a->drag_on || !a->have_layout || !a->hwnd || IsIconic(a->hwnd) || !IsWindowVisible(a->hwnd))
        return;
    to = fc_layout_card_rect(&a->L, &a->s.board, a->drag_col, a->drag_first);
    fl.a = a;
    fl.gen = a->layout_gen;
    drawn = ce_drag_zip_back(&a->drag, &a->anim, &a->bb, a->hwnd, to.x, to.y, a->L.anim_px_per_frame, FC_FRAME_MS,
                             flight_abort, &fl, &frames);
    ce_anim_idle(&a->anim);
    ce_log("zip back col %d pos %d: %d frames (%d drawn), %.1f ms", a->drag_col, a->drag_first, frames, drawn,
           ce_now_ms() - t0);
}

void view_drag_end(App *a)
{
    if (!a->drag_on)
        return;
    ce_drag_end(&a->drag, a->hwnd);                   /* its rect is repainted from the back buffer ... */
    a->drag_on = 0;
    view_sync(a);                                     /* ... which shows the cards again (or moved) */
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
    if (!ce_mouse_over(a->hwnd, &p)) {                /* for the next WM_SETCURSOR */
        a->cursor = cursor_for(a, fcs_cursor(&a->s, FCS_MISS, -1, 0));   /* wait / app starting / arrow */
        return;
    }
    a->cursor = cursor_for(a, cursor_at(a, p.x, p.y, 0));
    SetCursor(a->cursor);
}

/* ---- "Cards Left: N" in the menu bar (layout.md §8), and the extras "Moves" / "Time" ------------- */

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
        ce_to_wide(tb, t, 32);
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
    WCHAR texts[3][192];
    const WCHAR *t[3];
    int i, n;
    HWND h = a->hwnd;
    if (!h || !a->menu || IsIconic(h) || !IsWindowVisible(h))
        return;
    n = menubar_texts(a, texts);
    for (i = 0; i < n; i++)
        t[i] = texts[i];
    ce_menubar_draw(&a->mbt, h, a->menu, a->fs.on, t, n);
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
