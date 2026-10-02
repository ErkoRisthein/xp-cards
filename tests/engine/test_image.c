/*
 * Card engine — native tests for src/engine/image.c and the card set: resampling (incl. the card resampler against the
 * crisplab model's golden values, tests/card_golden.h), compositing, inversion, AA shapes, PNG round
 * trip.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/image.h"
#include "assets_native.h"
#include "card_golden.h"

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

#define A(p) ((p) >> 24)
#define R(p) (((p) >> 16) & 255)
#define G(p) (((p) >> 8) & 255)
#define B(p) ((p) & 255)

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static uint32_t random_premul(void)
{
    uint32_t a = rnd() % 256, r = rnd() % (a + 1), g = rnd() % (a + 1), b = rnd() % (a + 1);
    return CE_ARGB(a, r, g, b);
}

static CeImage *solid(int w, int h, uint32_t p)
{
    CeImage *img = ce_image_new(w, h);
    ce_fill_rect(img, 0, 0, w, h, p);
    return img;
}

static void test_constant(void)
{
    static const int sizes[][4] = {
        { 400, 560, 213, 288 }, { 400, 560, 71, 96 }, { 400, 560, 36, 48 }, { 400, 560, 430, 581 },
        { 7, 5, 3, 11 }, { 3, 3, 1, 1 }, { 1, 1, 9, 4 }, { 100, 100, 99, 101 }, { 400, 560, 400, 560 },
    };
    static const uint32_t colours[] = { 0xffffffffu, 0xff00ff00u, 0x80402010u, 0x00000000u, 0xff7f3a09u };
    size_t i, k;
    int q;
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        for (k = 0; k < sizeof colours / sizeof colours[0]; k++)
            for (q = 0; q <= 1; q++) {
                CeImage *src = solid(sizes[i][0], sizes[i][1], colours[k]);
                CeImage *dst = ce_image_resample(src, sizes[i][2], sizes[i][3], q);
                int x, y, bad = 0;
                CHECK(dst && dst->w == sizes[i][2] && dst->h == sizes[i][3], "size");
                for (y = 0; dst && y < dst->h; y++)
                    for (x = 0; x < dst->w; x++)
                        bad += dst->px[y * dst->stride + x] != colours[k];
                CHECK(bad == 0, "constant %08x %dx%d->%dx%d q%d: %d pixels changed", colours[k],
                      sizes[i][0], sizes[i][1], sizes[i][2], sizes[i][3], q, bad);
                ce_image_free(src);
                ce_image_free(dst);
            }
}

static void test_box_values(void)
{
    /* 1-D: [0, 255, 0] -> 2 px: each covers 1.5 source px: (0 + 0.5*255) / 1.5 = 85 */
    CeImage *src = ce_image_new(3, 1), *dst;
    src->px[1] = 0xffffffffu;
    src->px[0] = src->px[2] = 0xff000000u;
    dst = ce_image_resample(src, 2, 1, 1);
    CHECK(R(dst->px[0]) == 85 && R(dst->px[1]) == 85 && A(dst->px[0]) == 255, "got %08x %08x",
          dst->px[0], dst->px[1]);
    ce_image_free(dst);
    ce_image_free(src);

    /* 2x2 checkerboard of black/white to 1x1: exact mid grey (127.5 rounds to 128) */
    src = ce_image_new(2, 2);
    src->px[0] = src->px[3] = 0xffffffffu;
    src->px[1] = src->px[2] = 0xff000000u;
    dst = ce_image_resample(src, 1, 1, 1);
    CHECK(R(dst->px[0]) == 128 && G(dst->px[0]) == 128 && B(dst->px[0]) == 128, "got %08x", dst->px[0]);
    ce_image_free(dst);
    ce_image_free(src);

    /* independent x / y factors: 4x2 -> 2x2 averages only horizontally */
    src = ce_image_new(4, 2);
    src->px[0] = 0xff000000u; src->px[1] = 0xff646464u; src->px[2] = 0xffc8c8c8u; src->px[3] = 0xffffffffu;
    src->px[4] = 0xffffffffu; src->px[5] = 0xffffffffu; src->px[6] = 0xff000000u; src->px[7] = 0xff000000u;
    dst = ce_image_resample(src, 2, 2, 1);
    CHECK(R(dst->px[0]) == 50 && R(dst->px[1]) == 228 && R(dst->px[2]) == 255 && R(dst->px[3]) == 0,
          "got %08x %08x %08x %08x", dst->px[0], dst->px[1], dst->px[2], dst->px[3]);
    ce_image_free(dst);
    ce_image_free(src);

    /* bilinear upscale 2 -> 4: centres at -0.25, 0.25, 0.75, 1.25 -> 0, 63.75, 191.25, 255 */
    src = ce_image_new(2, 1);
    src->px[0] = 0xff000000u;
    src->px[1] = 0xffffffffu;
    dst = ce_image_resample(src, 4, 1, 1);
    CHECK(R(dst->px[0]) == 0 && R(dst->px[1]) == 64 && R(dst->px[2]) == 191 && R(dst->px[3]) == 255,
          "got %u %u %u %u", R(dst->px[0]), R(dst->px[1]), R(dst->px[2]), R(dst->px[3]));
    ce_image_free(dst);
    ce_image_free(src);

    /* mean is preserved by the box filter (integer factor) */
    {
        int x, y;
        double s0 = 0, s1 = 0;
        src = ce_image_new(60, 40);
        for (y = 0; y < 40; y++)
            for (x = 0; x < 60; x++)
                src->px[y * 60 + x] = CE_RGB(0, 0, rnd() % 256);
        dst = ce_image_resample(src, 20, 10, 1);
        for (x = 0; x < 60 * 40; x++) s0 += B(src->px[x]);
        for (x = 0; x < 20 * 10; x++) s1 += B(dst->px[x]);
        CHECK(fabs(s0 / 2400 - s1 / 200) < 0.6, "means %f %f", s0 / 2400, s1 / 200);
        ce_image_free(dst);
        ce_image_free(src);
    }
}

