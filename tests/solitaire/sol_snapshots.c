/*
 * Solitaire HD — native snapshot renderer: XP deals and situations at XP's size and at 1080p, next to
 * the captures of XP's sol.exe when they are available, plus the backs, the empty-pile pictures and
 * the win cascade, as PNGs for review; prints timings.
 *
 *   sol_snapshots <res dir> <out dir>                     (make snapshots: build/snapshots/solitaire)
 *   env SOL_XP_SHOTS=<sol-research/shots dir> adds the side-by-side comparisons with XP's captures
 *   (v01_launch.png, stock/s08.png, stock/vegas_x.png, drag/d01_mid.png, natwin/win_replay_last.png).
 *
 * The status bar is a Win32 child window; the snapshots draw its white strip and top border only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sol_assets_native.h"
#include "solitaire/game.h"
#include "solitaire/layout.h"
#include "solitaire/render.h"
#include "solitaire/winanim.h"

static const char *out_dir, *xp_dir;
static SolGfx *gfx;
static SolNativeAssets assets;

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void save(const CeImage *img, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.png", out_dir, name);
    if (!ce_image_write_png(img, path))
        fprintf(stderr, "cannot write %s\n", path);
    else
        printf("  %s (%dx%d)\n", path, img->w, img->h);
}

static CeImage *load_xp(const char *rel)
{
    char path[1024];
    size_t n;
    void *data;
    CeImage *img;
    if (!xp_dir)
        return NULL;
    snprintf(path, sizeof path, "%s/%s", xp_dir, rel);
    data = fc_native_read_file(path, &n);
    img = data ? ce_image_decode_png(data, n) : NULL;
    free(data);
    if (!img)
        fprintf(stderr, "note: no XP capture %s\n", path);
    return img;
}

/* XP's status bar window over the bottom of the client: white, a 1-px top border (its text is GDI's) */
static void status_strip(CeImage *fb, const SolLayout *l)
{
    if (l->status_h <= 0)
        return;
    ce_fill_rect(fb, 0, l->status.y, fb->w, l->status.h, CE_RGB(255, 255, 255));
    ce_fill_rect(fb, 0, l->status.y, fb->w, 1, CE_RGB(0, 0, 0));
}

static CeImage *render(int w, int h, int status_h, const SolBoard *b, const SolView *v, SolLayout *lout)
{
    SolLayout l;
    CeImage *fb = ce_image_new(w, h);
    sol_layout_compute(&l, w, h, status_h);
    sol_render_prepare(gfx, &l, 1);
    sol_render_board(fb, &l, b, v, gfx);
    status_strip(fb, &l);
    if (lout)
        *lout = l;
    return fb;
}

static void shot(const char *name, int w, int h, const SolBoard *b, const SolView *v)
{
    CeImage *fb = render(w, h, SOL_XP_STATUS_H, b, v, NULL);
    save(fb, name);
    ce_image_free(fb);
}

/* nearest-neighbour zoom of a region of src into dst at (dx, dy) */
static void zoom(CeImage *dst, int dx, int dy, const CeImage *src, CeRect r, int k)
{
    int x, y;
    for (y = 0; y < r.h * k; y++)
        for (x = 0; x < r.w * k; x++) {
            int sx = r.x + x / k, sy = r.y + y / k, tx = dx + x, ty = dy + y;
            if (sx < 0 || sy < 0 || sx >= src->w || sy >= src->h || tx < 0 || ty < 0 || tx >= dst->w || ty >= dst->h)
                continue;
            dst->px[(size_t)ty * dst->stride + tx] = src->px[(size_t)sy * src->stride + sx] | 0xFF000000u;
        }
}

