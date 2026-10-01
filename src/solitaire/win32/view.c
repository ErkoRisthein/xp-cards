/*
 * Solitaire HD — the board view: layout, incremental rendering, painting, drag and drop, the keyboard
 * pointer and the win cascade, on the engine's back buffer and animation clock.
 *
 * Rendering model (FreeCell HD's, docs/DESIGN.md "Rendering"). The back buffer always holds the board
 * as last rendered; drawn_board / drawn_view record what that was. view_sync() compares them with the
 * session and re-renders only the piles that differ (a top-row pile's rect, a tableau column's band
 * down to the bottom), plus the old and new outline of an "Outline dragging" drag, then invalidates
 * exactly that; WM_PAINT copies from the back buffer. A layout change renders everything.
 *
 * Dragging (layout.md §6). Normal dragging lifts the cards: the back buffer shows their pile without
 * them and the stack, rendered once into a sprite, floats over it (ce_backbuf_present: every frame
 * is the union of the old and new rects, composed off screen, so nothing flickers). XP grabbed the
 * stack's screen pixels; ours is drawn from the card sprites. "Outline dragging" leaves the cards in
 * place and renders XP's inverted outline and the inverted drop target as part of the board. A
 * refused drop slides the stack (or outline) back to its pile, XP's LineDDA "zip" at 36 XP px a step.
 *
 * The win cascade (layout.md §8, winanim.h) draws one card per 5-ms frame into the back buffer, never
 * erased, and presents its rect; frames that fall behind are drawn together (the trail is never
 * thinned). XP's abort set (a key, a mouse button, the menu) stops it and stays in the queue.
 */
#include "app.h"

#include <math.h>
#include <mmsystem.h>
#include <string.h>

static HBRUSH table_brush;

/* ---- init / free --------------------------------------------------------------------------------- */

void view_init(App *a)
{
    table_brush = CreateSolidBrush(RGB(0, 128, 0));
    a->quality = -1;
    a->drag_pile = a->drag_card = -1;
}

static void drag_free(App *a)
{
    ce_image_free(a->drag_sprite);
    a->drag_sprite = NULL;
    a->drag_on = 0;
}

void view_free(App *a)
{
    drag_free(a);
    ce_backbuf_free(&a->bb);
    a->have_layout = 0;
    if (table_brush)
        DeleteObject(table_brush);
    table_brush = NULL;
}

void view_anim_idle(App *a)
{
    ce_anim_idle(&a->anim);
}

static int ready(App *a) { return a->have_layout && a->gfx && a->bb.bits; }

static void invalidate(App *a, CeRect r)
{
    RECT rc;
    if (!a->hwnd || r.w <= 0 || r.h <= 0)
        return;
    rc.left = r.x;
    rc.top = r.y;
    rc.right = r.x + r.w;
    rc.bottom = r.y + r.h;
    InvalidateRect(a->hwnd, &rc, FALSE);
}

void view_invalidate_all(App *a)
{
    a->drawn_valid = 0;
    if (a->hwnd)
        InvalidateRect(a->hwnd, NULL, FALSE);
}

/* ---- layout ---------------------------------------------------------------------------------------- */

static int status_h_now(App *a) { return a->status ? a->status_h : 0; }

/* A drag cannot survive a new layout (its sprite and positions are stale): the cards go back. */
static void drag_abandon(App *a)
{
    if (!sol_dragging(&a->s))
        return;
    if (a->lcapture) {
        a->lcapture = 0;
        ReleaseCapture();
    }
    sol_cancel_drag(&a->s);
    ce_log("drag abandoned (new layout)");
}