/* An opaque coloured shape on a transparent background must not get dark fringes: every resampled
 * pixel's straight colour equals the shape colour (premultiplied channels proportional to alpha). */
static void test_alpha_edges(void)
{
    static const uint32_t colours[] = { 0xffffffffu, 0xffff0000u, 0xff00ff00u };
    size_t k;
    int q, x, y;
    for (k = 0; k < 3; k++)
        for (q = 0; q <= 1; q++) {
            CeImage *src = ce_image_new(97, 61), *dst;
            int bad = 0, partial = 0;
            for (y = 0; y < 61; y++)
                for (x = 0; x < 97; x++)
                    if ((x - 48) * (x - 48) + (y - 30) * (y - 30) < 27 * 27)
                        src->px[y * 97 + x] = colours[k];
            dst = ce_image_resample(src, 23, 17, q);
            for (y = 0; y < dst->h; y++)
                for (x = 0; x < dst->w; x++) {
                    uint32_t p = dst->px[y * dst->stride + x], a = A(p);
                    uint32_t want_r = R(colours[k]) ? a : 0, want_g = G(colours[k]) ? a : 0;
                    if (a > 0 && a < 255)
                        partial++;
                    if ((int)R(p) < (int)want_r - 1 || R(p) > want_r || (int)G(p) < (int)want_g - 1 ||
                        G(p) > want_g)
                        bad++;
                }
            CHECK(bad == 0 && partial > 0, "colour %08x q%d: %d fringe pixels (%d partial)", colours[k], q,
                  bad, partial);
            ce_image_free(src);
            ce_image_free(dst);
        }
}

static void test_premul_invariant(void)
{
    int i, q;
    for (q = 0; q <= 1; q++) {
        CeImage *src = ce_image_new(53, 47), *dst;
        int bad = 0;
        for (i = 0; i < 53 * 47; i++)
            src->px[i] = random_premul();
        dst = ce_image_resample(src, 17, 29, q);
        for (i = 0; i < dst->w * dst->h; i++) {
            uint32_t p = dst->px[i];
            bad += R(p) > A(p) || G(p) > A(p) || B(p) > A(p);
        }
        CHECK(bad == 0, "q%d: %d pixels with colour > alpha", q, bad);
        ce_image_free(dst);
        dst = ce_image_resample(src, 120, 70, q);
        bad = 0;
        for (i = 0; i < dst->w * dst->h; i++) {
            uint32_t p = dst->px[i];
            bad += R(p) > A(p) || G(p) > A(p) || B(p) > A(p);
        }
        CHECK(bad == 0, "q%d upscale: %d pixels with colour > alpha", q, bad);
        ce_image_free(dst);
        ce_image_free(src);
    }
}

static CeImage *opaque_noise(int w, int h)
{
    CeImage *img = ce_image_new(w, h);
    int i;
    for (i = 0; i < w * h; i++)
        img->px[i] = CE_RGB(rnd() & 255, rnd() & 255, rnd() & 255);
    return img;
}

static int same_px(const CeImage *a, const CeImage *b)
{
    return a && b && a->w == b->w && a->h == b->h &&
           memcmp(a->px, b->px, sizeof(uint32_t) * (size_t)a->w * (size_t)a->h) == 0;
}

/* Floating-point reference of the card resampler, straight from the spec (docs/DESIGN.md
 * "Crispness decisions"): exact-area box on (v/255)^g, clamped 3-tap sharpen along x then y, back with
 * ^(1/g). Downscales only (w < sw, h < sh). */
static double *ref_box_weights(int sn, int dn)
{
    double *wt = (double *)calloc((size_t)dn * sn, sizeof(double));
    int i, j;
    for (i = 0; i < dn; i++)
        for (j = 0; j < sn; j++) {
            double a0 = (double)i * sn, a1 = a0 + sn, b0 = (double)j * dn, b1 = b0 + dn;
            double lo = a0 > b0 ? a0 : b0, hi = a1 < b1 ? a1 : b1;
            wt[(size_t)i * sn + j] = hi > lo ? (hi - lo) / sn : 0;
        }
    return wt;
}

static void ref_sharpen(double *v, int n, int stride, double a)
{
    double *o = (double *)malloc(sizeof(double) * (size_t)n);
    int i;
    for (i = 0; i < n; i++) {
        double l = v[(i > 0 ? i - 1 : 0) * stride], c = v[i * stride], r = v[(i + 1 < n ? i + 1 : i) * stride];
        double x = (1 + a / 2) * c - a / 4 * (l + r), lo = fmin(fmin(l, r), c), hi = fmax(fmax(l, r), c);
        o[i] = x < lo ? lo : (x > hi ? hi : x);
    }
    for (i = 0; i < n; i++)
        v[i * stride] = o[i];
    free(o);
}

