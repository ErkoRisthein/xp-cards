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
 *
 * Extras (v1.1): the hint's flash is the view's keyboard-selection overlay (SolView sel_*: the cards
 * inverted, an empty pile's slot inverted), from sol_hint_view; Finish flies each card home
 * (view_animate_move). XP has no move animation at all; only Finish, which XP does not have either,
 * animates. v1.2: the click selection is the same overlay (sol_selection), and the cards that go home
 * automatically fly as Finish's do.
 *
 * 2d (motion): the flights are the engine scheduler's (engine/win32/anim.h): decelerating into the
 * foundation, as long as the straight flight of about 60 XP px per 10-ms frame at most (never over
 * 160 ms), the next card taking off when the previous one has flown 60% of its time. The back buffer
 * shows `disp`, the session's board without the cards in the air (display_board), and a card that lands
 * is rendered into it (flight_land). The zip-back of a refused drop is eased (standard easing), as long
 * as XP's 36-px steps at most. The Finish button: an XP push button drawn into the back buffer in the
 * empty slot between the waste and the foundations while Game > Finish is enabled. Enhanced animations
 * (extras.enhanced_anim, off by default): a soft shadow under the dragged cards, a card turning over
 * (a flip: the back narrowing to its axis, the face widening from it, 120 ms), a new deal flying out of
 * the stock, and the hint's flash as a soft pulse.
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
    a->pulse_pile = a->pulse_card = -1;
    if (a->anim_slow < 1)
        a->anim_slow = 1;
}

static void drag_free(App *a)
{
    ce_drag_free(&a->drag);
    a->drag_on = 0;
}