void view_resize(App *a, int w, int h)
{
    int q = ce_backbuf_quality(&a->bb);
    double t0;
    if (w <= 0 || h <= 0)
        return;
    if (a->have_layout && a->L.client_w == w && a->L.client_h == h && a->quality == q &&
        a->L.status_h == status_h_now(a)) {
        status_place(a);
        return;                                       /* e.g. WM_SIZE after a restore: nothing new */
    }
    drag_abandon(a);
    t0 = ce_now_ms();
    if (!ce_backbuf_ensure(&a->bb, w, h))
        return;                                       /* out of memory: keep the old view */
    sol_layout_compute(&a->L, w, h, status_h_now(a));
    a->have_layout = 1;
    a->layout_gen++;
    a->quality = q;
    ce_backbuf_note_layout(&a->bb);
    if (a->gfx)
        sol_render_prepare(a->gfx, &a->L, q);
    status_place(a);
    ce_log("resize %dx%d q%d status %d: buffer+layout %.2f ms (s=%.3f, card %dx%d)", w, h, q, a->L.status_h,
           ce_now_ms() - t0, a->L.s, a->L.cw, a->L.ch);
    view_invalidate_all(a);
}

void view_relayout(App *a)
{
    RECT cr;
    if (!a->hwnd || IsIconic(a->hwnd) || !GetClientRect(a->hwnd, &cr))
        return;
    a->quality = -1;                                  /* force it */
    view_resize(a, cr.right, cr.bottom);
}

void view_exit_sizemove(App *a)
{
    int w, h;
    if (ce_backbuf_exit_sizemove(&a->bb, a->hwnd, &w, &h))
        view_resize(a, w, h);                         /* quality 1, exact buffer */
}

/* The card set was decoded after the window appeared: size its sprites and repaint. */
void view_gfx_ready(App *a)
{
    if (a->have_layout && a->gfx)
        sol_render_prepare(a->gfx, &a->L, a->quality);
    view_invalidate_all(a);
}

/* ---- the drag as the view shows it ------------------------------------------------------------------ */

static CeRect drag_rect(App *a)
{
    int t = a->drag_outline ? a->L.line : 0;
    return ce_rect(a->drag_x, a->drag_y, a->drag_w + t, a->drag_h + t);
}

static void drag_begin(App *a)
{
    SolSession *s = &a->s;
    int x, y, fan = sol_waste_fan(s);
    a->drag_pile = s->drag_pile;
    a->drag_card = s->drag_index;
    a->drag_outline = s->opts.outline;
    sol_layout_card_pos(&a->L, &s->board, fan, a->drag_pile, a->drag_card, &x, &y);
    sol_layout_stack_size(&a->L, &s->board, fan, a->drag_pile, a->drag_card, &a->drag_w, &a->drag_h);
    a->drag_x = x;
    a->drag_y = y;
    if (a->press_valid) {                             /* the mouse: the grab offset is kept (XP) */
        a->grab_dx = a->press_x - x;
        a->grab_dy = a->press_y - y;
    } else {                                          /* the keyboard: the card's top centre */
        a->grab_dx = a->L.cw / 2;
        a->grab_dy = 0;
    }
    a->drag_sprite = NULL;
    if (!a->drag_outline) {
        a->drag_sprite = sol_render_stack(a->gfx, &a->L, &s->board, fan, s->back, a->drag_pile, a->drag_card);
        if (!a->drag_sprite)
            a->drag_outline = 1;                      /* no memory for the image: XP's outline fallback */
    }
    a->drag_on = 1;
    ce_log("drag begin: pile %d card %d (%d cards) at %d,%d grab %d,%d %s", a->drag_pile, a->drag_card,
           s->drag_count, x, y, a->grab_dx, a->grab_dy, a->drag_outline ? "outline" : "lifted");
}

static void drag_end(App *a)
{
    if (!a->drag_outline)
        invalidate(a, drag_rect(a));                  /* the floating stack goes (the board is in the buffer) */
    drag_free(a);
}

/* Follow the session: a drag began, ended, or changed under us. */
static void drag_track(App *a)
{
    SolSession *s = &a->s;
    int on = sol_dragging(s);
    if (a->drag_on && (!on || s->drag_pile != a->drag_pile || s->drag_index != a->drag_card))
        drag_end(a);
    if (on && !a->drag_on)
        drag_begin(a);
}

