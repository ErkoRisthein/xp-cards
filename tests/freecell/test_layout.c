/*
 * FreeCell HD — native tests for src/freecell/layout.c and render.c: XP equality at s = 1, scaling,
 * column compression, hit testing (layout.md §2 "Hit testing", rules.md §2.4), clipped rendering.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets_native.h"
#include "freecell/layout.h"
#include "freecell/render.h"

static int failures, checks;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            printf("FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond);         \
            printf(__VA_ARGS__);                                            \
            printf("\n");                                                   \
        }                                                                   \
    } while (0)

static void clear(FcBoard *b)
{
    int c, i;
    memset(b, 0, sizeof *b);
    for (c = 0; c < 9; c++)
        for (i = 0; i < FC_COLLEN; i++)
            b->board[c][i] = FC_EMPTY;
    for (c = 0; c < 4; c++)
        b->home_rank[c] = b->suit_home_slot[c] = -1;
}

/* column col gets n arbitrary distinct-ish cards */
static void fill_col(FcBoard *b, int col, int n)
{
    int i;
    for (i = 0; i < FC_COLLEN; i++)
        b->board[col][i] = i < n ? (col * 7 + i * 3) % 52 : FC_EMPTY;
}

static int rect_eq(CeRect r, int x, int y, int w, int h)
{
    return r.x == x && r.y == y && r.w == w && r.h == h;
}

static void test_xp_equality(void)
{
    static const int col_x[9] = { 0, 7, 85, 163, 241, 319, 397, 475, 553 };
    static const int top_x[8] = { 0, 71, 142, 213, 348, 419, 490, 561 };
    FcLayout l;
    int i;
    fc_layout_compute(&l, 632, 427);
    CHECK(l.s == 1.0, "s %f", l.s);
    CHECK(l.board_x == 0 && l.board_w == 632, "board %d %d", l.board_x, l.board_w);
    CHECK(l.cw == 71 && l.ch == 96, "card %dx%d", l.cw, l.ch);
    for (i = 0; i < 8; i++)
        CHECK(rect_eq(l.top[i], top_x[i], 0, 71, 96), "top %d: %d,%d %dx%d", i, l.top[i].x, l.top[i].y,
              l.top[i].w, l.top[i].h);
    for (i = 1; i <= 8; i++)
        CHECK(l.col_x[i] == col_x[i], "col %d x %d", i, l.col_x[i]);
    CHECK(l.col_y0 == 106 && l.step == 18, "y0 %d step %d", l.col_y0, l.step);
    CHECK(rect_eq(l.king, 300, 21, 32, 32), "king %d,%d %d", l.king.x, l.king.y, l.king.w);
    CHECK(rect_eq(l.king_frame, 297, 18, 38, 38), "frame %d,%d %d", l.king_frame.x, l.king_frame.y,
          l.king_frame.w);   /* lines at x 297 / 334, y 18 / 55 (layout.md §3) */
    CHECK(l.bevel == 1, "bevel %d", l.bevel);
    CHECK(rect_eq(l.big_king, 10, 106, 320, 320), "big king %d,%d %d", l.big_king.x, l.big_king.y,
          l.big_king.w);
    CHECK(l.anim_px_per_frame == 37, "anim %d", l.anim_px_per_frame);
    CHECK(l.bottom_limit == 423 && l.step_min == 10, "limit %d min %d", l.bottom_limit, l.step_min);
    {
        FcBoard b;
        CeRect r;
        clear(&b);
        fill_col(&b, 1, 7);
        r = fc_layout_card_rect(&l, &b, 1, 6);
        CHECK(rect_eq(r, 7, 214, 71, 96), "col 1 card 6 at %d,%d", r.x, r.y);   /* layout.md §2 */
        r = fc_layout_card_rect(&l, &b, 0, 5);
        CHECK(rect_eq(r, 419, 0, 71, 96), "home 1 at %d,%d", r.x, r.y);
    }
    /* any height >= 372 at width 632 is still s = 1 with the same geometry */
    fc_layout_compute(&l, 632, 1000);
    CHECK(l.s == 1.0 && l.cw == 71 && l.col_x[8] == 553 && l.board_x == 0, "632x1000");
    fc_layout_compute(&l, 632, 372);
    CHECK(l.s == 1.0 && l.cw == 71 && l.ch == 96, "632x372");
}