static CeImage *ref_card_resample(const CeImage *src, int w, int h)
{
    int sw = src->w, sh = src->h, x, y, i, j, c;
    double t = fmin(1, fmax(0, ((double)sh / h - 2) / 2)), g = 1 - 0.4 * t, a = 0.2 * t;
    double *wx = ref_box_weights(sw, w), *wy = ref_box_weights(sh, h);
    double *pl = (double *)calloc((size_t)w * h * 3, sizeof(double));
    CeImage *dst = ce_image_new(w, h);
    for (c = 0; c < 3; c++)
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                double s = 0;
                for (j = 0; j < sh; j++)
                    if (wy[(size_t)y * sh + j] > 0)
                        for (i = 0; i < sw; i++)
                            if (wx[(size_t)x * sw + i] > 0)
                                s += wy[(size_t)y * sh + j] * wx[(size_t)x * sw + i] *
                                     pow(((src->px[j * src->stride + i] >> (8 * c)) & 255) / 255.0, g);
                pl[((size_t)y * w + x) * 3 + c] = s;
            }
    for (c = 0; c < 3; c++) {
        for (y = 0; y < h; y++)
            ref_sharpen(pl + (size_t)y * w * 3 + c, w, 3, a);
        for (x = 0; x < w; x++)
            ref_sharpen(pl + (size_t)x * 3 + c, h, 3 * w, a);
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t p = 0xff000000u;
            for (c = 0; c < 3; c++)
                p |= (uint32_t)floor(255 * pow(pl[((size_t)y * w + x) * 3 + c], 1 / g) + 0.5) << (8 * c);
            dst->px[y * w + x] = p;
        }
    free(wx);
    free(wy);
    free(pl);
    return dst;
}

/* The card resampler (docs/DESIGN.md "Crispness decisions"): the float reference within 1 LSB
 * (including 1- and 2-pixel edges), flat colours exact at every strength, dark bias and no halos,
 * plain box where it is off, fallbacks. */
