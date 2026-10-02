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
 * Animation (layout.md §7, ROADMAP 2d): every step of a move is a flight of the engine's scheduler
 * (engine/win32/anim.h): eased (standard easing between piles, decelerating into a home cell), as long
 * as XP's straight flight of the same distance at most (and never over 160 ms), and overlapping: the
 * next step's card takes off when the previous one has flown 60% of its time. The cards in the air are
 * kept out of the back buffer (view_state: hidden at the source until the session moves them, then at
 * the destination until they land), each frame composes "back buffer region + cards" in a small scratch
 * DIB which is BitBlt'ed to the window (only the dirty rects, nothing flickers), and a card that lands
 * is rendered into the back buffer (flight_land). The session call's flights all land before anything
 * else happens (view_anim_idle: after the input, before a dialog). Frames are paced against timeGetTime
 * at FC_FRAME_MS each, with the system timer at 1 ms (timeBeginPeriod) while cards fly: a plain
 * Sleep(10) lasts a whole clock tick on XP (10–15.6 ms). Quick play flies nothing.
 *
 * The Finish button (2d, always there while Game > Finish is enabled): an XP push button drawn into the
 * back buffer in the gap below the king (engine/win32/button.h), redrawn whenever its look changes.
 *
 * Enhanced animations (2d, extras.enhanced_anim, off by default): a soft shadow under dragged cards, a
 * new deal flying in from below the board, and the hint's flash as a soft pulse (the inversion fading in
 * and out over PULSE_MS instead of blinking).
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
    a->pulse_col = a->pulse_pos = -1;
    if (a->anim_slow < 1)
        a->anim_slow = 1;
    a->menu_undo = a->menu_redo = a->menu_restart = -1;
    a->menu_hint = a->menu_finish = -1;
    a->quality = -1;
    green_brush = CreateSolidBrush(RGB(0, 127, 0));
    ce_menubar_init(&a->mbt);
    return 1;
}