static void test_scaling(void)
{
    FcLayout l, prev;
    int w, h, first = 1;
    /* growing both dimensions with the board's aspect: everything grows monotonically */
    for (w = 316; w <= 4000; w += 7) {
        h = w * 427 / 632;
        fc_layout_compute(&l, w, h);
        if (!first) {
            CHECK(l.s >= prev.s && l.cw >= prev.cw && l.ch >= prev.ch && l.step >= prev.step &&
                  l.col_y0 >= prev.col_y0 && l.king.w >= prev.king.w && l.bevel >= prev.bevel &&
                  l.col_x[8] >= prev.col_x[8] && l.step_min >= prev.step_min,
                  "not monotonic at %dx%d", w, h);
        }
        first = 0;
        prev = l;
    }
    /* invariants over a grid of client sizes */
    for (w = 316; w <= 4000; w += 61)
        for (h = 159; h <= 2400; h += 37) {
            int k, bad = 0;
            double s;
            fc_layout_compute(&l, w, h);
            s = (double)w / 632 < (double)h / 372 ? (double)w / 632 : (double)h / 372;
            if (s < 0.5)
                s = 0.5;
            bad += l.s != s;
            bad += l.cw != (int)(71 * s + 0.5) || l.ch != (int)(96 * s + 0.5);
            bad += l.board_x < 0 || l.board_x + l.board_w > w;
            bad += l.board_w < w && (l.board_x != (w - l.board_w) / 2 || l.board_w != (int)(632 * s + 0.5));
            bad += l.top[0].x != l.board_x || l.top[7].x + l.cw != l.board_x + l.board_w;
            bad += l.top[3].x + l.cw > l.king_frame.x || l.king_frame.x + l.king_frame.w > l.top[4].x;
            bad += l.king.y + l.king.h > l.ch || l.king_frame.y < 0;
            bad += l.col_x[1] < l.board_x || l.col_x[8] + l.cw > l.board_x + l.board_w;
            for (k = 1; k < 8; k++)
                bad += l.col_x[k] + l.cw > l.col_x[k + 1];
            bad += l.step != 9 * l.ch / 46 || l.step_min > l.step || l.step_min < 1;
            bad += l.col_y0 <= l.ch;
            bad += l.big_king.y + l.big_king.h > h && l.big_king.h > l.cw;
            bad += l.bevel != (s < 1.5 ? 1 : (int)(s + 0.5));
            CHECK(bad == 0, "invariants at %dx%d (%d)", w, h, bad);
        }
    /* height-limited: centred */
    fc_layout_compute(&l, 1800, 420);
    CHECK(l.board_w == (int)(632 * 420 / 372.0 + 0.5) && l.board_x == (1800 - l.board_w) / 2,
          "wide: %d %d", l.board_x, l.board_w);
    /* 1080p maximised client: height-limited, s ~ 2.68, card 190x257, board centred */
    fc_layout_compute(&l, 1904, 996);
    CHECK(l.cw == 190, "cw %d", l.cw);
    CHECK(l.ch == 257, "ch %d", l.ch);
    CHECK(l.board_w == 1692 && l.board_x == 106 && l.bevel == 3, "1080p board %d %d", l.board_x, l.board_w);
    /* min client, client for scale */
    fc_layout_min_client(&w, &h);
    CHECK(w == 316 && h == 186, "min %dx%d", w, h);
    fc_layout_compute(&l, w, h);
    CHECK(l.s == 0.5 && l.cw == 36 && l.ch == 48, "min layout s %f %dx%d", l.s, l.cw, l.ch);
    fc_layout_client_for_scale(1.0, &w, &h);
    CHECK(w == 632 && h == 427, "client for 1: %dx%d", w, h);
    fc_layout_client_for_scale(2.25, &w, &h);
    fc_layout_compute(&l, w, h);
    CHECK(l.s > 2.249 && l.s < 2.251 && l.board_x == 0, "client for 2.25: %dx%d s %f", w, h, l.s);
    /* below the minimum: clamped, anchored left */
    fc_layout_compute(&l, 200, 100);
    CHECK(l.s == 0.5 && l.board_x == 0 && l.board_w == 316, "clamped");
}

static void test_compression(void)
{
    FcLayout l;
    FcBoard b;
    int h, n, col;
    clear(&b);
    fc_layout_compute(&l, 632, 427);
    /* 13 cards fit XP's default client uncompressed (layout.md §2) */
    fill_col(&b, 1, 13);
    CHECK(fc_layout_col_step(&l, &b, 1) == 18, "13 cards");
    /* 15 cards: 106 + 14*18 + 96 = 454 > 423 -> step floor(221/14) = 15, ends at 412 */
    fill_col(&b, 2, 15);
    CHECK(fc_layout_col_step(&l, &b, 2) == 15, "15 cards: %d", fc_layout_col_step(&l, &b, 2));
    /* 19 cards: floor(221/18) = 12 >= step_min 10 -> 12, still fits */
    fill_col(&b, 3, 19);
    CHECK(fc_layout_col_step(&l, &b, 3) == 12, "19 cards: %d", fc_layout_col_step(&l, &b, 3));
    CHECK(fc_layout_col_step(&l, &b, 4) == 18 && fc_layout_col_step(&l, &b, 0) == 18, "empty/other");
    /* bounds over many sizes and lengths */
    for (h = 160; h <= 2200; h += 13)
        for (n = 0; n <= 19; n++) {
            int w = 632 + h / 3, step, bottom;
            fc_layout_compute(&l, w, h);
            fill_col(&b, 5, n);
            step = fc_layout_col_step(&l, &b, 5);
            bottom = l.col_y0 + (n - 1) * step + l.ch;
            col = 0;
            col += step < l.step_min || step > l.step;
            col += step < l.step && step > l.step_min && bottom > l.bottom_limit;   /* compressed fits */
            col += step < l.step && n > 1 && l.col_y0 + (n - 1) * (step + 1) + l.ch <= l.bottom_limit; /* tight */
            col += n > 1 && l.col_y0 + (n - 1) * l.step + l.ch <= l.bottom_limit && step != l.step;
            CHECK(col == 0, "h %d n %d step %d (normal %d min %d) bottom %d limit %d", h, n, step, l.step,
                  l.step_min, bottom, l.bottom_limit);
        }
}