/* ---- what is shown ----------------------------------------------------------------------------------- */

static void view_state(App *a, SolView *v)
{
    const SolSession *s = &a->s;
    sol_view_init(v);
    v->dealt = sol_board_visible(s);
    v->waste_fan = sol_waste_fan(s);
    v->back = s->back;
    v->stock_x = sol_stock_symbol(s) == SOL_STOCK_X;
    if (a->drag_on) {
        v->drag_pile = a->drag_pile;
        v->drag_card = a->drag_card;
        v->drag_x = a->drag_x;
        v->drag_y = a->drag_y;
        v->drag_mode = a->drag_outline ? SOL_DRAG_OUTLINE : SOL_DRAG_LIFTED;
        v->target = a->drag_outline ? s->target : -1;   /* XP inverts the target in outline mode only */
    }
}

#define MAX_DIRTY 24

/* The region a pile's drawing can touch: a top-row pile's rect (it holds the 3-D edges and the
 * waste's fan), a tableau column's band from the tableau top to the bottom of the client. */
static CeRect pile_region(App *a, int k)
{
    const SolLayout *l = &a->L;
    if (k < SOL_TAB0)
        return l->pile[k];
    return ce_rect(l->pile[k].x, l->tab_y, l->cw, a->bb.fb.h - l->tab_y);
}

static int lifted_pile(const SolView *v) { return v->drag_mode == SOL_DRAG_OUTLINE ? -1 : v->drag_pile; }
static int outline_on(const SolView *v) { return v->drag_pile >= 0 && v->drag_mode == SOL_DRAG_OUTLINE; }

static int view_diff(App *a, const SolBoard *b, const SolView *v, CeRect *out)
{
    const SolBoard *ob = &a->drawn_board;
    const SolView *ov = &a->drawn_view;
    unsigned piles = 0;
    int n = 0, k;
    if (!a->drawn_valid || v->dealt != ov->dealt || v->back != ov->back) {
        out[0] = ce_rect(0, 0, a->bb.fb.w, a->bb.fb.h);
        return 1;
    }
    if (!v->dealt)
        return 0;                                     /* the table alone: nothing else is drawn */
    for (k = 0; k < SOL_NPILES; k++)
        if (b->p[k].n != ob->p[k].n || memcmp(b->p[k].c, ob->p[k].c, b->p[k].n) != 0)
            piles |= 1u << k;
    if (v->waste_fan != ov->waste_fan)
        piles |= 1u << SOL_WASTE;
    if (v->stock_x != ov->stock_x)
        piles |= 1u << SOL_STOCK;
    if (lifted_pile(v) != lifted_pile(ov) || (lifted_pile(v) >= 0 && v->drag_card != ov->drag_card)) {
        if (lifted_pile(ov) >= 0)
            piles |= 1u << lifted_pile(ov);
        if (lifted_pile(v) >= 0)
            piles |= 1u << lifted_pile(v);
    }
    if (v->target != ov->target) {
        if (ov->target >= 0)
            piles |= 1u << ov->target;
        if (v->target >= 0)
            piles |= 1u << v->target;
    }
    if ((outline_on(v) || outline_on(ov)) &&
        (outline_on(v) != outline_on(ov) || v->drag_pile != ov->drag_pile || v->drag_card != ov->drag_card ||
         v->drag_x != ov->drag_x || v->drag_y != ov->drag_y || (piles & (1u << v->drag_pile)))) {
        if (outline_on(ov))
            out[n++] = sol_render_drag_rect(&a->L, ob, ov);
        if (outline_on(v))
            out[n++] = sol_render_drag_rect(&a->L, b, v);
    }
    for (k = 0; k < SOL_NPILES && n < MAX_DIRTY; k++)
        if (piles & (1u << k))
            out[n++] = pile_region(a, k);
    return n;
}