static void test_card_filter(void)
{
    static const int dims[][4] = { { 40, 56, 7, 10 }, { 40, 56, 10, 14 }, { 40, 56, 12, 18 }, { 40, 56, 30, 25 },
                                   { 40, 56, 1, 10 }, { 40, 56, 2, 14 }, { 40, 56, 7, 1 }, { 40, 56, 1, 1 },
                                   { 40, 56, 3, 2 }, { 33, 47, 21, 20 } };
    CeImage *src, *a, *b;
    CeCardFilter *f;
    size_t k;
    int v, x, y, bad = 0;
    /* a flat colour stays exactly the same (fwd/inv LUT round trip, weights summing to 1) */
    for (k = 0; k < sizeof dims / sizeof dims[0]; k++)
        for (v = 0; v < 256; v++) {
            uint32_t c = CE_RGB(v, 255 - v, v / 2 + 64);
            src = solid(dims[k][0], dims[k][1], c);
            a = ce_image_resample_card(src, dims[k][2], dims[k][3], 1);
            for (y = 0; a && y < a->h; y++)
                for (x = 0; x < a->w; x++)
                    bad += a->px[y * a->stride + x] != c;
            bad += !a;
            ce_image_free(a);
            ce_image_free(src);
        }
    CHECK(bad == 0, "%d pixels of flat colours changed", bad);

    /* random art against the float reference */
    for (k = 0, bad = 0; k < sizeof dims / sizeof dims[0]; k++) {
        int worst = 0;
        src = opaque_noise(dims[k][0], dims[k][1]);
        a = ce_image_resample_card(src, dims[k][2], dims[k][3], 1);
        b = ref_card_resample(src, dims[k][2], dims[k][3]);
        for (v = 0; a && v < a->w * a->h; v++) {
            int c;
            for (c = 0; c < 32; c += 8) {
                int d = abs((int)((a->px[v] >> c) & 255) - (int)((b->px[v] >> c) & 255));
                worst = d > worst ? d : worst;
            }
        }
        CHECK(a && worst <= 1, "%dx%d -> %dx%d: %d LSB from the reference", dims[k][0], dims[k][1], dims[k][2],
              dims[k][3], worst);
        ce_image_free(a);
        ce_image_free(b);
        ce_image_free(src);
    }

    /* dark bias at full strength (f >= 4): a 50 % black/white checkerboard averages v^0.6 to
     * 0.5^(1/0.6) = 0.315 -> 80 (the box gives 128) */
    src = ce_image_new(16, 16);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            src->px[y * 16 + x] = (x + y) & 1 ? CE_RGB(255, 255, 255) : CE_RGB(0, 0, 0);
    a = ce_image_resample_card(src, 4, 4, 1);
    b = ce_image_resample(src, 4, 4, 1);
    CHECK(a && a->px[5] == CE_RGB(80, 80, 80) && b->px[5] == CE_RGB(128, 128, 128), "checker %08x %08x",
          a ? a->px[5] : 0, b->px[5]);
    ce_image_free(a);
    ce_image_free(b);
    ce_image_free(src);

    /* a dark line on grey: darker than the box, and no light halo beside it (overshoot clamp) */
    src = solid(400, 560, CE_RGB(160, 160, 160));
    ce_fill_rect(src, 199, 0, 3, 560, CE_RGB(0, 0, 0));   /* 0.53 of output column 35 */
    a = ce_image_resample_card(src, 71, 96, 1);
    b = ce_image_resample(src, 71, 96, 1);
    {
        uint32_t amin = 255, bmin = 255, amax = 0;
        for (x = 0; a && x < 71; x++) {
            uint32_t pa = R(a->px[48 * 71 + x]), pb = R(b->px[48 * 71 + x]);
            amin = pa < amin ? pa : amin;
            bmin = pb < bmin ? pb : bmin;
            amax = pa > amax ? pa : amax;
        }
        CHECK(a && amin + 20 < bmin && amax == 160, "line: min %u (box %u), max %u", amin, bmin, amax);
    }
    ce_image_free(a);
    ce_image_free(b);
    ce_image_free(src);

    /* off for t < 0.1 (h >= 255 from 560: 1080p maximised is h257) and for upscales: exactly the box */
    src = opaque_noise(400, 560);
    for (k = 0; k < 4; k++) {
        static const int out[4][2] = { { 189, 255 }, { 190, 257 }, { 213, 288 }, { 430, 581 } };
        a = ce_image_resample_card(src, out[k][0], out[k][1], 1);
        b = ce_image_resample(src, out[k][0], out[k][1], 1);
        CHECK(same_px(a, b), "box at %dx%d", out[k][0], out[k][1]);
        ce_image_free(a);
        ce_image_free(b);
    }
    /* ... and on at h254 (t = 0.102): not the box */
    a = ce_image_resample_card(src, 188, 254, 1);
    b = ce_image_resample(src, 188, 254, 1);
    CHECK(a && !same_px(a, b), "on at h254");
    ce_image_free(a);
    ce_image_free(b);
    /* quality 0 is the fast path */
    a = ce_image_resample_card(src, 71, 96, 0);
    b = ce_image_resample(src, 71, 96, 0);
    CHECK(same_px(a, b), "quality 0");
    ce_image_free(a);
    ce_image_free(b);
    /* a cached filter gives the one-off result; keys; mismatched source size */
    f = ce_card_filter_new(400, 560, 95, 128);
    a = ce_image_resample_card_filter(src, f);
    b = ce_image_resample_card(src, 95, 128, 1);
    CHECK(same_px(a, b), "filter reuse");
    ce_image_free(a);
    a = ce_image_resample_card_filter(src, f);
    CHECK(same_px(a, b), "filter reuse twice");
    ce_image_free(a);
    ce_image_free(b);
    CHECK(ce_card_filter_is(f, 400, 560, 95, 128) && !ce_card_filter_is(f, 400, 560, 95, 127) &&
          !ce_card_filter_is(f, 200, 280, 95, 128) && !ce_card_filter_is(NULL, 400, 560, 95, 128), "keys");
    ce_image_free(src);
    src = opaque_noise(200, 280);
    CHECK(ce_image_resample_card_filter(src, f) == NULL && ce_image_resample_card_filter(NULL, f) == NULL &&
          ce_image_resample_card_filter(src, NULL) == NULL, "size mismatch / NULL");
    CHECK(ce_card_filter_new(0, 560, 95, 128) == NULL && ce_image_resample_card(src, 0, 5, 1) == NULL, "bad sizes");
    ce_card_filter_free(f);
    ce_card_filter_free(NULL);
    /* not opaque: falls back to the premultiplied box */
    src->px[1234] = CE_ARGB(128, 10, 20, 30);
    a = ce_image_resample_card(src, 50, 70, 1);
    b = ce_image_resample(src, 50, 70, 1);
    CHECK(same_px(a, b), "non-opaque source -> box");
    ce_image_free(a);
    ce_image_free(b);
    ce_image_free(src);
}

/* The shipped masters through the card set (clean, card resampler, finish) against the crisplab
 * model (tests/card_golden.h, tools/crisplab/proto/make_golden.py): every sample within 1 LSB, the
 * per-channel sums within npix/16. At h257 the resampler is the old box, bit for bit. */