static void hit(const FcLayout *l, const FcBoard *b, int x, int y, int mode, int want_ret, int want_col,
                int want_pos, int line)
{
    int col = 99, pos = 99, r = fc_layout_hit(l, b, x, y, mode, &col, &pos);
    checks++;
    if (r != want_ret || (want_ret && (col != want_col || pos != want_pos))) {
        failures++;
        printf("FAIL %s:%d: hit(%d,%d,%s) = %d col %d pos %d, want %d col %d pos %d\n", __FILE__, line, x, y,
               mode ? "dest" : "src", r, col, pos, want_ret, want_col, want_pos);
    }
}
#define HIT(x, y, m, r, c, p) hit(&l, &b, x, y, m, r, c, p, __LINE__)

static void test_hits(void)
{
    FcLayout l;
    FcBoard b;
    int S = FC_HIT_SOURCE, D = FC_HIT_DEST;
    clear(&b);
    fill_col(&b, 1, 7);
    fill_col(&b, 2, 1);
    /* column 3 empty */
    fill_col(&b, 4, 15);   /* compressed to step 15 at 632x427 */
    fill_col(&b, 8, 6);
    b.board[0][1] = 5;
    fc_layout_compute(&l, 632, 427);

    /* top row: free cells flush left, home cells flush right, empty or not */
    HIT(0, 0, S, 1, 0, 0);
    HIT(70, 95, S, 1, 0, 0);
    HIT(71, 0, S, 1, 0, 1);
    HIT(283, 50, S, 1, 0, 3);
    HIT(284, 50, S, 0, 0, 0);     /* gap left of the king */
    HIT(310, 30, S, 0, 0, 0);     /* the king itself */
    HIT(310, 30, D, 0, 0, 0);
    HIT(347, 10, S, 0, 0, 0);
    HIT(348, 10, S, 1, 0, 4);
    HIT(631, 95, D, 1, 0, 7);
    HIT(500, 96, S, 0, 0, 0);     /* band between the rows: y 96..105 */
    HIT(500, 105, D, 0, 0, 0);

    /* column 1 (7 cards): buried strips of 18 px, exposed card over its full height */
    HIT(7, 106, S, 1, 1, 0);
    HIT(77, 123, S, 1, 1, 0);
    HIT(7, 124, S, 1, 1, 1);
    HIT(40, 213, S, 1, 1, 5);
    HIT(40, 214, S, 1, 1, 6);
    HIT(40, 309, S, 1, 1, 6);
    HIT(40, 310, S, 0, 0, 0);     /* below the last card: source miss */
    HIT(40, 310, D, 1, 1, 6);     /* ...but a destination hit (pos = last index) */
    HIT(40, 426, D, 1, 1, 6);
    HIT(6, 200, S, 0, 0, 0);      /* left margin */
    HIT(6, 200, D, 0, 0, 0);
    HIT(78, 150, S, 0, 0, 0);     /* gap between columns 1 and 2 */
    HIT(78, 150, D, 1, 1, 6);     /* XP: the gap belongs to the column on its left for destinations */
    HIT(84, 150, D, 1, 1, 6);
    {
        int c = 0, p = 0;
        CHECK(!fc_layout_hit(&l, &b, 78, 150, S, &c, &p) && c == 1 && p == -1, "gap miss: col %d pos %d", c, p);
        CHECK(!fc_layout_hit(&l, &b, 310, 30, S, &c, &p) && c == -1 && p == -1, "king miss: col %d pos %d", c, p);
    }
    HIT(85, 150, D, 1, 2, 0);

    /* column 2 (1 card) and empty column 3 */
    HIT(100, 106, S, 1, 2, 0);
    HIT(100, 201, S, 1, 2, 0);
    HIT(100, 202, S, 0, 0, 0);
    HIT(170, 110, S, 1, 3, -1);   /* empty column: reported with pos -1 */
    HIT(170, 400, D, 1, 3, -1);
    HIT(235, 400, D, 1, 3, -1);

    /* column 4: 15 cards compressed to step 15 */
    HIT(250, 106 + 15 * 3, S, 1, 4, 3);
    HIT(250, 106 + 15 * 3 - 1, S, 1, 4, 2);
    HIT(250, 106 + 15 * 14, S, 1, 4, 14);
    HIT(250, 106 + 15 * 14 + 95, S, 1, 4, 14);
    HIT(250, 106 + 15 * 14 + 96, S, 0, 0, 0);

    /* column 8 and the right edge: XP's column-9 band (x >= 631) is a miss */
    HIT(553, 106, S, 1, 8, 0);
    HIT(623, 200, S, 1, 8, 5);
    HIT(624, 200, S, 0, 0, 0);
    HIT(630, 200, D, 1, 8, 5);
    HIT(631, 200, D, 0, 0, 0);
    HIT(-1, 200, D, 0, 0, 0);
    HIT(300, 427, D, 0, 0, 0);

    /* scaled + centred board: hits follow the drawn rects */
    fc_layout_compute(&l, 1800, 420);
    {
        int c, p, bad = 0;
        for (c = 1; c <= 8; c++) {
            int n = 0, k;
            while (n < FC_COLLEN && b.board[c][n] != FC_EMPTY)
                n++;
            for (k = 0; k < n; k++) {
                CeRect r = fc_layout_card_rect(&l, &b, c, k);
                int step = fc_layout_col_step(&l, &b, c), hc, hp;
                int ylast = k == n - 1 ? r.y + r.h - 1 : r.y + step - 1;
                if (r.y < l.client_h)   /* cards running off the bottom are not hittable there */
                    bad += !fc_layout_hit(&l, &b, r.x, r.y, S, &hc, &hp) || hc != c || hp != k;
                if (ylast < l.client_h)
                    bad += !fc_layout_hit(&l, &b, r.x + r.w - 1, ylast, S, &hc, &hp) || hc != c || hp != k;
            }
        }
        for (p = 0; p < 8; p++) {
            int hc, hp;
            CeRect r = l.top[p];
            bad += !fc_layout_hit(&l, &b, r.x + r.w - 1, r.y + r.h - 1, S, &hc, &hp) || hc != 0 || hp != p;
            bad += !fc_layout_hit(&l, &b, r.x, r.y, S, &hc, &hp) || hc != 0 || hp != p;
        }
        CHECK(bad == 0, "%d scaled hit mismatches", bad);
        HIT(l.board_x - 1, l.col_y0 + 5, D, 0, 0, 0);
        HIT(l.king.x + 2, l.king.y + 2, S, 0, 0, 0);
    }
}