/* Render what changed since the last sync into the back buffer; returns the regions. */
static int sync_render(App *a, CeRect *out)
{
    SolView v;
    int n, i;
    if (a->freeze || !ready(a))
        return 0;
    drag_track(a);
    view_state(a, &v);
    n = view_diff(a, &a->s.board, &v, out);
    if (n > 0) {
        double t0 = ce_now_ms();
        GdiFlush();
        for (i = 0; i < n; i++)
            if (ce_rect_clip(&out[i], a->bb.fb.w, a->bb.fb.h))
                sol_render_board_rect(&a->bb.fb, &a->L, &a->s.board, &v, a->gfx, out[i]);
        if (n == 1 && out[0].w == a->bb.fb.w && out[0].h == a->bb.fb.h)
            ce_log("full render %dx%d q%d: %.2f ms", a->bb.fb.w, a->bb.fb.h, a->quality, ce_now_ms() - t0);
        else
            ce_log("partial render, %d region(s): %.2f ms", n, ce_now_ms() - t0);
    }
    a->drawn_board = a->s.board;
    a->drawn_view = v;
    a->drawn_valid = 1;
    return n;
}

void view_sync(App *a)
{
    CeRect r[MAX_DIRTY];
    int n = sync_render(a, r), i;
    for (i = 0; i < n; i++)
        invalidate(a, r[i]);
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
    if (ready(a) && a->drag_on && a->drag_sprite) {
        CeRect r = ce_rect(ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
                           ps.rcPaint.bottom - ps.rcPaint.top);
        ce_backbuf_present(&a->bb, dc, r, a->drag_sprite, a->drag_x, a->drag_y, a->drag_w, a->drag_h);
    } else {
        ce_backbuf_paint(&a->bb, dc, &ps.rcPaint, ready(a), table_brush);
    }
    EndPaint(a->hwnd, &ps);
}

/* ---- hit testing, dragging --------------------------------------------------------------------------- */

int view_hit(App *a, int x, int y, int *pile, int *card)
{
    *pile = *card = -1;
    if (!ready(a) || !sol_board_visible(&a->s))
        return 0;
    return sol_layout_hit(&a->L, &a->s.board, sol_waste_fan(&a->s), x, y, pile, card);
}

static int accept_drop(void *ctx, int pile) { return sol_can_drop_on(&((App *)ctx)->s, pile); }

/* The pointer is at (mx, my): move the stack (keeping the grab offset), find the drop target (XP: the
 * first pile in index order whose top card or empty slot overlaps the dragged card and takes it). */
void view_drag_to(App *a, int mx, int my)
{
    CeRect old;
    int t;
    if (!a->drag_on || !sol_dragging(&a->s) || !ready(a))
        return;
    old = drag_rect(a);
    a->drag_x = mx - a->grab_dx;
    a->drag_y = my - a->grab_dy;
    t = sol_layout_drop_target(&a->L, &a->s.board, sol_waste_fan(&a->s), a->drag_x, a->drag_y, a->drag_pile,
                               accept_drop, a);
    sol_drag_over(&a->s, t);                          /* outline dragging: invalidate -> view_sync */
    if (a->drag_outline) {
        view_sync(a);                                 /* the outline's old and new rects */
        UpdateWindow(a->hwnd);
    } else {
        HDC dc = GetDC(a->hwnd);
        if (dc) {
            CeRect now = drag_rect(a);
            ce_backbuf_present(&a->bb, dc, ce_rect_union(old, now), a->drag_sprite, a->drag_x, a->drag_y,
                               a->drag_w, a->drag_h);
            ReleaseDC(a->hwnd, dc);
        }
    }
}

typedef struct ZipCtx { App *a; unsigned gen; } ZipCtx;

static int zip_abort(void *ctx)
{
    ZipCtx *z = ctx;
    return z->a->layout_gen != z->gen;
}