static void test_card_golden(void)
{
    static const char ranks[] = "A23456789TJQK", suits[] = "CDHS";
    FcNativeAssets na;
    CeCardSet *cs;
    size_t i, k;
    int worst = 0, bad = 0, n = 0, same = 0;
    double sum_worst = 0;
    fc_native_assets_init(&na, "res");
    cs = ce_cardset_new(fc_native_asset_loader, &na, 0);
    if (!cs) {
        printf("SKIP card golden: no assets in res/\n");
        return;
    }
    for (i = 0; i < sizeof card_golden / sizeof card_golden[0]; i++) {
        const CardGolden *g = &card_golden[i];
        int c = (int)((strchr(ranks, g->card[0]) - ranks) * 4 + (strchr(suits, g->card[1]) - suits));
        const CeImage *s;
        long sum[4] = { 0, 0, 0, 0 }, tol = (long)g->cw * g->ch / 16;
        int x, y, ch;
        ce_cardset_set_size(cs, g->cw, g->ch, 1);
        s = ce_cardset_card(cs, c);
        CHECK(s && s->w == g->cw && s->h == g->ch, "%s h%d: sprite", g->card, g->ch);
        if (!s)
            continue;
        for (y = 0; y < s->h; y++)
            for (x = 0; x < s->w; x++)
                for (ch = 0; ch < 4; ch++)
                    sum[ch] += (s->px[y * s->stride + x] >> (8 * ch)) & 255;
        for (ch = 0; ch < 4; ch++) {
            double e = (double)labs(sum[ch] - g->sum[ch]) / ((double)g->cw * g->ch);
            sum_worst = e > sum_worst ? e : sum_worst;
            CHECK(labs(sum[ch] - g->sum[ch]) <= tol, "%s h%d channel %d: sum %ld, model %ld (tolerance %ld)",
                  g->card, g->ch, ch, sum[ch], g->sum[ch], tol);
        }
        for (k = 0; k < sizeof g->s / sizeof g->s[0]; k++) {
            uint32_t p = s->px[g->s[k].y * s->stride + g->s[k].x], q = g->s[k].argb;
            int d = 0;
            for (ch = 0; ch < 32; ch += 8) {
                int e = abs((int)((p >> ch) & 255) - (int)((q >> ch) & 255));
                d = e > d ? e : d;
            }
            worst = d > worst ? d : worst;
            if (d > 1) {
                bad++;
                printf("  %s h%d (%d,%d): %08x, model %08x\n", g->card, g->ch, g->s[k].x, g->s[k].y, p, q);
            }
            n++;
        }
    }
    CHECK(bad == 0, "%d of %d samples more than 1 LSB from the model", bad, n);
    printf("card golden: %d sprites, %d samples, worst %d LSB; sums off by <= %.4f LSB/pixel\n",
           (int)(sizeof card_golden / sizeof card_golden[0]), n, worst, sum_worst);

    /* h257 (1080p): the card resampler is the v1.1 box, bit for bit, on the real masters */
    for (i = 0; i < 4; i++) {
        static const char *codes[4] = { "AS", "TC", "QH", "KS" };
        int c = (int)((strchr(ranks, codes[i][0]) - ranks) * 4 + (strchr(suits, codes[i][1]) - suits));
        size_t len;
        const void *data = fc_native_asset_loader(CE_ASSET_CARD0 + c, &len, &na);
        CeImage *m = data ? ce_image_decode_png(data, len) : NULL, *o, *a, *b;
        if (!m)
            continue;
        o = solid(m->w, m->h, CE_RGB(255, 255, 255));   /* opaque, as the card set's masters */
        ce_blit(o, m, 0, 0);
        a = ce_image_resample_card(o, 190, 257, 1);
        b = ce_image_resample(o, 190, 257, 1);
        same += same_px(a, b);
        ce_image_free(a);
        ce_image_free(b);
        ce_image_free(o);
        ce_image_free(m);
    }
    CHECK(same == 4, "h257: %d of 4 masters equal the box", same);
    ce_cardset_free(cs);
    fc_native_assets_free(&na);
}

