/*
 * FreeCell HD — native snapshot renderer: draws the board at several client sizes / states to PNG
 * for visual review, and prints timings.
 *
 *   snapshots <res dir> <out dir>        (make snapshots)
 *   env FC_XP_SHOT=<XP game #1 capture, 632x427 PNG> adds a side-by-side comparison.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "assets_native.h"
#include "core/game.h"
#include "gfx/image.h"
#include "gfx/layout.h"
#include "gfx/render.h"
#include "gfx/cardset.h"

static const char *out_dir;
static FcCardSet *cs;

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void save(const FcImage *img, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.png", out_dir, name);
    if (!fc_image_write_png(img, path))
        fprintf(stderr, "cannot write %s\n", path);
    else
        printf("  %s (%dx%d)\n", path, img->w, img->h);
}

static FcImage *render(int w, int h, const FcBoard *b, const FcView *v, int quality)
{
    FcLayout l;
    FcImage *fb = fc_image_new(w, h);
    fc_layout_compute(&l, w, h);
    fc_render_prepare(cs, &l, quality);
    fc_render_board(fb, &l, b, v, cs);
    return fb;
}

static void shot(const char *name, int w, int h, const FcBoard *b, const FcView *v)
{
    FcImage *fb = render(w, h, b, v, 1);
    save(fb, name);
    fc_image_free(fb);
}

/* Move card c from wherever it is to the end of column col (compacting its old column). */
static void move_to_col(FcBoard *b, Card c, int col)
{
    int k, i, n;
    for (k = 1; k <= 8; k++)
        for (i = 0; i < FC_COLLEN; i++)
            if (b->board[k][i] == c) {
                memmove(&b->board[k][i], &b->board[k][i + 1], sizeof(Card) * (size_t)(FC_COLLEN - 1 - i));
                b->board[k][FC_COLLEN - 1] = FC_EMPTY;
                goto found;
            }
    for (i = 0; i < 8; i++)
        if (b->board[0][i] == c)
            b->board[0][i] = FC_EMPTY;
found:
    for (n = 0; n < FC_COLLEN && b->board[col][n] != FC_EMPTY; n++)
        ;
    if (n < FC_COLLEN)
        b->board[col][n] = c;
}

static void move_to_top(FcBoard *b, Card c, int slot)
{
    move_to_col(b, c, 8);
    b->board[8][fc_last_index(b, 8)] = FC_EMPTY;
    b->board[0][slot] = c;
}

static Card card_of(int rank, int suit) { return rank * 4 + suit; }