/* fc_render_board_rect must equal the full render inside the rect and leave the rest untouched. */
static void test_render_rect(FcCardSet *cs)
{
    static const int sizes[][2] = { { 632, 427 }, { 977, 541 }, { 500, 260 } };
    FcBoard b;
    FcView v;
    FcLayout l;
    size_t si;
    int variant;
    clear(&b);
    fill_col(&b, 1, 7);
    fill_col(&b, 2, 19);
    fill_col(&b, 5, 3);
    fill_col(&b, 8, 12);
    b.board[0][0] = 51;
    b.board[0][2] = 13;
    b.board[0][5] = 24;
    for (si = 0; si < sizeof sizes / sizeof sizes[0]; si++)
        for (variant = 0; variant < 6; variant++) {
            int W = sizes[si][0], H = sizes[si][1], t, bad = 0;
            CeImage *full = ce_image_new(W, H), *part = ce_image_new(W, H);
            fc_layout_compute(&l, W, H);
            fc_render_prepare(cs, &l, variant & 1);
            fc_view_init(&v);
            if (variant == 1) { v.sel_col = 2; v.sel_pos = 18; v.peek_col = 1; v.peek_pos = 2; v.king = FC_KINGVIEW_LEFT; }
            if (variant == 2) { v.sel_col = 0; v.sel_pos = 2; v.hide_col = 8; v.hide_pos = 4; v.big_king = 1; v.king = FC_KINGVIEW_BLANK; }
            if (variant == 3) { v.no_game = 1; v.hide_col = 0; v.hide_pos = 5; }
            if (variant == 4) { v.hint_col = 2; v.hint_pos = 15; v.sel_col = 2; v.sel_pos = 18; }
            if (variant == 5) { v.hint_col = 0; v.hint_pos = 3; }
            fc_render_board(full, &l, &b, &v, cs);
            for (t = 0; t < 12; t++) {
                CeRect r;
                int x, y;
                uint32_t junk = 0x12345678u + (uint32_t)t;
                r.x = (t * 97 + variant * 31) % W - 20;
                r.y = (t * 53 + variant * 17) % H - 10;
                r.w = 1 + (t * 71) % (W / 2);
                r.h = 1 + (t * 43) % (H / 2);
                if (t == 0) { r.x = 0; r.y = 0; r.w = W; r.h = H; }
                ce_fill_rect(part, 0, 0, W, H, junk);
                fc_render_board_rect(part, &l, &b, &v, cs, r);
                for (y = 0; y < H; y++)
                    for (x = 0; x < W; x++) {
                        int in = x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
                        uint32_t p = part->px[y * W + x];
                        bad += in ? p != full->px[y * W + x] : p != junk;
                    }
            }
            CHECK(bad == 0, "render_rect %dx%d variant %d: %d pixels differ", W, H, variant, bad);
            /* spot checks of the full render */
            CHECK(full->px[(H - 1) * W + W - 1] == FC_TABLE_GREEN, "table green");
            if (si == 0) {
                /* empty free cell 1: XP bevel pixels (layout.md §3) */
                CHECK(full->px[0 * W + 71 + 0] == FC_BEVEL_DARK, "bevel tl");
                CHECK(full->px[0 * W + 71 + 70] == FC_TABLE_GREEN, "bevel corner tr");
                CHECK(full->px[95 * W + 71] == FC_TABLE_GREEN, "bevel corner bl");
                CHECK(full->px[95 * W + 71 + 1] == FC_BEVEL_LIGHT && full->px[50 * W + 71 + 70] == FC_BEVEL_LIGHT,
                      "bevel br");
                CHECK(full->px[50 * W + 71] == FC_BEVEL_DARK && full->px[50 * W + 72] == FC_TABLE_GREEN,
                      "bevel width 1");
                /* king frame: raised */
                CHECK(full->px[18 * W + 297] == FC_BEVEL_LIGHT && full->px[30 * W + 297] == FC_BEVEL_LIGHT &&
                      full->px[55 * W + 334] == FC_BEVEL_DARK && full->px[30 * W + 334] == FC_BEVEL_DARK &&
                      full->px[18 * W + 334] == FC_TABLE_GREEN && full->px[55 * W + 297] == FC_TABLE_GREEN,
                      "king frame");
                if (variant == 2) {
                    /* blank king box; card frames are black, the selected free-cell card inverted */
                    CHECK(full->px[37 * W + 316] == FC_TABLE_GREEN, "blank king");
                    CHECK(full->px[50 * W + 142] == CE_RGB(255, 255, 255), "inverted frame is white %08x",
                          full->px[50 * W + 142]);
                }
                if (variant == 0) {
                    CHECK(full->px[50 * W + 0] == CE_RGB(0, 0, 0), "card frame black %08x", full->px[50 * W]);
                    CHECK(full->px[0] == FC_TABLE_GREEN, "card corner transparent %08x", full->px[0]);
                    CHECK(full->px[37 * W + 316] != FC_TABLE_GREEN, "king drawn");
                }
            }
            ce_image_free(full);
            ce_image_free(part);
        }
}