/* ours | XP's, and below them the same crop of both zoomed 3x */
static void side_by_side(const char *name, const CeImage *ours, const char *xp_rel, CeRect crop)
{
    CeImage *xp = load_xp(xp_rel), *sbs;
    int W, H, gap = 8, zh = crop.h * 3;
    if (!xp)
        return;
    W = ours->w + gap + xp->w;
    if (W < 2 * crop.w * 3 + gap)
        W = 2 * crop.w * 3 + gap;
    H = (ours->h > xp->h ? ours->h : xp->h) + gap + zh;
    sbs = ce_image_new(W, H);
    ce_fill_rect(sbs, 0, 0, W, H, CE_RGB(255, 255, 255));
    ce_copy_rect(sbs, 0, 0, ours, 0, 0, ours->w, ours->h);
    ce_copy_rect(sbs, ours->w + gap, 0, xp, 0, 0, xp->w, xp->h);
    zoom(sbs, 0, H - zh, ours, crop, 3);
    zoom(sbs, crop.w * 3 + gap, H - zh, xp, crop, 3);
    save(sbs, name);
    ce_image_free(sbs);
    ce_image_free(xp);
}

/* ---- boards -------------------------------------------------------------------------------------- */

/* Draw all of the stock in draw-three packets (each packet reversed onto the waste), as XP: the waste
 * ends with the stock's bottom card on top, fanned with the two above it. */
static void draw_all(SolBoard *b, int draw, int *fan)
{
    SolPile *st = &b->p[SOL_STOCK], *w = &b->p[SOL_WASTE];
    *fan = 0;
    while (st->n > 0) {
        int k = st->n < draw ? st->n : draw, i;
        for (i = 0; i < k; i++)
            w->c[w->n++] = (SolCard)(st->c[--st->n] | SOL_UP);
        *fan = k;
    }
}

static void won_board(SolBoard *b)
{
    int f, r;
    sol_board_clear(b);
    for (f = 0; f < 4; f++) {
        b->p[SOL_FOUND0 + f].n = 13;
        for (r = 0; r < 13; r++)
            b->p[SOL_FOUND0 + f].c[r] = (SolCard)((r * 4 + f) | SOL_UP);
    }
}

static SolCard cd(int rank, int suit, int up) { return (SolCard)((rank * 4 + suit) | (up ? SOL_UP : 0)); }

/* a mid game with a 19-card column (6 face down, K..A alternating) and a fanned waste */
static void long_board(SolBoard *b, int *fan)
{
    static const char *const piles[SOL_NPILES] = {
        "#3C #9D #JS #2H #5S #8C #QD #6H #4D #TS",          /* stock */
        "7D 4C 9H QC 8D",                                    /* waste, the top 3 fanned */
        "AC 2C", "", "AD", "",                               /* foundations */
        "KS QD JC", "#5C 7S", "#2S #7C TD 9C", "#9S #6D #TH 3D", "#JD #KC #5H #8S 6C 5D",
        "#3S #KD #4S #TC #JH 2D",
        "#QH #3H #6S #7H #8H #4H KH QS JH TS 9H 8S 7H 6S 5H 4S 3H 2S AH"
    };
    int p;
    sol_board_clear(b);
    for (p = 0; p < SOL_NPILES; p++) {
        char buf[256], *t;
        snprintf(buf, sizeof buf, "%s", piles[p]);
        for (t = strtok(buf, " "); t; t = strtok(NULL, " ")) {
            const char *ranks = "A23456789TJQK", *suits = "CDHS";
            int up = *t != '#';
            if (!up)
                t++;
            b->p[p].c[b->p[p].n++] = cd((int)(strchr(ranks, t[0]) - ranks), (int)(strchr(suits, t[1]) - suits), up);
        }
    }
    *fan = 3;
}

/* ---- the win cascade ----------------------------------------------------------------------------- */