int main(int argc, char **argv)
{
    FcNativeAssets assets;
    FcBoard b, g1, lng, homes, won;
    FcView v;
    FcLayout l;
    FcImage *fb;
    double t0, t1;
    int i, w, h, rep;
    const char *xp_shot = getenv("FC_XP_SHOT");

    if (argc < 3) {
        fprintf(stderr, "usage: %s <res dir> <out dir>\n", argv[0]);
        return 2;
    }
    out_dir = argv[2];
    fc_native_assets_init(&assets, argv[1]);
    t0 = now_ms();
    cs = fc_cardset_new(fc_native_asset_loader, &assets);
    t1 = now_ms();
    if (!cs) {
        fprintf(stderr, "cannot load card assets from %s\n", argv[1]);
        return 1;
    }
    printf("decode 52 masters + kings: %.1f ms\n", t1 - t0);
    for (i = 0; i < 3; i++) {
        size_t n;
        if (!fc_native_asset_loader(FC_ASSET_KING_RIGHT + i, &n, &assets))
            printf("note: king asset %d missing, placeholder used\n", i);
    }

    /* timings: HQ rescale of all 52 at small, medium and 1080p sizes (the card resampler's dark bias
     * and sharpen are on below h255), HQ and fast at 213x288; full render at 1904x996 */
    for (rep = 0; rep < 5; rep++) {
        static const int sz[5][3] = { { 71, 96, 1 }, { 95, 128, 1 }, { 190, 257, 1 }, { 213, 288, 1 }, { 213, 288, 0 } };
        fc_cardset_set_size(cs, 1, 1, 1, 1, 1);   /* invalidate */
        fc_cardset_set_size(cs, sz[rep][0], sz[rep][1], 96, 960, sz[rep][2]);
        t0 = now_ms();
        for (i = 0; i < 52; i++)
            fc_cardset_card(cs, i);
        t1 = now_ms();
        printf("rescale 52 cards to %dx%d, quality %d: %.1f ms\n", sz[rep][0], sz[rep][1], sz[rep][2], t1 - t0);
    }
    fc_deal(&g1, 1);
    fc_view_init(&v);
    fc_layout_compute(&l, 1904, 996);
    fc_render_prepare(cs, &l, 1);
    fb = fc_image_new(1904, 996);
    fc_render_board(fb, &l, &g1, &v, cs);   /* warm the sprite cache */
    t0 = now_ms();
    for (rep = 0; rep < 20; rep++)
        fc_render_board(fb, &l, &g1, &v, cs);
    t1 = now_ms();
    printf("full board render 1904x996 (sprites cached): %.2f ms\n", (t1 - t0) / 20);
    fc_image_free(fb);

    printf("snapshots:\n");
    /* startup: no game dealt */
    fc_board_clear(&b);
    fc_view_init(&v);
    v.no_game = 1;
    shot("01_startup_632x427", 632, 427, &b, &v);

    /* game #1 at XP's size, and next to the XP capture */
    fc_view_init(&v);
    fb = render(632, 427, &g1, &v, 1);
    save(fb, "02_game1_632x427");
    if (xp_shot) {
        size_t n;
        void *data = fc_native_read_file(xp_shot, &n);
        FcImage *xp = data ? fc_image_decode_png(data, n) : NULL;
        if (xp) {
            FcImage *sbs = fc_image_new(632 * 2 + 8, 427);
            fc_fill_rect(sbs, 0, 0, sbs->w, sbs->h, FC_RGB(255, 255, 255));
            fc_copy_rect(sbs, 0, 0, fb, 0, 0, 632, 427);
            fc_copy_rect(sbs, 632 + 8, 0, xp, 0, 0, 632, 427);
            save(sbs, "03_game1_vs_xp");
            fc_image_free(sbs);
            fc_image_free(xp);
        }
        free(data);
    }
    fc_image_free(fb);

    shot("04_game1_1904x996", 1904, 996, &g1, &v);
    shot("05_game1_1264x669", 1264, 669, &g1, &v);
    shot("06_game1_800x540", 800, 540, &g1, &v);
    shot("07_game1_3824x2050", 3824, 2050, &g1, &v);
    fc_layout_min_client(&w, &h);
    shot("08_game1_min", w, h, &g1, &v);
    {
        FcImage *f2 = render(800, 540, &g1, &v, 0);
        save(f2, "09_game1_800x540_fast");
        fc_image_free(f2);
    }

    /* selection (bottom card of column 4) and a peek (column 1, 3rd card) */
    v.sel_col = 4;
    v.sel_pos = fc_last_index(&g1, 4);
    shot("10_selected_1264x669", 1264, 669, &g1, &v);
    fc_view_init(&v);
    v.peek_col = 1;
    v.peek_pos = 2;
    v.king = FC_KINGVIEW_LEFT;
    shot("11_peek_1264x669", 1264, 669, &g1, &v);

    /* a 19-card column (7 dealt + Q..A run) and a 13-card one: compression at 1080p */
    lng = g1;
    for (i = 11; i >= 0; i--)
        move_to_col(&lng, card_of(i, (i & 1) ? 1 : 3), 1);    /* QD JS TD 9S ... AS? alternate */
    for (i = 0; i < 6; i++)
        move_to_col(&lng, card_of(12 - i, (i & 1) ? 0 : 2), 5);
    fc_view_init(&v);
    v.sel_col = 1;
    v.sel_pos = fc_last_index(&lng, 1);
    shot("12_longcol_1904x996", 1904, 996, &lng, &v);
    shot("13_longcol_632x427", 632, 427, &lng, &v);
    shot("14_longcol_1264x500", 1264, 500, &lng, &v);

    /* free cells and home cells in use; a free-cell card selected; a card in flight hidden */
    homes = g1;
    move_to_top(&homes, card_of(0, 0), 4);
    move_to_top(&homes, card_of(1, 0), 4);
    move_to_top(&homes, card_of(0, 1), 5);
    move_to_top(&homes, card_of(0, 2), 6);
    move_to_top(&homes, card_of(1, 2), 6);
    move_to_top(&homes, card_of(2, 2), 6);
    move_to_top(&homes, card_of(12, 3), 0);
    move_to_top(&homes, card_of(9, 1), 2);
    move_to_top(&homes, card_of(5, 0), 3);
    fc_view_init(&v);
    v.sel_col = 0;
    v.sel_pos = 2;
    v.king = FC_KINGVIEW_LEFT;
    shot("15_cells_1264x669", 1264, 669, &homes, &v);
    shot("16_cells_632x427", 632, 427, &homes, &v);
    fc_view_init(&v);
    v.hide_col = 3;
    v.hide_pos = 4;
    shot("17_hidden_632x427", 632, 427, &homes, &v);

    /* win: four kings home, big smiling king, small box blank */
    fc_board_clear(&won);
    for (i = 0; i < 4; i++)
        won.board[0][4 + i] = card_of(12, i);
    fc_view_init(&v);
    v.king = FC_KINGVIEW_BLANK;
    v.big_king = 1;
    shot("18_win_632x427", 632, 427, &won, &v);
    shot("19_win_1904x996", 1904, 996, &won, &v);

    /* wide and short: height-limited, board centred */
    fc_view_init(&v);
    shot("20_wide_1800x420", 1800, 420, &g1, &v);
    shot("21_tall_700x1000", 700, 1000, &g1, &v);

    /* card sheet at several heights (frame quality at small sizes) */
    {
        static const int hs[] = { 48, 72, 96, 144, 200, 288 };
        int y = 4, k, W = 0;
        for (k = 0; k < 6; k++)
            W = W > 14 * ((hs[k] * 71 + 48) / 96 + 4) ? W : 14 * ((hs[k] * 71 + 48) / 96 + 4);
        fb = fc_image_new(W + 8, 4 + 6 * 4 + 48 + 72 + 96 + 144 + 200 + 288 + 8);
        fc_fill_rect(fb, 0, 0, fb->w, fb->h, FC_TABLE_GREEN);
        for (k = 0; k < 6; k++) {
            int ch = hs[k], cw = (ch * 71 + 48) / 96, x = 4;
            static const int show[] = { 0, 1, 2, 3, 26, 31, 39, 41, 44, 46, 49, 50, 51, 22 };
            fc_cardset_set_size(cs, cw, ch, 32, 320, 1);
            for (i = 0; i < 14; i++, x += cw + 4)
                fc_render_card(fb, cs, show[i], x, y, i == 13);
            y += ch + 4;
        }
        save(fb, "22_card_sheet");
        fc_image_free(fb);
    }

    fc_cardset_free(cs);
    fc_native_assets_free(&assets);
    return 0;
}