/* The hint flash (extra): a card is drawn inverted like a selection (both together cancel out), a run
 * from its first card on, an empty cell or column as an inverted card-shaped area. */
static void test_render_hint(FcCardSet *cs)
{
    const int W = 632, H = 427;
    CeImage *a = ce_image_new(W, H), *b = ce_image_new(W, H);
    FcBoard bd;
    FcView v, w;
    FcLayout l;
    int last;
    clear(&bd);
    fill_col(&bd, 1, 7);
    fill_col(&bd, 5, 3);
    bd.board[0][0] = 51;
    last = fc_last_index(&bd, 1);
    fc_layout_compute(&l, W, H);
    fc_render_prepare(cs, &l, 1);
#define SAME(msg) CHECK(memcmp(a->px, b->px, sizeof(uint32_t) * (size_t)W * H) == 0, msg)
    fc_view_init(&v); v.hint_col = 1; v.hint_pos = last;
    fc_view_init(&w); w.sel_col = 1; w.sel_pos = last;
    fc_render_board(a, &l, &bd, &v, cs);
    fc_render_board(b, &l, &bd, &w, cs);
    SAME("hint on the exposed card = selection");
    fc_view_init(&v); v.hint_col = 0; v.hint_pos = 0;
    fc_view_init(&w); w.sel_col = 0; w.sel_pos = 0;
    fc_render_board(a, &l, &bd, &v, cs);
    fc_render_board(b, &l, &bd, &w, cs);
    SAME("hint on a free-cell card = selection");
    v.sel_col = 0; v.sel_pos = 0;
    fc_view_init(&w);
    fc_render_board(a, &l, &bd, &v, cs);
    fc_render_board(b, &l, &bd, &w, cs);
    SAME("hint and selection cancel out");
    /* a run: every card from hint_pos on is inverted (the exposed one fully) */
    fc_view_init(&v); v.hint_col = 1; v.hint_pos = 3;
    fc_view_init(&w);
    fc_render_board(a, &l, &bd, &v, cs);
    fc_render_board(b, &l, &bd, &w, cs);
    {
        CeRect r2 = fc_layout_card_rect(&l, &bd, 1, 2), r3 = fc_layout_card_rect(&l, &bd, 1, 3);
        int x = r3.x + r3.w / 2, y3 = r3.y + 4, y2 = r2.y + 4;
        uint32_t p = a->px[y3 * W + x], q = b->px[y3 * W + x];
        CHECK((p & 0xffffff) == (~q & 0xffffff), "run card 3 inverted %08x / %08x", p, q);
        CHECK(a->px[y2 * W + x] == b->px[y2 * W + x], "card above the run untouched");
    }
    /* an empty free cell: the card shape inverted, the corner outside it untouched */
    fc_view_init(&v); v.hint_col = 0; v.hint_pos = 1;
    fc_render_board(a, &l, &bd, &v, cs);
    CHECK(a->px[48 * W + 106] == CE_RGB(255, 128, 255), "empty cell inverted %08x", a->px[48 * W + 106]);
    CHECK(a->px[50 * W + 71] == CE_RGB(255, 255, 255), "its bevel inverted %08x", a->px[50 * W + 71]);
    CHECK(a->px[0 * W + 71] == b->px[0 * W + 71], "the corner is not");
    /* an empty column: its first card slot */
    fc_view_init(&v); v.hint_col = 3; v.hint_pos = -1;
    fc_render_board(a, &l, &bd, &v, cs);
    CHECK(a->px[(l.col_y0 + 40) * W + l.col_x[3] + 35] == CE_RGB(255, 128, 255), "empty column inverted");
    CHECK(a->px[(l.col_y0 + l.ch + 4) * W + l.col_x[3] + 35] == FC_TABLE_GREEN, "below the slot");
    /* nothing before a deal */
    v.no_game = 1;
    v.hint_col = 0; v.hint_pos = 1;
    fc_render_board(a, &l, &bd, &v, cs);
    CHECK(a->px[48 * W + 106] == FC_TABLE_GREEN, "no hint without a game");
#undef SAME
    ce_image_free(a);
    ce_image_free(b);
}