static void cascade_shots(const char *prefix, int w, int h, unsigned seed, const long *at, int nat,
                          const char *xp_last)
{
    SolLayout l;
    SolBoard b, deal;
    SolView v;
    SolWinAnim a;
    CeImage *fb;
    uint32_t rng;
    int c, x, y, k = 0;
    long f = 0;
    double t0;
    char name[128];
    won_board(&b);
    sol_deal_board(&deal, seed, &rng);
    sol_view_init(&v);
    fb = render(w, h, SOL_XP_STATUS_H, &b, &v, &l);   /* XP: the stock's O and the full foundations */
    snprintf(name, sizeof name, "%s_f%05ld", prefix, 0L);
    save(fb, name);
    sol_winanim_start(&a, &l, &b, rng, 0);
    t0 = now_ms();
    while (sol_winanim_frame(&a, &c, &x, &y)) {
        sol_render_card(fb, gfx, c, 0, x, y);           /* never erased: the trail */
        f++;
        if (k < nat && f == at[k]) {
            CeImage *shot2 = ce_image_new(w, h);
            ce_copy_rect(shot2, 0, 0, fb, 0, 0, w, h);
            status_strip(shot2, &l);                    /* the status bar stays on top (WS_CLIPCHILDREN) */
            snprintf(name, sizeof name, "%s_f%05ld", prefix, f);
            save(shot2, name);
            ce_image_free(shot2);
            k++;
        }
    }
    printf("  cascade %dx%d: %ld frames (%.1f s at XP's 5 ms), drawn in %.0f ms\n", w, h, f, f * 0.005,
           now_ms() - t0);
    status_strip(fb, &l);
    snprintf(name, sizeof name, "%s_end", prefix);
    save(fb, name);
    if (xp_last)
        side_by_side("vs_xp_cascade_end", fb, xp_last, ce_rect(0, 0, 200, 120));
    ce_image_free(fb);
}

/* ---- sheets -------------------------------------------------------------------------------------- */

static void backs_sheet(void)
{
    static const int hs[] = { 96, 173, 256 };
    CeImage *fb;
    int k, i, x, y = 6, W = 0, H = 6;
    for (k = 0; k < 3; k++) {
        int cw = (hs[k] * 71 + 48) / 96;
        if (12 * (cw + 6) + 6 > W)
            W = 12 * (cw + 6) + 6;
        H += hs[k] + 6;
    }
    H += 62 + 6;
    fb = ce_image_new(W, H);
    ce_fill_rect(fb, 0, 0, W, H, SOL_TABLE_GREEN);
    for (k = 0; k < 3; k++) {
        int ch = hs[k], cw = (ch * 71 + 48) / 96;
        double t0 = now_ms();
        ce_cardset_set_size(sol_gfx_cards(gfx), cw, ch, 1);
        for (i = 0, x = 6; i < SOL_NBACKS; i++, x += cw + 6)
            sol_render_card(fb, gfx, -1, i, x, y);
        printf("  12 backs at %dx%d: %.1f ms\n", cw, ch, now_ms() - t0);
        y += ch + 6;
    }
    /* the Select Card Back dialog's pictures (45 x 62 at 96 DPI) */
    for (i = 0, x = 6; i < SOL_NBACKS; i++, x += 45 + 6) {
        CeImage *bi = sol_back_image(sol_native_asset_loader, &assets, i, 45, 62);
        if (bi) {
            ce_blit(fb, bi, x, y);
            ce_image_free(bi);
        }
    }
    save(fb, "30_backs_96_173_256_dialog");
    ce_image_free(fb);
}