void view_free(App *a)
{
    ce_flights_drop(&a->fl);
    ce_tbutton_free(&a->fbtn);
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

/* The Finish button's place (2d): centred in the gap between the free cells and the home cells, below
 * the king's frame (nothing is ever drawn there); 56 x 20 XP pixels with an 11-px font at s = 1. No room
 * (a small board): no button. */
static void button_place(App *a)
{
    const FcLayout *l = &a->L;
    int gx0 = l->top[3].x + l->top[3].w, gx1 = l->top[4].x;
    int gy0 = l->king_frame.y + l->king_frame.h, gy1 = l->ch;
    int m = (int)(2 * l->s + 0.5), bw, bh, font;
    WCHAR text[32];
    if (m < 1)
        m = 1;
    bw = (int)(56 * l->s + 0.5);
    bh = (int)(20 * l->s + 0.5);
    if (bw > gx1 - gx0 - 2 * m)
        bw = gx1 - gx0 - 2 * m;
    if (bh > gy1 - gy0 - 2 * m)
        bh = gy1 - gy0 - 2 * m;
    font = (int)(11 * l->s + 0.5);
    if (font > bh - 4)
        font = bh - 4;
    load_wstr(a, IDS_FINISHBTN, text, 32, L"Finish");
    ce_tbutton_set_text(&a->fbtn, text);
    if (bw < 36 || bh < 14 || font < 8)
        ce_tbutton_place(&a->fbtn, ce_rect(0, 0, 0, 0), 8);
    else
        ce_tbutton_place(&a->fbtn, ce_rect(gx0 + (gx1 - gx0 - bw) / 2, gy0 + (gy1 - gy0 - bh) / 2, bw, bh), font);
    ce_tbutton_reset(&a->fbtn, a->hwnd);
    a->btn_drawn = 0;
}

void view_resize(App *a, int w, int h)
{
    int q = ce_backbuf_quality(&a->bb);
    double t0, t1;
    if (w <= 0 || h <= 0)
        return;
    if (a->have_layout && a->L.client_w == w && a->L.client_h == h && a->quality == q &&
        a->L.large_print == (a->s.extras.large_print != 0))
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
    ce_flights_drop(&a->fl);                          /* (their rects are stale: the board shows them) */
    fc_layout_compute_ex(&a->L, w, h, a->s.extras.large_print);
    a->have_layout = 1;
    a->layout_gen++;
    a->quality = q;
    ce_backbuf_note_layout(&a->bb);
    fc_render_prepare(a->cs, &a->L, q);
    button_place(a);
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

void view_large_print(App *a)
{
    int on = a->s.extras.large_print != 0;
    RECT cr;
    if ((!a->cs || ce_cardset_faces(fc_cardset_cards(a->cs)) == (on ? CE_FACES_LARGE : CE_FACES_NORMAL)) &&
        (!a->have_layout || a->L.large_print == on))
        return;                                       /* unchanged */
    if (a->cs && ce_cardset_faces(fc_cardset_cards(a->cs)) != (on ? CE_FACES_LARGE : CE_FACES_NORMAL)) {
        if (!ce_cardset_set_faces(fc_cardset_cards(a->cs), on ? CE_FACES_LARGE : CE_FACES_NORMAL))
            ce_log("large print: the faces are missing");
        else
            ce_log("large print %s", on ? "on" : "off");
    }
    if (a->have_layout && a->L.large_print != on && a->hwnd && !IsIconic(a->hwnd) && GetClientRect(a->hwnd, &cr))
        view_resize(a, cr.right, cr.bottom);          /* the new column step */
    view_invalidate_all(a);                           /* the new faces everywhere */
}

void view_exit_sizemove(App *a)
{
    int w, h;
    if (ce_backbuf_exit_sizemove(&a->bb, a->hwnd, &w, &h))
        view_resize(a, w, h);                         /* quality 1, exact buffer */
}

/* ---- what is shown ----------------------------------------------------------------------------------- */

/* A flight's own value (CeFlight.user): the card, where it comes from and where it goes; deal = a card
 * of a new deal flying in (nothing to hide at a source). */
static int fl_pack(int card, int sc, int sp, int dc, int dp, int deal)
{
    return (card & 63) | (sc & 15) << 6 | (sp & 63) << 10 | (dc & 15) << 16 | (dp & 63) << 20 | (deal ? 1 << 26 : 0);
}

typedef struct FlInfo { int card, sc, sp, dc, dp, deal; } FlInfo;

static FlInfo fl_unpack(int u)
{
    FlInfo i;
    i.card = u & 63;
    i.sc = (u >> 6) & 15;
    i.sp = (u >> 10) & 63;
    i.dc = (u >> 16) & 15;
    i.dp = (u >> 20) & 63;
    i.deal = (u >> 26) & 1;
    return i;
}

static int pile_key(int col, int pos) { return col == 0 ? 10 + pos : col; }

static Card card_at(const FcBoard *b, int col, int pos)
{
    if (col < 0 || col > 8 || pos < 0 || pos >= (col == 0 ? 8 : FC_COLLEN))
        return FC_EMPTY;
    return b->board[col][pos];
}

static int enhanced(App *a) { return a->s.extras.enhanced_anim != 0; }

/* The hint's soft pulse (Enhanced animations): while the session's flash runs, its "on" steps fade the
 * inversion in and its "off" steps fade it out, PULSE_MS each (eased), on the cells it last showed; any
 * other end of the flash cuts it. FC_TIMER_PULSE drives the frames while a fade is under way. */
#define PULSE_MS 70

static int pulse_level(App *a, DWORD now)
{
    int p = ce_ease(CE_EASE_STANDARD, (int)(now - a->pulse_t0), PULSE_MS);
    return a->pulse_from + (int)(((long long)(a->pulse_to - a->pulse_from) * p) >> 16);
}

static void pulse(App *a, FcView *v)
{
    DWORD now = timeGetTime();
    int on = v->hint_col >= 0, level, same = a->pulse_col == v->hint_col && a->pulse_pos == v->hint_pos;
    if (!enhanced(a) || a->s.as.hint != FCS_HINT_FLASH) {
        a->pulse_col = a->pulse_pos = -1;
        if (a->pulse_timer && a->hwnd)
            KillTimer(a->hwnd, FC_TIMER_PULSE);
        a->pulse_timer = 0;
        return;                                       /* XP's blinks (or nothing) */
    }
    if (on && (!same || a->pulse_to != 256)) {
        a->pulse_from = same && a->pulse_col >= 0 ? pulse_level(a, now) : 0;
        a->pulse_to = 256;
        a->pulse_t0 = now;
        a->pulse_col = v->hint_col;
        a->pulse_pos = v->hint_pos;
    } else if (!on && a->pulse_col >= 0 && a->pulse_to != 0) {
        a->pulse_from = pulse_level(a, now);
        a->pulse_to = 0;
        a->pulse_t0 = now;
    }
    if (a->pulse_col < 0)
        return;
    level = pulse_level(a, now);
    v->hint_col = a->pulse_col;
    v->hint_pos = a->pulse_pos;
    v->hint_level = level;
    if (level == a->pulse_to) {
        if (a->pulse_to == 0)
            a->pulse_col = a->pulse_pos = -1;
        if (a->pulse_timer && a->hwnd)
            KillTimer(a->hwnd, FC_TIMER_PULSE);
        a->pulse_timer = 0;
    } else if (!a->pulse_timer && a->hwnd) {
        a->pulse_timer = SetTimer(a->hwnd, FC_TIMER_PULSE, 15, NULL) != 0;
    }
}

void view_pulse_tick(App *a)
{
    view_sync(a);
}

/* The board the back buffer shows and its view: the session's, without the cards in the air. */
static const FcBoard *view_state(App *a, FcView *v)
{
    const FcBoard *b = &a->s.board;
    FcsViewState vs;
    int i;
    fcs_view_state(&a->s, &vs);
    fc_view_init(v);
    v->sel_col = vs.sel_col;
    v->sel_pos = vs.sel_pos;
    v->peek_col = vs.peek_col;
    v->peek_pos = vs.peek_pos;
    v->king = vs.king;
    v->big_king = vs.big_king;
    v->no_game = vs.no_game;
    for (i = 0; i < a->fl.n; i++) {
        FlInfo f = fl_unpack(a->fl.f[i].user);
        if (!f.deal && card_at(&a->s.board, f.sc, f.sp) == f.card) {
            /* not moved yet: lifted off its pile (a buried card: the column closes up, a cheat's sweep) */
            if (f.sc >= 1 && f.sp != fc_last_index(&a->s.board, f.sc)) {
                int k;
                a->anim_board = a->s.board;
                for (k = f.sp; k < FC_COLLEN - 1; k++)
                    a->anim_board.board[f.sc][k] = a->anim_board.board[f.sc][k + 1];
                a->anim_board.board[f.sc][FC_COLLEN - 1] = FC_EMPTY;
                b = &a->anim_board;
            } else {
                v->hide_col = f.sc;
                v->hide_pos = f.sp;
            }
        } else if (f.dc == 0) {
            v->hide_top[f.dp & 7]++;
        } else {
            v->hide_n[f.dc]++;
        }
    }
    if (a->drag_on && v->hide_col < 0) {              /* v1.4: the lifted cards */
        v->hide_col = a->drag_col;
        v->hide_pos = a->drag_first;
    }
    v->hint_col = vs.hint_col;
    v->hint_pos = vs.hint_pos;
    pulse(a, v);
    return b;
}

/* The Finish button's look now: 0 not shown, else 1 + hot + 2 pressed. */
static int button_look(App *a)
{
    a->fbtn.shown = a->finish_avail && !a->s.busy && a->have_layout && a->s.game_number != 0 && a->fbtn.r.w > 0;
    if (!a->fbtn.shown)
        return 0;
    return 1 + (a->fbtn.hot != 0) + 2 * (a->fbtn.pressed && a->fbtn.armed);
}

typedef struct Dirty {
    int      full, king, button;
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
        if (b->board[0][i] != ob->board[0][i] || v->hide_top[i] != ov->hide_top[i])
            d->top |= 1u << i;
    for (i = 1; i <= 8; i++)
        if (memcmp(b->board[i], ob->board[i], sizeof b->board[i]) != 0 || v->hide_n[i] != ov->hide_n[i])
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
    if (v->hint_col != ov->hint_col || v->hint_pos != ov->hint_pos || v->hint_level != ov->hint_level) {
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
    if (d->button && a->fbtn.r.w > 0)
        r[n++] = a->fbtn.r;
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
    int n, i, look;
    a->dirty = 0;
    if (!a->have_layout || !a->bb.bits)
        return 0;                                     /* (without a card set yet: table only) */
    b = view_state(a, &v);
    diff(a, b, &v, &d);
    look = button_look(a);
    d.button = look != a->btn_drawn;
    n = dirty_rects(a, &d, out);
    if (n > 0) {
        double t0 = ce_now_ms();
        GdiFlush();
        for (i = 0; i < n; i++) {
            fc_render_board_rect(&a->bb.fb, &a->L, b, &v, a->cs, out[i]);
            if (look)
                ce_tbutton_draw(&a->fbtn, &a->bb, a->hwnd, out[i]);
        }
        if (d.full)
            ce_log("full render %dx%d q%d: %.2f ms", a->bb.fb.w, a->bb.fb.h, a->quality, ce_now_ms() - t0);
        else
            ce_log("partial render, %d region(s): %.2f ms", n, ce_now_ms() - t0);
    }
    a->btn_drawn = look;
    a->drawn_board = *b;
    a->drawn_view = v;
    a->drawn_valid = 1;
    return n;
}

static void deal_flights(App *a);

void view_sync(App *a)
{
    CeRect r[MAX_DIRTY];
    int n, i;
    if (a->s.as.deals != a->drawn_deals) {            /* a new deal (Enhanced animations: it flies in) */
        a->drawn_deals = a->s.as.deals;
        if (enhanced(a) && a->drawn_valid)
            deal_flights(a);
    }
    n = sync_render(a, r);
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

/* ---- the Finish button (2d) -------------------------------------------------------------------------- */

void view_finish_avail(App *a, int on)
{
    if (a->finish_avail == (on != 0))
        return;
    a->finish_avail = on != 0;
    if (!on)
        ce_tbutton_reset(&a->fbtn, a->hwnd);
    a->dirty = 1;                                     /* the next sync draws or removes it */
}

/* Mouse messages for the button: a press on it (not the activation click XP swallows, not while cards
 * fly or are dragged) is the button's; a release over it is Game > Finish. */
int view_button_mouse(App *a, UINT m, LPARAM lp)
{
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp), r;
    if ((m == WM_LBUTTONDOWN || m == WM_LBUTTONDBLCLK) &&
        (a->in_modal || !a->have_layout || !a->cs || a->s.busy || a->drag_on || a->press_armed || a->s.swallow_click ||
         button_look(a) == 0))
        return 0;
    if (m == WM_MOUSELEAVE) {
        x = y = -1;
    }
    r = ce_tbutton_mouse(&a->fbtn, a->hwnd, m, x, y);
    if (r & CE_TBUTTON_REDRAW)
        view_sync(a);
    if (r & CE_TBUTTON_CLICK) {
        ce_log("Finish button");
        PostMessageW(a->hwnd, WM_COMMAND, MAKEWPARAM(IDM_FINISH, 0), 0);
    }
    return (r & CE_TBUTTON_USED) != 0;
}

/* ---- card flights (2d) ------------------------------------------------------------------------------- */

#define FC_FRAME_MS     10                      /* DESIGN.md: ~10 ms per frame */
#define FC_DEAL_STAGGER 6                       /* Enhanced animations: a deal's cards take off 6 ms apart */

static int flight_abort(void *ctx)
{
    App *a = ctx;
    return a->layout_gen != a->fl_gen || !a->hwnd;
}

/* A card landed: the back buffer shows it now (its region re-rendered, presented with the next frame). */
static void flight_land(void *ctx, const CeFlight *f)
{
    App *a = ctx;
    CeRect r[MAX_DIRTY];
    int n = sync_render(a, r), i;
    (void)f;
    for (i = 0; i < n; i++)
        ce_flights_dirty(&a->fl, r[i]);
}

static int animatable(App *a)
{
    return !a->s.opts.quick && a->have_layout && a->cs && a->bb.bits && a->hwnd && !IsIconic(a->hwnd) &&
           IsWindowVisible(a->hwnd);
}

static void flights_ready(App *a)
{
    if (a->fl.n == 0)
        a->fl_gen = a->layout_gen;
    a->fl.clock = &a->anim;
    a->fl.bb = &a->bb;
    a->fl.hwnd = a->hwnd;
    a->fl.frame_ms = FC_FRAME_MS;
    a->fl.land = flight_land;
    a->fl.abort = flight_abort;
    a->fl.ctx = a;
}

void view_anim_idle(App *a)
{
    if (a->fl.n > 0 || a->fl.ndirty > 0) {
        flights_ready(a);
        if (ce_flights_land_all(&a->fl) < 0)
            view_invalidate_all(a);                   /* stopped (a new layout): start over cleanly */
    }
    ce_anim_idle(&a->anim);
}

void view_anim_drop(App *a)
{
    ce_flights_drop(&a->fl);
    ce_anim_idle(&a->anim);
}

/* The flight's time: the distance's, eased, never slower than XP's straight flight (engine/ease.h). */
static int flight_ms(App *a, CeRect from, CeRect to)
{
    int dx = to.x - from.x, dy = to.y - from.y;
    int dist = ce_isqrt((unsigned long)dx * (unsigned long)dx + (unsigned long)dy * (unsigned long)dy);
    return ce_flight_ms(dist, (int)(a->L.s * 1000 + 0.5), a->L.anim_px_per_frame, FC_FRAME_MS) * a->anim_slow;
}

void view_animate_step(App *a, const FcStep *st, int forward)
{
    const FcBoard *b = &a->s.board;
    const CeImage *sprite;
    CeFlight *f;
    FcBoard after;
    CeRect from, to, r[MAX_DIRTY];
    int fcol, fpos, tcol, tpos, n, i, dur;
    double t0;
    int dropped = 0;
    CeRect drop_r = ce_rect(0, 0, 0, 0);
    if (!animatable(a)) {
        if (a->fl.n > 0) {                            /* (minimized meanwhile: the rest just appears) */
            ce_flights_drop(&a->fl);
            view_invalidate_all(a);
        }
        return;
    }
    t0 = ce_now_ms();
    flights_ready(a);
    if (a->drag_on) {                                 /* v1.4: autoplay after a drop: the dropped cards are */
        drop_r = ce_drag_cover(&a->drag);             /* in the board now; the first frame shows them there */
        ce_drag_free(&a->drag);
        a->drag_on = 0;
        dropped = 1;
    }
    fcol = forward ? st->src_col : st->dst_col;
    fpos = forward ? st->src_pos : st->dst_pos;
    tcol = forward ? st->dst_col : st->src_col;
    tpos = forward ? st->dst_pos : st->src_pos;
    ce_flights_release(&a->fl);                       /* the session has moved the previous cards */
    /* a card still landing on this card's pile lands first */
    if (ce_flights_settle(&a->fl, pile_key(fcol, fpos)) < 0)
        view_invalidate_all(a);
    flights_ready(a);
    after = *b;
    if (forward)
        fc_step_apply(&after, st);
    else
        fc_step_unapply(&after, st);
    from = fc_layout_card_rect(&a->L, b, fcol, fpos);
    to = fc_layout_card_rect(&a->L, &after, tcol, tpos);   /* where it lands, after the move */
    sprite = fc_cardset_card(a->cs, st->card);
    dur = flight_ms(a, from, to);
    f = ce_flights_add(&a->fl, sprite, from, to, dur, tcol == 0 && tpos >= 4 ? CE_EASE_DECEL : CE_EASE_STANDARD,
                       pile_key(fcol, fpos), pile_key(tcol, tpos), fl_pack(st->card, fcol, fpos, tcol, tpos, 0));
    if (!f)
        return;
    a->cursor = a->cur_wait;                          /* input is blocked while the card flies */
    if (ce_mouse_over(a->hwnd, NULL))
        SetCursor(a->cursor);
    n = sync_render(a, r);                            /* the board without the card, + what changed */
    for (i = 0; i < n; i++)
        ce_flights_dirty(&a->fl, r[i]);
    if (dropped)
        ce_flights_dirty(&a->fl, drop_r);
    if (ce_flights_run(&a->fl, ce_flights_next(f)) < 0)
        view_invalidate_all(a);                       /* resized mid-flight: start over cleanly */
    ce_log("flight %d,%d -> %d,%d: %d ms %s, %d in the air, %.1f ms", fcol, fpos, tcol, tpos, dur,
           tcol == 0 && tpos >= 4 ? "decel" : "std", a->fl.n, ce_now_ms() - t0);
}

/* Enhanced animations: the new deal's cards fly in from below the middle of the board, row by row as
 * they were dealt, FC_DEAL_STAGGER ms apart, each decelerating into its place. */
static void deal_flights(App *a)
{
    const FcBoard *b = &a->s.board;
    CeRect from, r[MAX_DIRTY];
    DWORD base;
    int row, col, k = 0, n, i;
    double t0 = ce_now_ms();
    if (!animatable(a) || a->s.game_number == 0)
        return;
    flights_ready(a);
    ce_flights_land_all(&a->fl);
    flights_ready(a);
    from = ce_rect(a->L.board_x + (a->L.board_w - a->L.cw) / 2, a->L.client_h, a->L.cw, a->L.ch);
    base = timeGetTime();
    for (row = 0; row < FC_COLLEN; row++)
        for (col = 1; col <= 8; col++) {
            Card c = b->board[col][row];
            CeRect to;
            CeFlight *f;
            DWORD want;
            if (c == FC_EMPTY)
                continue;
            to = fc_layout_card_rect(&a->L, b, col, row);
            f = ce_flights_add(&a->fl, fc_cardset_card(a->cs, c), from, to, flight_ms(a, from, to), CE_EASE_DECEL, -1,
                               col, fl_pack(c, 0, 0, col, row, 1));
            if (!f)
                continue;
            f->early = 0;
            f->hold = 0;
            want = base + (DWORD)(k++ * FC_DEAL_STAGGER * a->anim_slow);
            if ((LONG)(want - f->t0) > 0)
                f->t0 = want;
        }
    n = sync_render(a, r);
    for (i = 0; i < n; i++)
        ce_flights_dirty(&a->fl, r[i]);
    if (ce_flights_land_all(&a->fl) < 0)
        view_invalidate_all(a);
    ce_anim_idle(&a->anim);
    ce_log("deal animation: %d cards, %.1f ms", k, ce_now_ms() - t0);
}

/* ---- drag and drop (v1.4) ---------------------------------------------------------------------------- */

/* Enhanced animations: the lifted cards cast a soft shadow (35%, blurred 3 XP px, 3 right and 4 down). */
static void drag_shadow(App *a)
{
    int rad = (int)(3 * a->L.s + 0.5), dx = (int)(3 * a->L.s + 0.5), dy = (int)(4 * a->L.s + 0.5);
    CeImage *sh;
    if (!enhanced(a) || !a->drag.sprite)
        return;
    sh = ce_image_shadow(a->drag.sprite, rad, 90);
    if (sh)
        ce_drag_set_shadow(&a->drag, sh, dx - 2 * rad, dy - 2 * rad);
}

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
    drag_shadow(a);
    a->drag_on = 1;
    a->drag_col = col;
    a->drag_first = first;
    view_sync(a);                                     /* the board without them; WM_PAINT adds the sprite */
    if (a->drag.shadow) {
        CeRect c = ce_drag_cover(&a->drag);
        RECT rc;
        rc.left = c.x;
        rc.top = c.y;
        rc.right = c.x + c.w;
        rc.bottom = c.y + c.h;
        InvalidateRect(a->hwnd, &rc, FALSE);          /* (the shadow) */
    }
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
    CeRect to, from;
    int frames = 0, drawn, dur;
    double t0 = ce_now_ms();
    if (!a->drag_on || !a->have_layout || !a->hwnd || IsIconic(a->hwnd) || !IsWindowVisible(a->hwnd))
        return;
    to = fc_layout_card_rect(&a->L, &a->s.board, a->drag_col, a->drag_first);
    from = ce_drag_rect(&a->drag);
    dur = flight_ms(a, from, to);
    a->fl_gen = a->layout_gen;
    drawn = ce_drag_zip_back(&a->drag, &a->anim, &a->bb, a->hwnd, to.x, to.y, dur, CE_EASE_STANDARD, FC_FRAME_MS,
                             flight_abort, a, &frames);
    ce_anim_idle(&a->anim);
    ce_log("zip back col %d pos %d: %d ms, %d frames (%d drawn), %.1f ms", a->drag_col, a->drag_first, dur, frames,
           drawn, ce_now_ms() - t0);
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