void view_free(App *a)
{
    drag_free(a);
    ce_flights_drop(&a->fl);
    ce_tbutton_free(&a->fbtn);
    ce_backbuf_free(&a->bb);
    a->have_layout = 0;
    if (table_brush)
        DeleteObject(table_brush);
    table_brush = NULL;
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

/* The Finish button's place (2d): centred in the empty slot between the waste (with its fan) and the
 * first foundation, level with the cards' middle; 56 x 20 XP pixels with an 11-px font at s = 1. No
 * room: no button. */
static void button_place(App *a)
{
    const SolLayout *l = &a->L;
    CeRect w = l->pile[SOL_WASTE];
    int gx0 = w.x + w.w, gx1 = l->pile[SOL_FOUND0].x, m = (int)(2 * l->s + 0.5), bw, bh, font;
    WCHAR text[32];
    if (m < 1)
        m = 1;
    bw = (int)(56 * l->s + 0.5);
    bh = (int)(20 * l->s + 0.5);
    if (bw > gx1 - gx0 - 2 * m)
        bw = gx1 - gx0 - 2 * m;
    if (bh > l->ch - 2 * m)
        bh = l->ch - 2 * m;
    font = (int)(11 * l->s + 0.5);
    if (font > bh - 4)
        font = bh - 4;
    load_wstr(a, IDS_FINISHBTN, text, 32, L"Finish");
    ce_tbutton_set_text(&a->fbtn, text);
    if (bw < 36 || bh < 14 || font < 8)
        ce_tbutton_place(&a->fbtn, ce_rect(0, 0, 0, 0), 8);
    else
        ce_tbutton_place(&a->fbtn, ce_rect(gx0 + (gx1 - gx0 - bw) / 2, l->top + (l->ch - bh) / 2, bw, bh), font);
    ce_tbutton_reset(&a->fbtn, a->hwnd);
    a->btn_drawn = 0;
}

void view_resize(App *a, int w, int h)
{
    int q = ce_backbuf_quality(&a->bb);
    double t0;
    if (w <= 0 || h <= 0)
        return;
    if (a->have_layout && a->L.client_w == w && a->L.client_h == h && a->quality == q &&
        a->L.status_h == status_h_now(a) && a->L.large_print == (a->s.extras.large_print != 0)) {
        status_place(a);
        return;                                       /* e.g. WM_SIZE after a restore: nothing new */
    }
    drag_abandon(a);
    t0 = ce_now_ms();
    if (!ce_backbuf_ensure(&a->bb, w, h))
        return;                                       /* out of memory: keep the old view */
    ce_flights_drop(&a->fl);                          /* (their rects are stale: the board shows them) */
    sol_layout_compute_ex(&a->L, w, h, status_h_now(a), a->s.extras.large_print);
    a->have_layout = 1;
    a->layout_gen++;
    a->quality = q;
    ce_backbuf_note_layout(&a->bb);
    if (a->gfx)
        sol_render_prepare(a->gfx, &a->L, q);
    button_place(a);
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

void view_large_print(App *a)
{
    int on = a->s.extras.large_print != 0;
    CeCardSet *cs = a->gfx ? sol_gfx_cards(a->gfx) : NULL;
    if (cs && ce_cardset_faces(cs) != (on ? CE_FACES_LARGE : CE_FACES_NORMAL))
        ce_log(ce_cardset_set_faces(cs, on ? CE_FACES_LARGE : CE_FACES_NORMAL) ? "large print %s"
                                                                               : "large print %s: the faces are missing",
               on ? "on" : "off");
    view_relayout(a);                                 /* the face-up step; every card re-rendered */
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

static void drag_begin(App *a)
{
    SolSession *s = &a->s;
    int x, y, w, h, fan = sol_waste_fan(s);
    CeImage *sprite = NULL;
    a->drag_pile = s->drag_pile;
    a->drag_card = s->drag_index;
    a->drag_outline = s->opts.outline;
    sol_layout_card_pos(&a->L, &s->board, fan, a->drag_pile, a->drag_card, &x, &y);
    sol_layout_stack_size(&a->L, &s->board, fan, a->drag_pile, a->drag_card, &w, &h);
    if (!a->drag_outline) {
        sprite = sol_render_stack(a->gfx, &a->L, &s->board, fan, s->back, a->drag_pile, a->drag_card);
        if (!sprite)
            a->drag_outline = 1;                      /* no memory for the image: XP's outline fallback */
    }
    /* the mouse: the grab offset is kept (XP); the keyboard: the card's top centre */
    ce_drag_begin(&a->drag, sprite, x, y, w, h, a->press_valid ? a->press_x : x + a->L.cw / 2,
                  a->press_valid ? a->press_y : y);
    if (sprite && s->extras.enhanced_anim) {
        /* Enhanced animations: the lifted cards cast a soft shadow (35%, blurred 3 XP px, 3 right, 4 down) */
        int rad = (int)(3 * a->L.s + 0.5), dx = (int)(3 * a->L.s + 0.5), dy = (int)(4 * a->L.s + 0.5);
        CeImage *sh = ce_image_shadow(sprite, rad, 90);
        if (sh)
            ce_drag_set_shadow(&a->drag, sh, dx - 2 * rad, dy - 2 * rad);
    }
    a->drag_on = 1;
    ce_log("drag begin: pile %d card %d (%d cards) at %d,%d grab %d,%d %s", a->drag_pile, a->drag_card,
           s->drag_count, x, y, a->drag.grab_dx, a->drag.grab_dy, a->drag_outline ? "outline" : "lifted");
}

static void drag_end(App *a)
{
    ce_drag_end(&a->drag, a->hwnd);                   /* the floating stack goes (the board is in the buffer) */
    a->drag_on = 0;
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

/* The hint's soft pulse (Enhanced animations): while the session's flash runs, its "on" steps fade the
 * inversion in and its "off" steps fade it out, PULSE_MS each (eased), on the cards it last showed; any
 * other end of the flash cuts it. SOL_TIMER_PULSE drives the frames while a fade is under way. */
#define PULSE_MS 70

static int pulse_level(App *a, DWORD now)
{
    int p = ce_ease(CE_EASE_STANDARD, (int)(now - a->pulse_t0), PULSE_MS);
    return a->pulse_from + (int)(((long long)(a->pulse_to - a->pulse_from) * p) >> 16);
}

static void pulse_stop_timer(App *a)
{
    if (a->pulse_timer && a->hwnd)
        KillTimer(a->hwnd, SOL_TIMER_PULSE);
    a->pulse_timer = 0;
}

/* Returns 1 when the pulse decides the selection overlay (v->sel_*). */
static int pulse(App *a, SolView *v)
{
    DWORD now = timeGetTime();
    int pile, card, on, same, level;
    if (!a->s.extras.enhanced_anim || !a->s.hint) {
        a->pulse_pile = a->pulse_card = -1;
        pulse_stop_timer(a);
        return 0;                                     /* XP's blinks (or nothing) */
    }
    sol_hint_view(&a->s, &pile, &card);
    on = pile >= 0;
    same = a->pulse_pile == pile && a->pulse_card == card;
    if (on && (!same || a->pulse_to != 256)) {
        a->pulse_from = same && a->pulse_pile >= 0 ? pulse_level(a, now) : 0;
        a->pulse_to = 256;
        a->pulse_t0 = now;
        a->pulse_pile = pile;
        a->pulse_card = card;
    } else if (!on && a->pulse_pile >= 0 && a->pulse_to != 0) {
        a->pulse_from = pulse_level(a, now);
        a->pulse_to = 0;
        a->pulse_t0 = now;
    }
    if (a->pulse_pile < 0)
        return 1;
    level = pulse_level(a, now);
    v->sel_pile = a->pulse_pile;
    v->sel_card = a->pulse_card;
    v->sel_level = level;
    if (level == a->pulse_to) {
        if (a->pulse_to == 0)
            a->pulse_pile = a->pulse_card = -1;
        pulse_stop_timer(a);
    } else if (!a->pulse_timer && a->hwnd) {
        a->pulse_timer = SetTimer(a->hwnd, SOL_TIMER_PULSE, 15, NULL) != 0;
    }
    return 1;
}

void view_pulse_tick(App *a)
{
    view_sync(a);
}

static void view_state(App *a, SolView *v)
{
    const SolSession *s = &a->s;
    sol_view_init(v);
    v->dealt = sol_board_visible(s);
    v->waste_fan = sol_waste_fan(s);
    v->back = s->back;
    v->stock_x = sol_stock_symbol(s) == SOL_STOCK_X;
    if (!pulse(a, v)) {                               /* 2d, Enhanced animations: the hint's soft pulse */
        sol_hint_view(s, &v->sel_pile, &v->sel_card); /* extra: the hint's flash */
        if (v->sel_pile < 0)
            sol_selection(s, &v->sel_pile, &v->sel_card);   /* extra (v1.2): the click selection */
    }
    if (a->drag_on) {
        v->drag_pile = a->drag_pile;
        v->drag_card = a->drag_card;
        v->drag_x = a->drag.x;
        v->drag_y = a->drag.y;
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
    if (v->sel_pile != ov->sel_pile || v->sel_card != ov->sel_card || v->sel_level != ov->sel_level) {
        if (ov->sel_pile >= 0 && ov->sel_pile < SOL_NPILES)
            piles |= 1u << ov->sel_pile;
        if (v->sel_pile >= 0 && v->sel_pile < SOL_NPILES)
            piles |= 1u << v->sel_pile;
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

/* The Finish button's look now: 0 not shown, else 1 + hot + 2 pressed. */
static int button_look(App *a)
{
    a->fbtn.shown = a->finish_avail && !a->s.busy && sol_board_visible(&a->s) && a->fbtn.r.w > 0;
    if (!a->fbtn.shown)
        return 0;
    return 1 + (a->fbtn.hot != 0) + 2 * (a->fbtn.pressed && a->fbtn.armed);
}

/* Render board b (the session's without the cards in the air: display_board) where it differs from what
 * the back buffer shows; returns the regions. */
static int sync_render_board(App *a, const SolBoard *b, CeRect *out)
{
    SolView v;
    int n, i, look;
    if (a->freeze || !ready(a))
        return 0;
    drag_track(a);
    view_state(a, &v);
    n = view_diff(a, b, &v, out);
    look = button_look(a);
    if (look != a->btn_drawn && n < MAX_DIRTY && a->fbtn.r.w > 0 && !(n == 1 && out[0].w == a->bb.fb.w &&
                                                                       out[0].h == a->bb.fb.h))
        out[n++] = a->fbtn.r;
    if (n > 0) {
        double t0 = ce_now_ms();
        GdiFlush();
        for (i = 0; i < n; i++)
            if (ce_rect_clip(&out[i], a->bb.fb.w, a->bb.fb.h)) {
                sol_render_board_rect(&a->bb.fb, &a->L, b, &v, a->gfx, out[i]);
                if (look)
                    ce_tbutton_draw(&a->fbtn, &a->bb, a->hwnd, out[i]);
            }
        if (n == 1 && out[0].w == a->bb.fb.w && out[0].h == a->bb.fb.h)
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

/* ---- the cards in the air (2d) ------------------------------------------------------------------------ */

/* A flight's own value (CeFlight.user): the card, its source pile + 1 (0: none), its destination pile,
 * the kind. */
enum { FL_MOVE = 0, FL_DEAL = 1, FL_FLIP = 2 };

static int fl_pack(int card, int src, int dst, int kind)
{
    return (card & 63) | ((src + 1) & 15) << 6 | (dst & 15) << 10 | (kind & 3) << 14;
}

static void fl_unpack(int u, int *card, int *src, int *dst, int *kind)
{
    *card = u & 63;
    *src = ((u >> 6) & 15) - 1;
    *dst = (u >> 10) & 15;
    *kind = (u >> 14) & 3;
}

/* The board the back buffer shows: the session's without the cards in the air. Newest first: a card the
 * session has not moved yet is on top of its source; the others are on top of their destination (a card
 * bound for the same pile later lies above an earlier one), a dealt or turning card too. */
static const SolBoard *display_board(App *a)
{
    int i;
    if (a->fl.n == 0)
        return &a->s.board;
    a->disp = a->s.board;
    for (i = a->fl.n - 1; i >= 0; i--) {
        int card, src, dst, kind;
        SolPile *ps, *pd;
        fl_unpack(a->fl.f[i].user, &card, &src, &dst, &kind);
        ps = src >= 0 && src < SOL_NPILES ? &a->disp.p[src] : NULL;
        pd = dst < SOL_NPILES ? &a->disp.p[dst] : NULL;
        if (kind == FL_MOVE && ps && ps->n > 0 && sol_card_id(ps->c[ps->n - 1]) == card)
            ps->n--;
        else if (pd && pd->n > 0 && (kind != FL_FLIP || sol_card_id(pd->c[pd->n - 1]) == card))
            pd->n--;                                  /* (a turning card only while it is still the top) */
    }
    return &a->disp;
}

/* Render what changed since the last sync into the back buffer; returns the regions. */
static int sync_render(App *a, CeRect *out)
{
    return sync_render_board(a, display_board(a), out);
}

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

static void flights_ready(App *a)
{
    if (a->fl.n == 0)
        a->fl_gen = a->layout_gen;
    a->fl.clock = &a->anim;
    a->fl.bb = &a->bb;
    a->fl.hwnd = a->hwnd;
    a->fl.frame_ms = SOL_ZIP_FRAME_MS;
    a->fl.land = flight_land;
    a->fl.abort = flight_abort;
    a->fl.ctx = a;
}

static int animatable(App *a)
{
    return ready(a) && a->hwnd && !a->freeze && !IsIconic(a->hwnd) && IsWindowVisible(a->hwnd);
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

/* The time of a flight over that distance: eased, never slower than the straight flight of px_per_frame
 * pixels per 10-ms frame, never over 160 ms (engine/ease.h). */
static int flight_ms(App *a, CeRect from, CeRect to, int px_per_frame)
{
    int dx = to.x - from.x, dy = to.y - from.y;
    int dist = ce_isqrt((unsigned long)dx * (unsigned long)dx + (unsigned long)dy * (unsigned long)dy);
    return ce_flight_ms(dist, (int)(a->L.s * 1000 + 0.5), px_per_frame, SOL_ZIP_FRAME_MS) * a->anim_slow;
}

#define SOL_FLIP_MS       120     /* Enhanced animations: a card turning over */
#define SOL_DEAL_STAGGER  10      /* ... a deal's cards take off 10 ms apart */

/* Enhanced animations: the new deal's 28 cards fly out of the stock to their places, in XP's order (row
 * by row), SOL_DEAL_STAGGER ms apart, each decelerating; the face-down ones as backs. */
static void deal_flights(App *a)
{
    const SolBoard *b = &a->s.board;
    CeCardSet *cs = sol_gfx_cards(a->gfx);
    CeRect from, r[MAX_DIRTY];
    DWORD base;
    int row, t, k = 0, n, i, sx, sy, ppf = (int)(60 * a->L.s + 0.5);
    double t0 = ce_now_ms();
    if (!animatable(a) || !sol_board_visible(&a->s))
        return;
    flights_ready(a);
    ce_flights_land_all(&a->fl);
    flights_ready(a);
    sol_layout_card_pos(&a->L, b, 0, SOL_STOCK, b->p[SOL_STOCK].n > 0 ? b->p[SOL_STOCK].n - 1 : 0, &sx, &sy);
    from = ce_rect(sx, sy, a->L.cw, a->L.ch);
    base = timeGetTime();
    for (row = 0; row < 7; row++)
        for (t = row; t < 7; t++) {
            int pile = SOL_TAB0 + t, x, y;
            const SolPile *p = &b->p[pile];
            SolCard c;
            CeRect to;
            CeFlight *f;
            DWORD want;
            if (row >= p->n)
                continue;
            c = p->c[row];
            sol_layout_card_pos(&a->L, b, 0, pile, row, &x, &y);
            to = ce_rect(x, y, a->L.cw, a->L.ch);
            f = ce_flights_add(&a->fl, sol_is_up(c) ? ce_cardset_card(cs, sol_card_id(c)) : ce_cardset_back(cs, a->s.back),
                               from, to, flight_ms(a, from, to, ppf > 0 ? ppf : 1), CE_EASE_DECEL, -1, pile,
                               fl_pack(sol_card_id(c), -1, pile, FL_DEAL));
            if (!f)
                continue;
            f->early = 0;
            f->hold = 0;
            want = base + (DWORD)(k++ * SOL_DEAL_STAGGER * a->anim_slow);
            if ((LONG)(want - f->t0) > 0)
                f->t0 = want;
        }
    n = sync_render(a, r);
    for (i = 0; i < n; i++)
        ce_flights_dirty(&a->fl, r[i]);
    if (ce_flights_land_all(&a->fl) < 0)
        view_invalidate_all(a);
    ce_log("deal animation: %d cards, %.1f ms", k, ce_now_ms() - t0);
}

/* Enhanced animations: a column's face-down top card turned face up since the back buffer was drawn
 * (a click, the keyboard, "Turn cards over automatically", Redo) turns over in place. Returns the
 * flips added; they land with the next flight, view_anim_idle or the posted WM_APP_LAND. */
static int flip_flights(App *a)
{
    const SolBoard *ob = &a->drawn_board, *b = &a->s.board;
    CeCardSet *cs = sol_gfx_cards(a->gfx);
    int k, added = 0;
    for (k = SOL_TAB0; k < SOL_NPILES; k++) {
        /* (the back buffer shows the pile as `disp` had it: a card in the air off it makes the counts
         * differ, so only a turn in place compares equal) */
        const SolPile *op = &ob->p[k], *p = &b->p[k];
        int x, y;
        CeRect r;
        CeFlight *f;
        if (p->n == 0 || p->n != op->n || sol_is_up(op->c[op->n - 1]) || !sol_is_up(p->c[p->n - 1]) ||
            sol_card_id(op->c[op->n - 1]) != sol_card_id(p->c[p->n - 1]))
            continue;
        sol_layout_card_pos(&a->L, b, sol_waste_fan(&a->s), k, p->n - 1, &x, &y);
        r = ce_rect(x, y, a->L.cw, a->L.ch);
        f = ce_flights_add(&a->fl, ce_cardset_back(cs, a->s.back), r, r, SOL_FLIP_MS * a->anim_slow, CE_EASE_LINEAR, -1,
                           k, fl_pack(sol_card_id(p->c[p->n - 1]), -1, k, FL_FLIP));
        if (!f)
            continue;
        f->kind = CE_FLIGHT_FLIP;
        f->img2 = ce_cardset_card(cs, sol_card_id(p->c[p->n - 1]));
        f->hold = 0;
        added++;
        ce_log("card turns over: pile %d", k);
    }
    return added;
}

/* What happened to the board since it was drawn that Enhanced animations show: a new deal flies in, a
 * card turns over. */
static void detect(App *a)
{
    if (a->s.deals != a->drawn_deals) {
        a->drawn_deals = a->s.deals;
        if (a->s.extras.enhanced_anim && a->drawn_valid && animatable(a))
            deal_flights(a);
        return;
    }
    if (!a->s.extras.enhanced_anim || !a->drawn_valid || !animatable(a) || !a->drawn_view.dealt ||
        !sol_board_visible(&a->s))
        return;
    flights_ready(a);
    if (flip_flights(a) && !a->land_posted && a->hwnd) {
        a->land_posted = 1;
        PostMessageW(a->hwnd, WM_APP_LAND, 0, 0);     /* (after the call that turned it, at the latest) */
    }
}

void view_sync(App *a)
{
    CeRect r[MAX_DIRTY];
    int n, i;
    if (!a->freeze && ready(a))
        detect(a);
    n = sync_render(a, r);
    for (i = 0; i < n; i++) {
        if (a->fl.n > 0)
            ce_flights_dirty(&a->fl, r[i]);           /* presented with the flights' next frame */
        invalidate(a, r[i]);
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
    if (!(ready(a) && a->drag_on && ce_drag_paint(&a->drag, &a->bb, dc, &ps.rcPaint)))
        ce_backbuf_paint(&a->bb, dc, &ps.rcPaint, ready(a), table_brush);
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
    int t;
    if (!a->drag_on || !sol_dragging(&a->s) || !ready(a))
        return;
    ce_drag_move(&a->drag, &a->bb, a->hwnd, mx, my);  /* the lifted stack shows there at once */
    t = sol_layout_drop_target(&a->L, &a->s.board, sol_waste_fan(&a->s), a->drag.x, a->drag.y, a->drag_pile,
                               accept_drop, a);
    sol_drag_over(&a->s, t);                          /* outline dragging: invalidate -> view_sync */
    if (a->drag_outline) {
        view_sync(a);                                 /* the outline's old and new rects */
        UpdateWindow(a->hwnd);
    }
}

typedef struct ZipCtx { App *a; unsigned gen; } ZipCtx;

static int zip_abort(void *ctx)
{
    ZipCtx *z = ctx;
    return z->a->layout_gen != z->gen;
}

/* A refused drop: the cards slide back to where they came from (XP's AnimateBack, a LineDDA with a redraw
 * every 36th point: 36 XP px a step; 2d: eased, standard easing, in at most the time of XP's steps), then
 * the caller ends the drag in the session. */
static int zip_ms(App *a, int dx, int dy)
{
    int dist = ce_isqrt((unsigned long)dx * (unsigned long)dx + (unsigned long)dy * (unsigned long)dy);
    return ce_flight_ms(dist, (int)(a->L.s * 1000 + 0.5), a->L.zip_px_per_frame, SOL_ZIP_FRAME_MS) * a->anim_slow;
}

void view_zip_back(App *a)
{
    int x0, y0, frames = 0, drawn = 0, dur;
    double t0;
    ZipCtx z;
    if (!a->drag_on || !ready(a) || IsIconic(a->hwnd) || !IsWindowVisible(a->hwnd))
        return;
    sol_layout_card_pos(&a->L, &a->s.board, sol_waste_fan(&a->s), a->drag_pile, a->drag_card, &x0, &y0);
    if (x0 == a->drag.x && y0 == a->drag.y)
        return;
    t0 = ce_now_ms();
    z.a = a;
    z.gen = a->layout_gen;
    dur = zip_ms(a, x0 - a->drag.x, y0 - a->drag.y);
    if (!a->drag_outline) {
        drawn = ce_drag_zip_back(&a->drag, &a->anim, &a->bb, a->hwnd, x0, y0, dur, CE_EASE_STANDARD, SOL_ZIP_FRAME_MS,
                                 zip_abort, &z, &frames);
    } else {
        int xs = a->drag.x, ys = a->drag.y, i = 1;
        DWORD start;
        ce_anim_begin(&a->anim);
        start = timeGetTime();
        while (!zip_abort(&z)) {
            LONG t = (LONG)(timeGetTime() - start) + SOL_ZIP_FRAME_MS;   /* the position due one frame later */
            int p = ce_ease(CE_EASE_STANDARD, (int)t, dur);
            a->drag.x = ce_ease_lerp(xs, x0, p);
            a->drag.y = ce_ease_lerp(ys, y0, p);
            view_sync(a);
            UpdateWindow(a->hwnd);
            drawn++;
            if (t >= dur)
                break;
            i = ce_anim_frame_wait(start, i, SOL_ZIP_FRAME_MS);
        }
        frames = drawn;
    }
    ce_anim_idle(&a->anim);
    if (!zip_abort(&z)) {
        a->drag.x = x0;
        a->drag.y = y0;
    }
    ce_log("zip back pile %d card %d: %d ms, %d frames (%d drawn), %.1f ms", a->drag_pile, a->drag_card, dur, frames,
           drawn, ce_now_ms() - t0);
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

/* ---- the Finish button (2d) -------------------------------------------------------------------------- */

void view_finish_avail(App *a, int on)
{
    if (a->finish_avail == (on != 0))
        return;
    a->finish_avail = on != 0;
    if (!on)
        ce_tbutton_reset(&a->fbtn, a->hwnd);
    view_sync(a);                                     /* draws or removes it */
}

/* Mouse messages for the button: a press on it (not while cards fly, are dragged or a dialog is up) is
 * the button's; a release over it is Game > Finish. */
int view_button_mouse(App *a, UINT m, LPARAM lp)
{
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp), r;
    if (m == WM_LBUTTONDOWN || m == WM_LBUTTONDBLCLK)
        a->btn_down = 0;                              /* (again below if this press is the button's) */
    if ((m == WM_LBUTTONDOWN || m == WM_LBUTTONDBLCLK) &&
        (a->in_modal || !ready(a) || a->s.busy || sol_dragging(&a->s) || button_look(a) == 0))
        return 0;
    if (m == WM_MOUSELEAVE)
        x = y = -1;
    r = ce_tbutton_mouse(&a->fbtn, a->hwnd, m, x, y);
    if ((m == WM_LBUTTONDOWN || m == WM_LBUTTONDBLCLK) && (r & CE_TBUTTON_USED)) {
        a->btn_down = 1;
        a->btn_down_t = GetMessageTime();
    }
    if (r & CE_TBUTTON_REDRAW)
        view_sync(a);
    if (r & CE_TBUTTON_CLICK) {
        ce_log("Finish button");
        PostMessageW(a->hwnd, WM_COMMAND, MAKEWPARAM(IDM_FINISH, 0), 0);
    }
    return (r & CE_TBUTTON_USED) != 0;
}

/* ---- Finish's and the auto-moves' flights (extra; 2d: eased, overlapping) ------------------------------- */

/* The session is about to move the top card of src to foundation dst (it moves it when this returns):
 * the card takes off at once, the cards still in the air keep flying, and this returns when it has flown
 * CE_CASCADE_PERCENT of its time, so the next card of a cascade overlaps it. */
void view_animate_move(App *a, int src, int dst)
{
    const SolBoard *b = &a->s.board;
    SolBoard after;
    CeRect r[MAX_DIRTY], from, to;
    int n, fan, fx, fy, tx, ty, i, dur, ppf;
    const CeImage *sprite;
    CeFlight *f;
    double t0;
    int dropped;
    CeRect drop_r;
    if (!animatable(a) || src < 0 || src >= SOL_NPILES || dst < 0 || dst >= SOL_NPILES || b->p[src].n == 0) {
        if (a->fl.n > 0 && !animatable(a)) {
            ce_flights_drop(&a->fl);                  /* (minimized meanwhile: the rest just appears) */
            view_invalidate_all(a);
        }
        return;
    }
    t0 = ce_now_ms();
    flights_ready(a);
    ce_flights_release(&a->fl);                       /* the session has moved the previous cards */
    detect(a);                                        /* Enhanced animations: a card turned over meanwhile */
    /* a card still landing on src lands first; so does src's own card turning over (it turns, then flies:
     * the turn is detected first, else it would go on turning on the pile after its card has left) */
    if (ce_flights_settle(&a->fl, src) < 0)
        view_invalidate_all(a);
    flights_ready(a);
    /* v1.2 (cards home after a drop): the lifted stack still floats on the screen where it was dropped;
     * the first frame repaints that place too */
    dropped = a->drag_on && !a->drag_outline && !sol_dragging(&a->s);
    drop_r = dropped ? ce_drag_cover(&a->drag) : ce_rect(0, 0, 0, 0);
    n = b->p[src].n;
    fan = sol_waste_fan(&a->s);
    sprite = ce_cardset_card(sol_gfx_cards(a->gfx), sol_card_id(b->p[src].c[n - 1]));
    sol_layout_card_pos(&a->L, b, fan, src, n - 1, &fx, &fy);
    after = *b;
    after.p[dst].c[after.p[dst].n++] = after.p[src].c[--after.p[src].n];
    sol_layout_card_pos(&a->L, &after, fan, dst, after.p[dst].n - 1, &tx, &ty);
    from = ce_rect(fx, fy, a->L.cw, a->L.ch);
    to = ce_rect(tx, ty, a->L.cw, a->L.ch);
    ppf = (int)(60 * a->L.s + 0.5);
    dur = flight_ms(a, from, to, ppf > 0 ? ppf : 1);
    f = ce_flights_add(&a->fl, sprite, from, to, dur, CE_EASE_DECEL, src, dst,
                       fl_pack(sol_card_id(b->p[src].c[n - 1]), src, dst, FL_MOVE));
    if (!f)
        return;
    n = sync_render(a, r);                            /* the board without it (and what changed meanwhile) */
    for (i = 0; i < n; i++)
        ce_flights_dirty(&a->fl, r[i]);
    if (dropped && ce_rect_clip(&drop_r, a->bb.fb.w, a->bb.fb.h))
        ce_flights_dirty(&a->fl, drop_r);
    if (ce_flights_run(&a->fl, ce_flights_next(f)) < 0)
        view_invalidate_all(a);                       /* resized mid-flight: start over cleanly */
    ce_log("finish flight pile %d -> %d: %d ms, %d in the air, %.1f ms", src, dst, dur, a->fl.n, ce_now_ms() - t0);
}

/* ---- the win cascade -------------------------------------------------------------------------------- */

/* The second press of a double-click on the Finish button (2d): its first press clicked the button, so
 * Finish has begun meanwhile (it arrives while the cards fly home or the cascade runs). It is the
 * button's, as a push button takes a double-click as two clicks, never input that stops the cascade. */
static int button_second_press(App *a, const MSG *m)
{
    CeRect r = a->fbtn.r;
    int x = (short)LOWORD(m->lParam), y = (short)HIWORD(m->lParam);
    return m->message == WM_LBUTTONDBLCLK && m->hwnd == a->hwnd && a->btn_down &&
           (DWORD)(m->time - a->btn_down_t) <= GetDoubleClickTime() && x >= r.x && y >= r.y && x < r.x + r.w &&
           y < r.y + r.h;
}

/* XP's AbortPending (0x1004D43): these stop the cascade and stay in the queue; anything else is
 * dispatched. Returns 1 to stop. */
static int cascade_pump(App *a)
{
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_NOREMOVE)) {
        if (button_second_press(a, &m)) {
            a->btn_down = 0;
            PeekMessageW(&m, a->hwnd, WM_LBUTTONDBLCLK, WM_LBUTTONDBLCLK, PM_REMOVE);
            ce_log("win cascade: the Finish button's double-click (its second press) ignored");
            continue;
        }
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
    view_anim_idle(a);                                /* 2d: the last cards land first */
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