/* A refused drop: the cards slide back to where they came from (XP's AnimateBack, LineDDA with a redraw
 * every 36th point: 36 XP px a step), then the caller ends the drag in the session. */
void view_zip_back(App *a)
{
    int x0, y0, frames = 0, drawn = 0;
    double t0;
    ZipCtx z;
    if (!a->drag_on || !ready(a) || IsIconic(a->hwnd) || !IsWindowVisible(a->hwnd))
        return;
    sol_layout_card_pos(&a->L, &a->s.board, sol_waste_fan(&a->s), a->drag_pile, a->drag_card, &x0, &y0);
    if (x0 == a->drag_x && y0 == a->drag_y)
        return;
    t0 = ce_now_ms();
    z.a = a;
    z.gen = a->layout_gen;
    if (!a->drag_outline) {
        HDC dc = GetDC(a->hwnd);
        if (dc) {
            CeRect from = ce_rect(a->drag_x, a->drag_y, a->drag_w, a->drag_h);
            CeRect to = ce_rect(x0, y0, a->drag_w, a->drag_h);
            drawn = ce_anim_fly(&a->anim, &a->bb, dc, from, to, a->L.zip_px_per_frame, SOL_ZIP_FRAME_MS,
                                a->drag_sprite, zip_abort, &z, &frames);
            ReleaseDC(a->hwnd, dc);
        }
    } else {
        int xs = a->drag_x, ys = a->drag_y, dx = x0 - xs, dy = y0 - ys, i;
        DWORD start;
        frames = (int)(sqrt((double)dx * dx + (double)dy * dy) / a->L.zip_px_per_frame);   /* as ce_anim_fly */
        if (frames < 1)
            frames = 1;
        ce_anim_begin(&a->anim);
        start = timeGetTime();
        for (i = 1; i <= frames && !zip_abort(&z);) {
            a->drag_x = xs + dx * i / frames;
            a->drag_y = ys + dy * i / frames;
            view_sync(a);
            UpdateWindow(a->hwnd);
            drawn++;
            i = ce_anim_frame_wait(start, i, SOL_ZIP_FRAME_MS);
        }
    }
    ce_anim_idle(&a->anim);
    if (!zip_abort(&z)) {
        a->drag_x = x0;
        a->drag_y = y0;
    }
    ce_log("zip back pile %d card %d: %d frames (%d drawn), %.1f ms", a->drag_pile, a->drag_card, frames, drawn,
           ce_now_ms() - t0);
}

/* The keyboard moved its cursor (rules.md §9.1): XP puts the pointer on the card's top centre (lower
 * by the pile's dyUp while something is dragged over a non-empty pile), and the drag follows it; that
 * also sets the drop target Enter / Space uses (sol_key_drop_target). */
void view_kbd_cursor(App *a, int pile, int card, int dragging)
{
    const SolBoard *b = &a->s.board;
    int x, y, n;
    POINT p;
    if (!ready(a) || pile < 0 || pile >= SOL_NPILES)
        return;
    n = b->p[pile].n;
    if (n == 0) {
        x = a->L.pile[pile].x;
        y = a->L.pile[pile].y;
    } else {
        sol_layout_card_pos(&a->L, b, sol_waste_fan(&a->s), pile, card, &x, &y);
        /* XP adds the pile class's dyUp: a face-up step on the tableau, 1 px on a foundation (KeyHit
         * 0x1003038; the stock and the waste cannot take the cursor during a drag) */
        if (dragging)
            y += sol_is_tab(pile) ? sol_layout_col_step(&a->L, b, pile) : a->L.edge_dy;
    }
    x += a->L.cw / 2;
    ce_log("kbd cursor pile %d card %d%s -> %d,%d", pile, card, dragging ? " (dragging)" : "", x, y);
    if (dragging)
        view_drag_to(a, x, y);
    if (!a->no_warp && a->hwnd && GetForegroundWindow() == a->hwnd) {
        p.x = x;
        p.y = y;
        ClientToScreen(a->hwnd, &p);
        SetCursorPos(p.x, p.y);
    }
}