static void test_compositing(void)
{
    CeImage *dst = solid(8, 8, CE_RGB(0, 127, 0)), *src = ce_image_new(4, 4);
    int i;
    uint32_t p;
    /* opaque, transparent, half-transparent white */
    src->px[0] = CE_RGB(255, 0, 0);
    src->px[1] = 0;
    src->px[2] = CE_ARGB(128, 128, 128, 128);
    src->px[3] = CE_ARGB(255, 10, 20, 30);
    ce_blit(dst, src, 2, 3);
    p = dst->px[3 * 8 + 2];
    CHECK(p == CE_RGB(255, 0, 0), "opaque %08x", p);
    p = dst->px[3 * 8 + 3];
    CHECK(p == CE_RGB(0, 127, 0), "transparent %08x", p);
    p = dst->px[3 * 8 + 4];
    CHECK(A(p) == 255 && R(p) == 128 && B(p) == 128 && G(p) == 128 + 63, "half %08x", p);  /* 127*127/255 = 63.25 */

    /* exact src-over vs. floating point over many random pairs */
    {
        int bad = 0;
        CeImage *d1 = ce_image_new(1, 1), *s1 = ce_image_new(1, 1);
        for (i = 0; i < 20000; i++) {
            uint32_t s = random_premul(), d = random_premul(), o;
            int c;
            d1->px[0] = d;
            s1->px[0] = s;
            ce_blit(d1, s1, 0, 0);
            o = d1->px[0];
            for (c = 0; c < 32; c += 8) {
                double want = ((s >> c) & 255) + ((d >> c) & 255) * (255 - A(s)) / 255.0;
                if (fabs(((o >> c) & 255) - want) > 0.5 + 1e-9)
                    bad++;
            }
        }
        CHECK(bad == 0, "%d channels off by more than rounding", bad);
        ce_image_free(d1);
        ce_image_free(s1);
    }

    /* inversion: XP selection look (255 - RGB), transparent corners untouched, alpha kept */
    ce_fill_rect(dst, 0, 0, 8, 8, CE_RGB(0, 127, 0));
    ce_blit_inverted(dst, src, 0, 0);
    CHECK(dst->px[0] == CE_RGB(0, 255, 255), "red -> cyan %08x", dst->px[0]);
    CHECK(dst->px[1] == CE_RGB(0, 127, 0), "transparent stays %08x", dst->px[1]);
    CHECK(dst->px[3] == CE_RGB(245, 235, 225), "inverted %08x", dst->px[3]);
    p = dst->px[2];   /* straight colour 255 at alpha 128 inverts to 0: result = background * 127/255 */
    CHECK(R(p) == 0 && G(p) == 63 && B(p) == 0, "half inverted %08x", p);
    {
        CeImage *w = solid(2, 2, CE_RGB(255, 255, 255)), *k = solid(2, 2, CE_RGB(0, 0, 0));
        w->px[0] = 0;
        ce_blit_inverted(k, w, 0, 0);
        CHECK(k->px[1] == CE_RGB(0, 0, 0) && k->px[0] == CE_RGB(0, 0, 0), "white -> black");
        ce_image_free(w);
        ce_image_free(k);
    }

    /* clipping: blits and fills partly or wholly outside never touch memory outside */
    ce_blit(dst, src, -2, -3);
    ce_blit(dst, src, 6, 7);
    ce_blit(dst, src, 100, 100);
    ce_blit(dst, src, -100, 2);
    ce_fill_rect(dst, -5, -5, 7, 7, CE_RGB(1, 2, 3));
    CHECK(dst->px[0] == CE_RGB(1, 2, 3) && dst->px[2] != CE_RGB(1, 2, 3), "fill clip");
    ce_fill_rect(dst, 7, 7, 50, 50, CE_RGB(4, 5, 6));
    CHECK(dst->px[63] == CE_RGB(4, 5, 6) && dst->px[62] != CE_RGB(4, 5, 6), "fill clip br");
    ce_copy_rect(dst, 6, 6, src, 0, 0, 4, 4);
    CHECK(dst->px[6 * 8 + 6] == src->px[0] && dst->px[7 * 8 + 7] == src->px[5], "copy clip");
    ce_copy_rect(dst, 0, 0, src, -2, -2, 4, 4);
    CHECK(dst->px[2 * 8 + 2] == src->px[0], "copy src clip");
    ce_image_free(src);
    ce_image_free(dst);

    /* wrapped views share pixels with a stride */
    {
        CeImage *big = solid(10, 10, 0), v;
        v = ce_image_wrap(4, 4, 10, big->px + 3 * 10 + 2);
        ce_fill_rect(&v, 0, 0, 100, 100, CE_RGB(9, 9, 9));
        CHECK(big->px[3 * 10 + 2] == CE_RGB(9, 9, 9) && big->px[6 * 10 + 5] == CE_RGB(9, 9, 9) &&
              big->px[6 * 10 + 6] == 0 && big->px[7 * 10 + 5] == 0 && big->px[2 * 10 + 2] == 0, "wrap");
        ce_image_free(big);
    }
}

static void test_shapes(void)
{
    double area = 0, want;
    int x, y;
    CeImage *img;
    /* coverage integrates to the rounded rect's area */
    for (y = -1; y < 40; y++)
        for (x = -1; x < 30; x++)
            area += ce_round_rect_coverage(1.25, 2.5, 25.5, 33.0, 6.0, x, y);
    want = 25.5 * 33.0 - (4 - 3.14159265358979) * 36.0;
    CHECK(fabs(area - want) < 0.5, "area %f want %f", area, want);
    CHECK(ce_round_rect_coverage(0, 0, 10, 10, 3, 5, 5) == 1.0, "inside");
    CHECK(ce_round_rect_coverage(0, 0, 10, 10, 5, 0, 0) == 0.0, "corner cut");
    CHECK(ce_round_rect_coverage(0, 0, 10, 10, 3, 10, 5) == 0.0, "outside");

    /* square 1-px stroke is exactly the border */
    img = ce_image_new(20, 30);
    ce_stroke_round_rect(img, 0, 0, 20, 30, 0, 1, CE_RGB(0, 0, 0));
    CHECK(img->px[5 * 20] == 0xff000000u && img->px[5 * 20 + 19] == 0xff000000u && img->px[19] == 0xff000000u &&
          img->px[29 * 20 + 7] == 0xff000000u && img->px[5 * 20 + 1] == 0 && img->px[15 * 20 + 10] == 0,
          "square stroke");
    ce_image_free(img);
    /* rounded stroke: AA corner, crisp straight edges, nothing inside */
    img = ce_image_new(40, 40);
    ce_stroke_round_rect(img, 0, 0, 40, 40, 6, 1.5, CE_RGB(255, 0, 0));
    CHECK(img->px[20 * 40] == 0xffff0000u, "edge opaque %08x", img->px[20 * 40]);
    CHECK(A(img->px[20 * 40 + 1]) >= 126 && A(img->px[20 * 40 + 1]) <= 129, "half px %08x", img->px[20 * 40 + 1]);
    CHECK(img->px[0] == 0 && img->px[20 * 40 + 20] == 0 && img->px[20 * 40 + 2] == 0, "corner/interior");
    ce_image_free(img);

    /* card finish: transparent corners, crisp frame, art kept inside, alpha follows the shape */
    img = solid(71, 96, CE_RGB(200, 100, 50));
    img->px[0] = 0;   /* art alpha does not matter: shape is analytic */
    img->px[40 * 71 + 1] = CE_ARGB(128, 0, 0, 0);
    ce_image_card_finish(img, 3.6, 1, CE_RGB(0, 0, 0), CE_RGB(255, 255, 255));
    CHECK(img->px[0] == 0 && img->px[70] == 0 && img->px[95 * 71] == 0 && img->px[95 * 71 + 70] == 0, "corners");
    CHECK(img->px[50 * 71] == 0xff000000u && img->px[50 * 71 + 70] == 0xff000000u &&
          img->px[35] == 0xff000000u && img->px[95 * 71 + 35] == 0xff000000u, "frame");
    CHECK(img->px[50 * 71 + 1] == CE_RGB(200, 100, 50) && img->px[48 * 71 + 35] == CE_RGB(200, 100, 50), "art");
    CHECK(img->px[40 * 71 + 1] == CE_RGB(127, 127, 127), "art over white %08x", img->px[40 * 71 + 1]);
    {
        int bad = 0;
        for (y = 0; y < 96; y++)
            for (x = 0; x < 71; x++) {
                uint32_t p = img->px[y * 71 + x], a = A(p);
                double cov = ce_round_rect_coverage(0, 0, 71, 96, 3.6, x, y);
                bad += fabs(a - cov * 255) > 1.01 || R(p) > a || G(p) > a || B(p) > a;
            }
        CHECK(bad == 0, "%d pixels where alpha != shape coverage", bad);
    }
    ce_image_free(img);
}