/* The empty-pile pictures at s = 1, 2.67 and the 1x set zoomed 6x, next to XP's bitmaps 53, 68, 67 */
static void markers_sheet(void)
{
    SolLayout l1, lh;
    SolBoard b;
    SolView v;
    CeImage *one, *hd, *fb;
    static const char *xp_bmp[3] = { "../res/cards/cards_bitmap_53.png", "../res/cards/cards_bitmap_68.png",
                                     "../res/cards/cards_bitmap_67.png" };
    int i, W, H;
    sol_board_clear(&b);
    sol_view_init(&v);
    one = render(585, 384, SOL_XP_STATUS_H, &b, &v, &l1);
    v.stock_x = 1;
    {
        CeImage *x1 = render(585, 384, SOL_XP_STATUS_H, &b, &v, NULL);
        ce_copy_rect(one, 300, 200, x1, l1.pile[SOL_STOCK].x, l1.pile[SOL_STOCK].y, 71, 96);   /* the X */
        ce_image_free(x1);
    }
    v.stock_x = 0;
    hd = render(1904, 996, SOL_XP_STATUS_H, &b, &v, &lh);
    W = 3 * (71 * 6 + 8) + 8;
    H = 96 * 6 + 8 + 96 * 6 + 8 + lh.ch + 16;
    if (W < 3 * (lh.cw + 8) + 8)
        W = 3 * (lh.cw + 8) + 8;
    fb = ce_image_new(W, H);
    ce_fill_rect(fb, 0, 0, W, H, CE_RGB(255, 255, 255));
    zoom(fb, 8, 8, one, ce_rect(l1.pile[SOL_FOUND0].x, l1.pile[SOL_FOUND0].y, 71, 96), 6);
    zoom(fb, 8 + (71 * 6 + 8), 8, one, ce_rect(l1.pile[SOL_STOCK].x, l1.pile[SOL_STOCK].y, 71, 96), 6);
    zoom(fb, 8 + 2 * (71 * 6 + 8), 8, one, ce_rect(300, 200, 71, 96), 6);
    for (i = 0; i < 3; i++) {
        CeImage *xp = load_xp(xp_bmp[i]);
        if (xp) {
            zoom(fb, 8 + i * (71 * 6 + 8), 8 + 96 * 6 + 8, xp, ce_rect(0, 0, 71, 96), 6);
            ce_image_free(xp);
        }
    }
    ce_copy_rect(fb, 8, 2 * (96 * 6 + 8) + 8, hd, lh.pile[SOL_FOUND0].x, lh.pile[SOL_FOUND0].y, lh.cw, lh.ch);
    ce_copy_rect(fb, 8 + lh.cw + 8, 2 * (96 * 6 + 8) + 8, hd, lh.pile[SOL_STOCK].x, lh.pile[SOL_STOCK].y, lh.cw,
                 lh.ch);
    v.stock_x = 1;
    {
        CeImage *x2 = render(1904, 996, SOL_XP_STATUS_H, &b, &v, NULL);
        ce_copy_rect(fb, 8 + 2 * (lh.cw + 8), 2 * (96 * 6 + 8) + 8, x2, lh.pile[SOL_STOCK].x, lh.pile[SOL_STOCK].y,
                     lh.cw, lh.ch);
        ce_image_free(x2);
    }
    save(fb, "31_markers_1x_x6_vs_xp_and_1080p");
    ce_image_free(fb);
    ce_image_free(one);
    ce_image_free(hd);
}