/* ---- the win cascade -------------------------------------------------------------------------------- */

/* XP's AbortPending (0x1004D43): these stop the cascade and stay in the queue; anything else is
 * dispatched. Returns 1 to stop. */
static int cascade_pump(App *a)
{
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_NOREMOVE)) {
        switch (m.message) {
        case WM_KEYDOWN: case WM_SYSKEYDOWN:
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK:
        case WM_NCLBUTTONDOWN: case WM_NCRBUTTONDOWN: case WM_NCMBUTTONDOWN:
        case WM_MENUSELECT:
            ce_log("win cascade stopped by message 0x%04x", m.message);
            return 1;
        }
        if (!PeekMessageW(&m, NULL, 0, 0, PM_REMOVE))
            break;
        if (m.message == WM_QUIT) {
            PostQuitMessage((int)m.wParam);
            return 1;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
        if (a->cascade_abort || !a->hwnd)
            return 1;
    }
    return 0;
}

void view_cascade(App *a)
{
    SolWinAnim w;
    HDC dc;
    DWORD t0;
    long drawn = 0;
    int card, x, y, done = 0;
    unsigned gen;
    double tlog;
    if (!ready(a) || !a->hwnd || IsIconic(a->hwnd) || !IsWindowVisible(a->hwnd))
        return;
    if (a->drag_on) {
        drag_end(a);                                  /* (the session ended the drag) */
        view_sync(a);
    }
    a->freeze = 1;                                    /* the back buffer keeps the board as shown */
    UpdateWindow(a->hwnd);                            /* the last move, before the first card flies */
    sol_winanim_start(&w, &a->L, &a->s.board, a->s.rng, a->s.forced_win);
    gen = a->layout_gen;
    a->cascade_abort = 0;
    tlog = ce_now_ms();
    dc = GetDC(a->hwnd);
    ce_anim_begin(&a->anim);
    t0 = timeGetTime();
    while (dc && !done) {
        long due = (long)((timeGetTime() - t0) / SOL_WINANIM_FRAME_MS) + 1;
        CeRect u = ce_rect(0, 0, 0, 0);
        int any = 0;
        LONG wait;
        if (due - drawn > 200)
            due = drawn + 200;                        /* far behind: present at least every 200 frames */
        GdiFlush();
        while (drawn < due) {
            CeRect r;
            if (!sol_winanim_frame(&w, &card, &x, &y)) {
                done = 1;
                break;
            }
            sol_render_card(&a->bb.fb, a->gfx, card, 0, x, y);
            r = ce_rect(x, y, a->L.cw, a->L.ch);
            u = any ? ce_rect_union(u, r) : r;
            any = 1;
            drawn++;
        }
        if (any && ce_rect_clip(&u, a->bb.fb.w, a->bb.fb.h))
            ce_backbuf_blit(&a->bb, dc, u.x, u.y, u.w, u.h);
        if (done)
            break;
        GdiFlush();
        if (cascade_pump(a) || a->cascade_abort || a->layout_gen != gen || !a->bb.bits || !a->hwnd)
            break;
        wait = (LONG)(t0 + (DWORD)drawn * SOL_WINANIM_FRAME_MS - timeGetTime());
        if (wait > 0)
            MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD)(wait < 100 ? wait : 100), QS_ALLINPUT);
    }
    if (dc && a->hwnd)
        ReleaseDC(a->hwnd, dc);
    ce_anim_idle(&a->anim);
    a->freeze = 0;
    a->cascade_abort = 0;
    ce_log("win cascade%s: %ld frames, %s, %.0f ms", a->s.forced_win ? " (forced)" : "", drawn,
           done ? "complete" : "stopped", ce_now_ms() - tlog);
}