static void test_cardset(FcCardSet *cs)
{
    const CeImage *a, *b2;
    fc_cardset_set_size(cs, 71, 96, 32, 320, 1);
    a = fc_cardset_card(cs, 0);
    CHECK(a && a->w == 71 && a->h == 96, "card size");
    CHECK(fc_cardset_card(cs, 0) == a, "cached");
    fc_cardset_set_size(cs, 71, 96, 32, 320, 1);
    CHECK(fc_cardset_card(cs, 0) == a, "same size keeps cache");
    CHECK(fc_cardset_card(cs, -1) == NULL && fc_cardset_card(cs, 52) == NULL, "bad card");
    a = fc_cardset_king(cs, FC_KING_RIGHT, 0);
    b2 = fc_cardset_king(cs, FC_KING_SMILE, 1);
    CHECK(a && a->w == 32 && a->h == 32 && b2 && b2->w == 320, "kings (placeholder if missing)");
    CHECK(fc_cardset_king(cs, 3, 0) == NULL, "bad king");
    fc_cardset_set_size(cs, 213, 288, 96, 960, 0);
    a = fc_cardset_card(cs, 51);
    CHECK(a && a->w == 213 && a->h == 288 && a->px[0] == 0 && a->px[144 * 213] == 0xff000000u, "213x288");
    fc_cardset_set_size(cs, 430, 581, 193, 1935, 1);   /* sprites > budget: masters dropped/reloaded */
    a = fc_cardset_card(cs, 7);
    CHECK(a && a->w == 430 && a->h == 581, "4K card");
    fc_cardset_set_size(cs, 71, 96, 32, 320, 1);
    a = fc_cardset_card(cs, 7);
    CHECK(a && a->w == 71, "card after master reload");
}

/* ---- v1 reference pipeline (verbatim copies of v1's clean_master and ce_image_card_finish): the
 * card set's faster code (edge runs, cached shape) must give the same sprites bit for bit. -------- */

static uint32_t ref_mul255(uint32_t x, uint32_t a)
{
    uint32_t t = x * a + 128;
    return (t + (t >> 8)) >> 8;
}