int main(int argc, char **argv)
{
    SolBoard b, base;
    SolView v;
    SolLayout l;
    CeImage *fb;
    uint32_t rng;
    double t0, t1;
    int i, rep, fan, w, h;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <res dir> <out dir>\n", argv[0]);
        return 2;
    }
    out_dir = argv[2];
    xp_dir = getenv("SOL_XP_SHOTS");
    sol_native_assets_init(&assets, argv[1]);
    t0 = now_ms();
    gfx = sol_gfx_new(sol_native_asset_loader, &assets);
    t1 = now_ms();
    if (!gfx) {
        fprintf(stderr, "cannot load card assets from %s\n", argv[1]);
        return 1;
    }
    printf("decode 52 face masters: %.1f ms\n", t1 - t0);

    /* timings: a full 1080p render (sprites cached), the empty-pile pictures' build */
    sol_deal_board(&base, 27694, &rng);
    sol_view_init(&v);
    sol_layout_compute(&l, 1904, 996, SOL_XP_STATUS_H);
    t0 = now_ms();
    sol_render_prepare(gfx, &l, 1);
    fb = ce_image_new(1904, 996);
    sol_render_board(fb, &l, &base, &v, gfx);
    t1 = now_ms();
    printf("first 1080p render (52 faces + back at %dx%d, HQ): %.1f ms\n", l.cw, l.ch, t1 - t0);
    b = base;
    b.p[SOL_STOCK].n = 0;
    t0 = now_ms();
    sol_render_board(fb, &l, &b, &v, gfx);
    printf("O and ghost pictures at %dx%d: %.1f ms\n", l.cw, l.ch, now_ms() - t0);
    t0 = now_ms();
    for (rep = 0; rep < 20; rep++)
        sol_render_board(fb, &l, &base, &v, gfx);
    printf("full board render 1904x996 (cached): %.2f ms\n", (now_ms() - t0) / 20);
    ce_image_free(fb);

    printf("snapshots:\n");
    /* 1. XP's launch deal (seed 27694, back 54 = Sky) at XP's size, next to XP's capture, and at 1080p */
    sol_view_init(&v);
    fb = render(585, 384, SOL_XP_STATUS_H, &base, &v, NULL);
    save(fb, "01_deal27694_585x384");
    side_by_side("02_deal27694_vs_xp", fb, "v01_launch.png", ce_rect(0, 0, 200, 130));
    ce_image_free(fb);
    shot("03_deal27694_1904x996", 1904, 996, &base, &v);
    shot("04_deal27694_1264x669", 1264, 669, &base, &v);

    /* 2. Draw three, the stock drawn out (seed 28555): the empty stock's O and the waste fan */
    sol_deal_board(&b, 28555, &rng);
    draw_all(&b, 3, &fan);
    sol_view_init(&v);
    v.waste_fan = fan;
    fb = render(585, 384, SOL_XP_STATUS_H, &b, &v, NULL);
    save(fb, "05_draw3_fan_585x384");
    side_by_side("06_draw3_fan_vs_xp", fb, "stock/s08.png", ce_rect(0, 0, 200, 110));
    ce_image_free(fb);
    shot("07_draw3_fan_1904x996", 1904, 996, &b, &v);
    /* the fan after the top card is played: the two left stay */
    b.p[SOL_WASTE].n--;
    v.waste_fan = fan - 1;
    shot("08_fan_after_play_1904x996", 1904, 996, &b, &v);

    /* 3. Vegas, no passes left (seed 29263, back 64 = Sunset): the red X */
    sol_deal_board(&b, 29263, &rng);
    draw_all(&b, 3, &fan);
    sol_view_init(&v);
    v.waste_fan = fan;
    v.stock_x = 1;
    v.back = 10;
    fb = render(585, 384, SOL_XP_STATUS_H, &b, &v, NULL);
    save(fb, "09_vegas_x_585x384");
    side_by_side("10_vegas_x_vs_xp", fb, "stock/vegas_x.png", ce_rect(0, 0, 200, 110));
    ce_image_free(fb);
    shot("11_vegas_x_1904x996", 1904, 996, &b, &v);

    /* 4. A drag (seed 28378): the 6 of diamonds of column 7 at (363, 265), XP's d01_mid capture */
    sol_deal_board(&b, 28378, &rng);
    sol_view_init(&v);
    v.drag_pile = SOL_TAB0 + 6;
    v.drag_card = 6;
    v.drag_x = 363;
    v.drag_y = 265;
    fb = render(585, 384, SOL_XP_STATUS_H, &b, &v, NULL);
    save(fb, "12_drag_585x384");
    side_by_side("13_drag_vs_xp", fb, "drag/d01_mid.png", ce_rect(330, 100, 200, 130));
    ce_image_free(fb);
    sol_layout_compute(&l, 1904, 996, SOL_XP_STATUS_H);
    {
        int x0, y0;
        sol_layout_card_pos(&l, &b, 0, SOL_TAB0 + 6, 6, &x0, &y0);
        v.drag_x = x0 + (int)((363 - 503) * l.s);
        v.drag_y = y0 + (int)((265 - 125) * l.s);
    }
    shot("14_drag_1904x996", 1904, 996, &b, &v);
    /* a run dragged by its middle card: 2 cards of a column, full drag */
    {
        SolBoard r = b;
        SolPile *c = &r.p[SOL_TAB0 + 4];
        c->c[c->n++] = cd(5, 2, 1);   /* 6H on 7S */
        c->c[c->n++] = cd(4, 0, 1);   /* 5C */
        v.drag_pile = SOL_TAB0 + 4;
        v.drag_card = c->n - 2;
        sol_layout_card_pos(&l, &r, 0, SOL_TAB0 + 4, c->n - 2, &v.drag_x, &v.drag_y);
        v.drag_x += (int)(-120 * l.s);
        v.drag_y += (int)(95 * l.s);
        shot("15_drag_run_1904x996", 1904, 996, &r, &v);
    }

    /* 5. Outline dragging: the 6D's outline over the 7S, which is inverted (the target) */
    sol_view_init(&v);
    v.drag_mode = SOL_DRAG_OUTLINE;
    v.drag_pile = SOL_TAB0 + 6;
    v.drag_card = 6;
    v.drag_x = 300;
    v.drag_y = 200;
    v.target = SOL_TAB0 + 4;
    shot("16_outline_585x384", 585, 384, &b, &v);
    sol_layout_compute(&l, 1904, 996, SOL_XP_STATUS_H);
    v.drag_x = (int)(300 * l.s);
    v.drag_y = (int)(200 * l.s);
    shot("17_outline_1904x996", 1904, 996, &b, &v);
    /* outline of a 13-card run */
    {
        int f2;
        long_board(&b, &f2);
        sol_view_init(&v);
        v.waste_fan = f2;
        v.drag_mode = SOL_DRAG_OUTLINE;
        v.drag_pile = SOL_TAB0 + 6;
        v.drag_card = 6;
        v.drag_x = 250;
        v.drag_y = 140;
        v.target = SOL_TAB0;
        shot("18_outline_run_585x384", 585, 384, &b, &v);

        /* 6. Long columns: compressed at 1080p and at XP's size; keyboard selection */
        sol_view_init(&v);
        v.waste_fan = f2;
        v.sel_pile = SOL_TAB0 + 6;
        v.sel_card = 15;
        shot("19_longcol_1904x996", 1904, 996, &b, &v);
        shot("20_longcol_585x384", 585, 384, &b, &v);
        v.sel_pile = SOL_TAB0 + 1;
        v.sel_card = 1;
        shot("21_kbd_select_1264x669", 1264, 669, &b, &v);
    }

    /* 7. Window shapes: wide and short (XP's spread), tall, the minimum, live-resize quality */
    sol_view_init(&v);
    shot("22_wide_1904x500", 1904, 500, &base, &v);
    shot("23_tall_700x1000", 700, 1000, &base, &v);
    sol_layout_min_client(SOL_XP_STATUS_H, &w, &h);
    shot("24_min", w, h, &base, &v);
    {
        SolLayout lq;
        CeImage *q = ce_image_new(1264, 669);
        sol_layout_compute(&lq, 1264, 669, SOL_XP_STATUS_H);
        sol_render_prepare(gfx, &lq, 0);
        ce_cardset_set_size(sol_gfx_cards(gfx), 1, 1, 0);   /* force a fast rebuild */
        sol_render_prepare(gfx, &lq, 0);
        sol_render_board(q, &lq, &base, &v, gfx);
        status_strip(q, &lq);
        save(q, "25_fast_quality_1264x669");
        ce_image_free(q);
    }
    /* not dealt (startup, after a win): the table only */
    v.dealt = 0;
    shot("26_not_dealt_585x384", 585, 384, &base, &v);
    /* every back on a board (the stock and face-down cards) */
    for (i = 0; i < SOL_NBACKS; i += 4) {
        char name[64];
        sol_view_init(&v);
        v.back = i + 1;
        snprintf(name, sizeof name, "27_back_%s_1264x669", sol_back_names[i + 1]);
        shot(name, 1264, 669, &base, &v);
    }

    /* 8. The win cascade: natural win of deal 29785 (XP's logged cascade), at XP's size (with XP's
     * frame-exact replay of its end for comparison) and at 1080p */
    {
        static const long at1[] = { 300, 1500, 4000, 7000 };
        static const long at2[] = { 300, 1500, 4000, 8000, 12000 };
        cascade_shots("40_cascade_585x384", 585, 384, 29785, at1, 4, "natwin/win_replay_last.png");
        cascade_shots("41_cascade_1904x996", 1904, 996, 29785, at2, 5, NULL);
    }

    /* 9. Sheets: the backs at three sizes + the dialog pictures; the empty-pile pictures */
    backs_sheet();
    markers_sheet();

    sol_gfx_free(gfx);
    sol_native_assets_free(&assets);
    return 0;
}