/* The precomputed card shape gives exactly ce_image_card_finish (which the card set used per card in
 * v1): random art, sizes from tiny to 4K-like, fractional frames, radii above w/2. */
static void test_card_shape(void)
{
    static const int dims[][2] = { { 1, 1 }, { 3, 7 }, { 8, 8 }, { 71, 96 }, { 133, 180 }, { 191, 258 },
                                   { 300, 406 }, { 431, 583 } };
    static const double rt[][2] = { { 3.6, 1 }, { 0, 1 }, { 9.6, 1.353 }, { 500, 2 }, { 7.25, 0.5 } };
    size_t di, ri;
    int bad = 0, n = 0;
    for (di = 0; di < sizeof dims / sizeof dims[0]; di++)
        for (ri = 0; ri < sizeof rt / sizeof rt[0]; ri++) {
            int w = dims[di][0], h = dims[di][1], i;
            double r = rt[ri][0] * h / 96.0, t = rt[ri][1];
            CeImage *a = ce_image_new(w, h), *b = ce_image_new(w, h);
            CeCardShape *shape = ce_card_shape_new(w, h, r, t);
            uint32_t frame = ri == 2 ? CE_ARGB(200, 30, 60, 90) : CE_RGB(0, 0, 0);
            for (i = 0; i < w * h; i++)
                a->px[i] = b->px[i] = rnd() % 3 ? random_premul() : CE_RGB(rnd() & 255, rnd() & 255, rnd() & 255);
            ce_image_card_finish(a, r, t, frame, CE_RGB(255, 255, 255));
            ce_image_card_finish_shape(b, shape, frame, CE_RGB(255, 255, 255));
            bad += memcmp(a->px, b->px, sizeof(uint32_t) * (size_t)w * h) != 0;
            n++;
            CHECK(ce_card_shape_is(shape, w, h, r, t) && !ce_card_shape_is(shape, w, h, r, t + 0.25) &&
                  !ce_card_shape_is(shape, w + 1, h, r, t) && !ce_card_shape_is(NULL, w, h, r, t), "shape key");
            ce_card_shape_free(shape);
            ce_image_free(a);
            ce_image_free(b);
        }
    CHECK(bad == 0, "%d of %d shapes differ from ce_image_card_finish", bad, n);
}

/* Bevel ring: AA ring, light/dark split at the 45-degree mitre, nothing inside. */
static void test_bevel_ring(void)
{
    CeImage *r = ce_bevel_ring_new(191, 258, 2.6875, 9.6, CE_RGB(0, 0, 0), CE_RGB(0, 255, 0));
    CHECK(r && r->w == 191 && r->h == 258, "ring size");
    if (!r)
        return;
    CHECK(r->px[100 * 191 + 0] == CE_RGB(0, 0, 0) && r->px[0 * 191 + 95] == CE_RGB(0, 0, 0), "top/left dark");
    CHECK(r->px[100 * 191 + 190] == CE_RGB(0, 255, 0) && r->px[257 * 191 + 95] == CE_RGB(0, 255, 0),
          "bottom/right light");
    CHECK(r->px[100 * 191 + 95] == 0 && r->px[0] == 0 && r->px[100 * 191 + 4] == 0, "inside/corner empty");
    CHECK(A(r->px[100 * 191 + 2]) > 0 && A(r->px[100 * 191 + 2]) < 255, "fractional edge %08x", r->px[100 * 191 + 2]);
    ce_image_free(r);
}