static uint32_t ref_over(uint32_t s, uint32_t d)
{
    uint32_t inv = 255 - (s >> 24), rb, ag;
    rb = (d & 0x00ff00ffu) * inv + 0x00800080u;
    rb = ((rb + ((rb >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
    ag = ((d >> 8) & 0x00ff00ffu) * inv + 0x00800080u;
    ag = (ag + ((ag >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
    return s + (rb | ag);
}

static void ref_clean_master(CeImage *m)
{
    const double band = 3.5 * m->h / 560.0;
    const double r = 0.0372 * m->h;
    const double ri = r - band;
    int x, y;
    for (y = 0; y < m->h; y++) {
        uint32_t *p = m->px + (size_t)y * m->stride;
        double cy = y + 0.5, dy = cy < r ? r - cy : (cy > m->h - r ? cy - (m->h - r) : 0);
        for (x = 0; x < m->w; x++) {
            double cx = x + 0.5, dx = cx < r ? r - cx : (cx > m->w - r ? cx - (m->w - r) : 0);
            int edge = cx < band || cy < band || cx > m->w - band || cy > m->h - band ||
                       (dx > 0 && dy > 0 && dx * dx + dy * dy > ri * ri);
            if (edge)
                p[x] = CE_RGB(255, 255, 255);
            else if (p[x] < 0xff000000u) {
                uint32_t a = p[x] >> 24, k = 255 - a;
                p[x] += (k << 24) | (k << 16) | (k << 8) | k;
            }
        }
    }
}

static void ref_card_finish(CeImage *img, double r, double t, uint32_t frame, uint32_t under)
{
    int px, py;
    double w, h, ri, band;
    uint32_t fa, fr, fg, fb;
    w = img->w;
    h = img->h;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    ri = r - t > 0 ? r - t : 0;
    band = r + 1;
    fa = frame >> 24;
    fr = (frame >> 16) & 255;
    fg = (frame >> 8) & 255;
    fb = frame & 255;
    for (py = 0; py < img->h; py++) {
        uint32_t *d = img->px + (size_t)py * img->stride;
        int ey = py < t + 1 || py + 1 > h - t - 1, cy = py < band || py + 1 > h - band;
        for (px = 0; px < img->w; px++) {
            double co, ci;
            uint32_t p = d[px], ring, in, c[4], k;
            int edge = ey || px < t + 1 || px + 1 > w - t - 1 || (cy && (px < band || px + 1 > w - band));
            if (!edge) {
                if (p < 0xff000000u)
                    d[px] = ref_over(p, under);
                continue;
            }
            co = ce_round_rect_coverage(0, 0, w, h, r, px, py);
            ci = ce_round_rect_coverage(t, t, w - 2 * t, h - 2 * t, ri, px, py);
            if (p < 0xff000000u)
                p = ref_over(p, under);
            ring = (uint32_t)((co - ci) * 255 + 0.5);
            in = (uint32_t)(ci * 255 + 0.5);
            if (ring + in > 255)
                ring = 255 - in;
            for (k = 0; k < 4; k++) {
                uint32_t sh = 8 * k, fcol = k == 3 ? fa : (k == 2 ? fr : (k == 1 ? fg : fb));
                uint32_t fpm = k == 3 ? fa : ref_mul255(fcol, fa);
                c[k] = ref_mul255(fpm, ring) + ref_mul255((p >> sh) & 255, in);
                if (c[k] > 255)
                    c[k] = 255;
            }
            if (c[0] > c[3]) c[0] = c[3];
            if (c[1] > c[3]) c[1] = c[3];
            if (c[2] > c[3]) c[2] = c[3];
            d[px] = (c[3] << 24) | (c[2] << 16) | (c[1] << 8) | c[0];
        }
    }
}

static CeImage *ref_sprite(FcNativeAssets *na, Card c, int cw, int ch, int q)
{
    size_t len;
    const void *d = fc_native_asset_loader(CE_ASSET_CARD0 + c, &len, na);
    CeImage *m = d ? ce_image_decode_png(d, len) : NULL, *s;
    if (!m)
        return NULL;
    ref_clean_master(m);
    s = ce_image_resample_card(m, cw, ch, q);   /* q 1: the card resampler (test_image.c checks it) */
    ce_image_free(m);
    if (s)
        ref_card_finish(s, 0.0372 * ch, ch <= 300 ? 1.0 : ch / 300.0, CE_RGB(0, 0, 0), CE_RGB(255, 255, 255));
    return s;
}

static int same_image(const CeImage *a, const CeImage *b)
{
    int y;
    if (!a || !b || a->w != b->w || a->h != b->h)
        return 0;
    for (y = 0; y < a->h; y++)
        if (memcmp(a->px + (size_t)y * a->stride, b->px + (size_t)y * b->stride, sizeof(uint32_t) * (size_t)a->w))
            return 0;
    return 1;
}

typedef struct CountingAssets { FcNativeAssets na; int cards; } CountingAssets;

static const void *counting_loader(int id, size_t *len, void *ctx)
{
    CountingAssets *c = (CountingAssets *)ctx;
    if (id >= CE_ASSET_CARD0 && id < CE_ASSET_CARD0 + 52)
        c->cards++;
    return fc_native_asset_loader(id, len, &c->na);
}

/* Sprites equal the reference pipeline (v1 clean and finish around ce_image_resample_card) at every
 * size class; quality and size rules (a fast request at the built size keeps the sprites); the
 * big-size path never decodes a PNG for fast sprites; the bevel ring cache. */
static void test_cardset_rules(const char *res)
{
    static const int sizes[][3] = { { 71, 96, 1 }, { 191, 258, 1 }, { 213, 288, 0 }, { 300, 406, 1 }, { 430, 581, 1 } };
    static const Card cards[] = { 0, 13, 26, 39, 44, 47, 50, 51 };
    CountingAssets ca;
    FcCardSet *cs;
    size_t si, ci;
    const CeImage *a, *b;
    int bad = 0, n = 0, before;
    memset(&ca, 0, sizeof ca);
    fc_native_assets_init(&ca.na, res);
    cs = fc_cardset_new(counting_loader, &ca);
    if (!cs) {
        printf("SKIP cardset rules: no assets\n");
        return;
    }
    CHECK(ca.cards == 52, "52 decodes at start-up (%d)", ca.cards);
    for (si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
        fc_cardset_set_size(cs, sizes[si][0], sizes[si][1], 32, 320, sizes[si][2]);
        for (ci = 0; ci < sizeof cards / sizeof cards[0]; ci++) {
            CeImage *r = ref_sprite(&ca.na, cards[ci], sizes[si][0], sizes[si][1], sizes[si][2]);
            bad += !same_image(fc_cardset_card(cs, cards[ci]), r);
            n++;
            ce_image_free(r);
        }
    }
    CHECK(bad == 0, "%d of %d sprites differ from the reference pipeline", bad, n);

    /* quality: fast at the same size keeps the best sprites, best again keeps them too */
    fc_cardset_set_size(cs, 191, 258, 32, 320, 1);
    a = fc_cardset_card(cs, 5);
    b = fc_cardset_king(cs, FC_KING_RIGHT, 0);
    fc_cardset_set_size(cs, 191, 258, 32, 320, 0);
    CHECK(fc_cardset_card(cs, 5) == a && fc_cardset_king(cs, FC_KING_RIGHT, 0) == b, "fast request keeps HQ sprites");
    {
        CeImage *r = ref_sprite(&ca.na, 6, 191, 258, 1);   /* built after the fast request: still HQ */
        CHECK(same_image(fc_cardset_card(cs, 6), r), "missing sprite built at the kept quality");
        ce_image_free(r);
    }
    fc_cardset_set_size(cs, 191, 258, 32, 320, 1);
    CHECK(fc_cardset_card(cs, 5) == a, "back to best: nothing rebuilt");
    fc_cardset_set_size(cs, 190, 257, 32, 320, 0);         /* a new size: fast sprites */
    a = fc_cardset_card(cs, 5);
    {
        CeImage *r = ref_sprite(&ca.na, 5, 190, 257, 0);
        CHECK(same_image(a, r), "fast sprite at a new size");
        ce_image_free(r);
    }
    fc_cardset_set_size(cs, 190, 257, 32, 320, 1);         /* best at that size: rebuilt */
    {
        CeImage *r = ref_sprite(&ca.na, 5, 190, 257, 1);
        CHECK(same_image(fc_cardset_card(cs, 5), r), "HQ rebuild after fast");
        ce_image_free(r);
    }

    /* big sprites: masters dropped; a live resize (fast) decodes nothing, also back at a small size */
    fc_cardset_set_size(cs, 430, 581, 193, 1935, 1);
    for (ci = 0; ci < 52; ci++)
        fc_cardset_card(cs, (Card)ci);
    before = ca.cards;
    fc_cardset_set_size(cs, 428, 578, 193, 1935, 0);
    for (ci = 0; ci < 52; ci++)
        fc_cardset_card(cs, (Card)ci);
    fc_cardset_set_size(cs, 200, 270, 90, 900, 0);
    for (ci = 0; ci < 52; ci++)
        fc_cardset_card(cs, (Card)ci);
    a = fc_cardset_card(cs, 51);
    CHECK(ca.cards == before, "fast sprites from the half-size copies: %d decodes", ca.cards - before);
    CHECK(a && a->w == 200 && a->h == 270, "fast sprite size");
    fc_cardset_set_size(cs, 200, 270, 90, 900, 1);         /* best: the masters come back (decoded once) */
    {
        CeImage *r = ref_sprite(&ca.na, 51, 200, 270, 1);
        CHECK(same_image(fc_cardset_card(cs, 51), r), "HQ after the big size");
        ce_image_free(r);
    }
    before = ca.cards;
    fc_cardset_set_size(cs, 201, 271, 90, 900, 1);
    fc_cardset_card(cs, 51);
    CHECK(ca.cards == before, "master kept again: %d decodes", ca.cards - before);

    /* bevel rings: cached per key, equal to a fresh one */
    a = fc_cardset_bevel(cs, 191, 258, 2.6875, 9.6, CE_RGB(0, 0, 0), CE_RGB(0, 255, 0));
    b = fc_cardset_bevel(cs, 102, 102, 2.6875, 5.7, CE_RGB(0, 255, 0), CE_RGB(0, 0, 0));
    CHECK(a && b && a != b, "two rings");
    CHECK(fc_cardset_bevel(cs, 191, 258, 2.6875, 9.6, CE_RGB(0, 0, 0), CE_RGB(0, 255, 0)) == a &&
          fc_cardset_bevel(cs, 102, 102, 2.6875, 5.7, CE_RGB(0, 255, 0), CE_RGB(0, 0, 0)) == b, "rings cached");
    {
        CeImage *r = ce_bevel_ring_new(191, 258, 2.6875, 9.6, CE_RGB(0, 0, 0), CE_RGB(0, 255, 0));
        CHECK(same_image(a, r), "cached ring == fresh ring");
        ce_image_free(r);
    }
    CHECK(fc_cardset_bevel(cs, 191, 258, 2.6875, 9.5, CE_RGB(0, 0, 0), CE_RGB(0, 255, 0)) != a, "new key, new ring");
    CHECK(fc_cardset_bevel(NULL, 10, 10, 2, 2, 0, 0) == NULL, "no card set: no cache");
    fc_cardset_free(cs);
    fc_native_assets_free(&ca.na);
}

/* Board renders with the cached bevel rings equal renders without a card set (rings built on the
 * fly), at scaled-up sizes where the HD bevel is used. */
static void test_bevel_cache_render(FcCardSet *cs)
{
    static const int sizes[][2] = { { 948, 640 }, { 1920, 1000 }, { 1264, 854 } };
    size_t si;
    for (si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
        int W = sizes[si][0], H = sizes[si][1];
        CeImage *a = ce_image_new(W, H), *b = ce_image_new(W, H);
        FcLayout l;
        FcView v;
        fc_layout_compute(&l, W, H);
        fc_render_prepare(cs, &l, 1);
        fc_view_init(&v);
        v.no_game = 1;
        v.king = FC_KINGVIEW_BLANK;                   /* (no king sprite without a card set) */
        fc_render_board(a, &l, NULL, &v, cs);
        fc_render_board(a, &l, NULL, &v, cs);         /* the second time from the cache */
        fc_render_board(b, &l, NULL, &v, NULL);
        CHECK(l.bevel > 1 && memcmp(a->px, b->px, sizeof(uint32_t) * (size_t)W * H) == 0,
              "%dx%d: cached bevels differ", W, H);
        ce_image_free(a);
        ce_image_free(b);
    }
}

static const void *null_loader(int id, size_t *len, void *ctx)
{
    (void)id;
    (void)ctx;
    *len = 0;
    return NULL;
}

int main(void)
{
    FcNativeAssets assets;
    FcCardSet *cs;
    const char *res = getenv("FC_RES_DIR");
    test_xp_equality();
    test_scaling();
    test_compression();
    test_hits();
    CHECK(fc_cardset_new(null_loader, NULL) == NULL, "missing cards -> NULL");
    fc_native_assets_init(&assets, res ? res : "res");
    cs = fc_cardset_new(fc_native_asset_loader, &assets);
    if (cs) {
        test_cardset(cs);
        test_render_rect(cs);
        test_render_hint(cs);
        test_bevel_cache_render(cs);
        test_cardset_rules(res ? res : "res");
        fc_cardset_free(cs);
    } else {
        printf("SKIP render tests: card assets not found in %s\n", assets.dir);
    }
    fc_native_assets_free(&assets);
    printf("test_layout: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