static void test_png(void)
{
#ifdef CE_WITH_PNG_WRITER
    CeImage *img = ce_image_new(5, 3), *back;
    int i, ok = 1;
    const char *path = "build/host/test_image_roundtrip.png";
    FILE *f;
    long n;
    void *data;
    img->px[0] = CE_RGB(255, 0, 0);
    img->px[1] = CE_ARGB(128, 64, 0, 128);
    img->px[2] = 0;
    img->px[3] = CE_ARGB(255, 1, 2, 3);
    for (i = 4; i < 15; i++)
        img->px[i] = CE_ARGB(200, 100, 50, 0);
    if (!ce_image_write_png(img, path)) {
        printf("skip PNG round trip (cannot write %s)\n", path);
        ce_image_free(img);
        return;
    }
    f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)n);
    if (fread(data, 1, (size_t)n, f) != (size_t)n)
        ok = 0;
    fclose(f);
    remove(path);
    back = ce_image_decode_png(data, (size_t)n);
    CHECK(ok && back && back->w == 5 && back->h == 3, "decode");
    for (i = 0; back && i < 15; i++)
        CHECK(back->px[i] == img->px[i], "px %d: %08x != %08x", i, back->px[i], img->px[i]);
    CHECK(ce_image_decode_png(data, 10) == NULL, "truncated PNG rejected");
    free(data);
    ce_image_free(back);
    ce_image_free(img);
#endif
    CHECK(ce_image_decode_png("not a png", 9) == NULL, "garbage rejected");
    CHECK(ce_image_decode_png(NULL, 0) == NULL, "null rejected");
}

/* 2d: the drag's soft shadow and the hint pulse's partial inversion. */
static void test_shadow_and_pulse(void)
{
    CeImage *sq = ce_image_new(20, 30), *sh, *half, *full, *none;
    int x, y, sym = 1, mono = 1, black = 1;
    for (y = 0; y < 30; y++)
        for (x = 0; x < 20; x++)
            sq->px[y * 20 + x] = CE_RGB(200, 10, 90);
    sh = ce_image_shadow(sq, 3, 255);
    CHECK(sh && sh->w == 32 && sh->h == 42, "shadow size: the sprite plus twice the radius all round");
    if (sh) {
        CHECK((sh->px[21 * 32 + 16] >> 24) == 255, "the middle is fully dark: %u", sh->px[21 * 32 + 16] >> 24);
        CHECK((sh->px[0] >> 24) == 0, "the outer corner is clear: %u", sh->px[0] >> 24);
        for (y = 0; y < 42; y++)
            for (x = 0; x < 32; x++) {
                uint32_t p = sh->px[y * 32 + x];
                if ((p & 0xffffff) != 0)
                    black = 0;
                if (p != sh->px[y * 32 + (31 - x)] || p != sh->px[(41 - y) * 32 + x])
                    sym = 0;
                if (x > 0 && x <= 16 && (p >> 24) < (sh->px[y * 32 + x - 1] >> 24))
                    mono = 0;
                if ((x == 0 || y == 0 || x == 31 || y == 41) && (p >> 24) > 8)
                    black = 0;              /* only the faintest tail at the edge: nothing cut off */
            }
        CHECK(black, "premultiplied black, fading out by the edge");
        CHECK(sym, "symmetric");
        CHECK(mono, "darker towards the middle");
    }
    half = ce_image_shadow(sq, 3, 90);
    CHECK(half && (half->px[21 * 32 + 16] >> 24) == 90, "opacity 90: %u", half ? half->px[21 * 32 + 16] >> 24 : 0);
    ce_image_free(half);
    ce_image_free(sh);
    sh = ce_image_shadow(sq, 0, 255);
    CHECK(sh && sh->w == 20 && (sh->px[0] >> 24) == 255, "radius 0: the sprite's own shape");
    ce_image_free(sh);

    /* partial inversion: 0 nothing, 256 = ce_invert_masked, 128 half way */
    full = ce_image_new(20, 30);
    none = ce_image_new(20, 30);
    half = ce_image_new(20, 30);
    for (x = 0; x < 600; x++)
        full->px[x] = none->px[x] = half->px[x] = CE_RGB(200, 10, 90);
    ce_invert_masked(full, sq, 0, 0);
    ce_invert_masked_level(none, sq, 0, 0, 0);
    ce_invert_masked_level(half, sq, 0, 0, 256);
    CHECK(memcmp(half->px, full->px, 600 * 4) == 0, "level 256 is the full inversion");
    CHECK(none->px[0] == CE_RGB(200, 10, 90) && full->px[0] == CE_RGB(55, 245, 165), "level 0: nothing");
    for (x = 0; x < 600; x++)
        half->px[x] = CE_RGB(200, 10, 90);
    ce_invert_masked_level(half, sq, 0, 0, 128);
    CHECK(half->px[0] == CE_RGB(128, 128, 128) || half->px[0] == CE_RGB(127, 127, 127) ||
          ((half->px[0] >> 16) & 255) == 128, "level 128: half way (%06x)", half->px[0] & 0xffffff);
    ce_image_free(full);
    ce_image_free(none);
    ce_image_free(half);
    ce_image_free(sq);
}

int main(void)
{
    test_constant();
    test_box_values();
    test_alpha_edges();
    test_premul_invariant();
    test_card_filter();
    test_card_golden();
    test_compositing();
    test_shapes();
    test_card_shape();
    test_bevel_ring();
    test_png();
    test_shadow_and_pulse();
    printf("test_image: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
