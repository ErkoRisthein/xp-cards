/*
 * FreeCell HD — native tests for src/gfx/image.c: resampling, compositing, inversion, AA shapes,
 * PNG round trip.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/image.h"

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
    return FC_ARGB(a, r, g, b);
}

static FcImage *solid(int w, int h, uint32_t p)
{
    FcImage *img = fc_image_new(w, h);
    fc_fill_rect(img, 0, 0, w, h, p);
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
                FcImage *src = solid(sizes[i][0], sizes[i][1], colours[k]);
                FcImage *dst = fc_image_resample(src, sizes[i][2], sizes[i][3], q);
                int x, y, bad = 0;
                CHECK(dst && dst->w == sizes[i][2] && dst->h == sizes[i][3], "size");
                for (y = 0; dst && y < dst->h; y++)
                    for (x = 0; x < dst->w; x++)
                        bad += dst->px[y * dst->stride + x] != colours[k];
                CHECK(bad == 0, "constant %08x %dx%d->%dx%d q%d: %d pixels changed", colours[k],
                      sizes[i][0], sizes[i][1], sizes[i][2], sizes[i][3], q, bad);
                fc_image_free(src);
                fc_image_free(dst);
            }
}

static void test_box_values(void)
{
    /* 1-D: [0, 255, 0] -> 2 px: each covers 1.5 source px: (0 + 0.5*255) / 1.5 = 85 */
    FcImage *src = fc_image_new(3, 1), *dst;
    src->px[1] = 0xffffffffu;
    src->px[0] = src->px[2] = 0xff000000u;
    dst = fc_image_resample(src, 2, 1, 1);
    CHECK(R(dst->px[0]) == 85 && R(dst->px[1]) == 85 && A(dst->px[0]) == 255, "got %08x %08x",
          dst->px[0], dst->px[1]);
    fc_image_free(dst);
    fc_image_free(src);

    /* 2x2 checkerboard of black/white to 1x1: exact mid grey (127.5 rounds to 128) */
    src = fc_image_new(2, 2);
    src->px[0] = src->px[3] = 0xffffffffu;
    src->px[1] = src->px[2] = 0xff000000u;
    dst = fc_image_resample(src, 1, 1, 1);
    CHECK(R(dst->px[0]) == 128 && G(dst->px[0]) == 128 && B(dst->px[0]) == 128, "got %08x", dst->px[0]);
    fc_image_free(dst);
    fc_image_free(src);

    /* independent x / y factors: 4x2 -> 2x2 averages only horizontally */
    src = fc_image_new(4, 2);
    src->px[0] = 0xff000000u; src->px[1] = 0xff646464u; src->px[2] = 0xffc8c8c8u; src->px[3] = 0xffffffffu;
    src->px[4] = 0xffffffffu; src->px[5] = 0xffffffffu; src->px[6] = 0xff000000u; src->px[7] = 0xff000000u;
    dst = fc_image_resample(src, 2, 2, 1);
    CHECK(R(dst->px[0]) == 50 && R(dst->px[1]) == 228 && R(dst->px[2]) == 255 && R(dst->px[3]) == 0,
          "got %08x %08x %08x %08x", dst->px[0], dst->px[1], dst->px[2], dst->px[3]);
    fc_image_free(dst);
    fc_image_free(src);

    /* bilinear upscale 2 -> 4: centres at -0.25, 0.25, 0.75, 1.25 -> 0, 63.75, 191.25, 255 */
    src = fc_image_new(2, 1);
    src->px[0] = 0xff000000u;
    src->px[1] = 0xffffffffu;
    dst = fc_image_resample(src, 4, 1, 1);
    CHECK(R(dst->px[0]) == 0 && R(dst->px[1]) == 64 && R(dst->px[2]) == 191 && R(dst->px[3]) == 255,
          "got %u %u %u %u", R(dst->px[0]), R(dst->px[1]), R(dst->px[2]), R(dst->px[3]));
    fc_image_free(dst);
    fc_image_free(src);

    /* mean is preserved by the box filter (integer factor) */
    {
        int x, y;
        double s0 = 0, s1 = 0;
        src = fc_image_new(60, 40);
        for (y = 0; y < 40; y++)
            for (x = 0; x < 60; x++)
                src->px[y * 60 + x] = FC_RGB(0, 0, rnd() % 256);
        dst = fc_image_resample(src, 20, 10, 1);
        for (x = 0; x < 60 * 40; x++) s0 += B(src->px[x]);
        for (x = 0; x < 20 * 10; x++) s1 += B(dst->px[x]);
        CHECK(fabs(s0 / 2400 - s1 / 200) < 0.6, "means %f %f", s0 / 2400, s1 / 200);
        fc_image_free(dst);
        fc_image_free(src);
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
            FcImage *src = fc_image_new(97, 61), *dst;
            int bad = 0, partial = 0;
            for (y = 0; y < 61; y++)
                for (x = 0; x < 97; x++)
                    if ((x - 48) * (x - 48) + (y - 30) * (y - 30) < 27 * 27)
                        src->px[y * 97 + x] = colours[k];
            dst = fc_image_resample(src, 23, 17, q);
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
            fc_image_free(src);
            fc_image_free(dst);
        }
}

static void test_premul_invariant(void)
{
    int i, q;
    for (q = 0; q <= 1; q++) {
        FcImage *src = fc_image_new(53, 47), *dst;
        int bad = 0;
        for (i = 0; i < 53 * 47; i++)
            src->px[i] = random_premul();
        dst = fc_image_resample(src, 17, 29, q);
        for (i = 0; i < dst->w * dst->h; i++) {
            uint32_t p = dst->px[i];
            bad += R(p) > A(p) || G(p) > A(p) || B(p) > A(p);
        }
        CHECK(bad == 0, "q%d: %d pixels with colour > alpha", q, bad);
        fc_image_free(dst);
        dst = fc_image_resample(src, 120, 70, q);
        bad = 0;
        for (i = 0; i < dst->w * dst->h; i++) {
            uint32_t p = dst->px[i];
            bad += R(p) > A(p) || G(p) > A(p) || B(p) > A(p);
        }
        CHECK(bad == 0, "q%d upscale: %d pixels with colour > alpha", q, bad);
        fc_image_free(dst);
        fc_image_free(src);
    }
}

static void test_compositing(void)
{
    FcImage *dst = solid(8, 8, FC_RGB(0, 127, 0)), *src = fc_image_new(4, 4);
    int i;
    uint32_t p;
    /* opaque, transparent, half-transparent white */
    src->px[0] = FC_RGB(255, 0, 0);
    src->px[1] = 0;
    src->px[2] = FC_ARGB(128, 128, 128, 128);
    src->px[3] = FC_ARGB(255, 10, 20, 30);
    fc_blit(dst, src, 2, 3);
    p = dst->px[3 * 8 + 2];
    CHECK(p == FC_RGB(255, 0, 0), "opaque %08x", p);
    p = dst->px[3 * 8 + 3];
    CHECK(p == FC_RGB(0, 127, 0), "transparent %08x", p);
    p = dst->px[3 * 8 + 4];
    CHECK(A(p) == 255 && R(p) == 128 && B(p) == 128 && G(p) == 128 + 63, "half %08x", p);  /* 127*127/255 = 63.25 */

    /* exact src-over vs. floating point over many random pairs */
    {
        int bad = 0;
        FcImage *d1 = fc_image_new(1, 1), *s1 = fc_image_new(1, 1);
        for (i = 0; i < 20000; i++) {
            uint32_t s = random_premul(), d = random_premul(), o;
            int c;
            d1->px[0] = d;
            s1->px[0] = s;
            fc_blit(d1, s1, 0, 0);
            o = d1->px[0];
            for (c = 0; c < 32; c += 8) {
                double want = ((s >> c) & 255) + ((d >> c) & 255) * (255 - A(s)) / 255.0;
                if (fabs(((o >> c) & 255) - want) > 0.5 + 1e-9)
                    bad++;
            }
        }
        CHECK(bad == 0, "%d channels off by more than rounding", bad);
        fc_image_free(d1);
        fc_image_free(s1);
    }

    /* inversion: XP selection look (255 - RGB), transparent corners untouched, alpha kept */
    fc_fill_rect(dst, 0, 0, 8, 8, FC_RGB(0, 127, 0));
    fc_blit_inverted(dst, src, 0, 0);
    CHECK(dst->px[0] == FC_RGB(0, 255, 255), "red -> cyan %08x", dst->px[0]);
    CHECK(dst->px[1] == FC_RGB(0, 127, 0), "transparent stays %08x", dst->px[1]);
    CHECK(dst->px[3] == FC_RGB(245, 235, 225), "inverted %08x", dst->px[3]);
    p = dst->px[2];   /* straight colour 255 at alpha 128 inverts to 0: result = background * 127/255 */
    CHECK(R(p) == 0 && G(p) == 63 && B(p) == 0, "half inverted %08x", p);
    {
        FcImage *w = solid(2, 2, FC_RGB(255, 255, 255)), *k = solid(2, 2, FC_RGB(0, 0, 0));
        w->px[0] = 0;
        fc_blit_inverted(k, w, 0, 0);
        CHECK(k->px[1] == FC_RGB(0, 0, 0) && k->px[0] == FC_RGB(0, 0, 0), "white -> black");
        fc_image_free(w);
        fc_image_free(k);
    }

    /* clipping: blits and fills partly or wholly outside never touch memory outside */
    fc_blit(dst, src, -2, -3);
    fc_blit(dst, src, 6, 7);
    fc_blit(dst, src, 100, 100);
    fc_blit(dst, src, -100, 2);
    fc_fill_rect(dst, -5, -5, 7, 7, FC_RGB(1, 2, 3));
    CHECK(dst->px[0] == FC_RGB(1, 2, 3) && dst->px[2] != FC_RGB(1, 2, 3), "fill clip");
    fc_fill_rect(dst, 7, 7, 50, 50, FC_RGB(4, 5, 6));
    CHECK(dst->px[63] == FC_RGB(4, 5, 6) && dst->px[62] != FC_RGB(4, 5, 6), "fill clip br");
    fc_copy_rect(dst, 6, 6, src, 0, 0, 4, 4);
    CHECK(dst->px[6 * 8 + 6] == src->px[0] && dst->px[7 * 8 + 7] == src->px[5], "copy clip");
    fc_copy_rect(dst, 0, 0, src, -2, -2, 4, 4);
    CHECK(dst->px[2 * 8 + 2] == src->px[0], "copy src clip");
    fc_image_free(src);
    fc_image_free(dst);

    /* wrapped views share pixels with a stride */
    {
        FcImage *big = solid(10, 10, 0), v;
        v = fc_image_wrap(4, 4, 10, big->px + 3 * 10 + 2);
        fc_fill_rect(&v, 0, 0, 100, 100, FC_RGB(9, 9, 9));
        CHECK(big->px[3 * 10 + 2] == FC_RGB(9, 9, 9) && big->px[6 * 10 + 5] == FC_RGB(9, 9, 9) &&
              big->px[6 * 10 + 6] == 0 && big->px[7 * 10 + 5] == 0 && big->px[2 * 10 + 2] == 0, "wrap");
        fc_image_free(big);
    }
}

static void test_shapes(void)
{
    double area = 0, want;
    int x, y;
    FcImage *img;
    /* coverage integrates to the rounded rect's area */
    for (y = -1; y < 40; y++)
        for (x = -1; x < 30; x++)
            area += fc_round_rect_coverage(1.25, 2.5, 25.5, 33.0, 6.0, x, y);
    want = 25.5 * 33.0 - (4 - 3.14159265358979) * 36.0;
    CHECK(fabs(area - want) < 0.5, "area %f want %f", area, want);
    CHECK(fc_round_rect_coverage(0, 0, 10, 10, 3, 5, 5) == 1.0, "inside");
    CHECK(fc_round_rect_coverage(0, 0, 10, 10, 5, 0, 0) == 0.0, "corner cut");
    CHECK(fc_round_rect_coverage(0, 0, 10, 10, 3, 10, 5) == 0.0, "outside");

    /* square 1-px stroke is exactly the border */
    img = fc_image_new(20, 30);
    fc_stroke_round_rect(img, 0, 0, 20, 30, 0, 1, FC_RGB(0, 0, 0));
    CHECK(img->px[5 * 20] == 0xff000000u && img->px[5 * 20 + 19] == 0xff000000u && img->px[19] == 0xff000000u &&
          img->px[29 * 20 + 7] == 0xff000000u && img->px[5 * 20 + 1] == 0 && img->px[15 * 20 + 10] == 0,
          "square stroke");
    fc_image_free(img);
    /* rounded stroke: AA corner, crisp straight edges, nothing inside */
    img = fc_image_new(40, 40);
    fc_stroke_round_rect(img, 0, 0, 40, 40, 6, 1.5, FC_RGB(255, 0, 0));
    CHECK(img->px[20 * 40] == 0xffff0000u, "edge opaque %08x", img->px[20 * 40]);
    CHECK(A(img->px[20 * 40 + 1]) >= 126 && A(img->px[20 * 40 + 1]) <= 129, "half px %08x", img->px[20 * 40 + 1]);
    CHECK(img->px[0] == 0 && img->px[20 * 40 + 20] == 0 && img->px[20 * 40 + 2] == 0, "corner/interior");
    fc_image_free(img);

    /* card finish: transparent corners, crisp frame, art kept inside, alpha follows the shape */
    img = solid(71, 96, FC_RGB(200, 100, 50));
    img->px[0] = 0;   /* art alpha does not matter: shape is analytic */
    img->px[40 * 71 + 1] = FC_ARGB(128, 0, 0, 0);
    fc_image_card_finish(img, 3.6, 1, FC_RGB(0, 0, 0), FC_RGB(255, 255, 255));
    CHECK(img->px[0] == 0 && img->px[70] == 0 && img->px[95 * 71] == 0 && img->px[95 * 71 + 70] == 0, "corners");
    CHECK(img->px[50 * 71] == 0xff000000u && img->px[50 * 71 + 70] == 0xff000000u &&
          img->px[35] == 0xff000000u && img->px[95 * 71 + 35] == 0xff000000u, "frame");
    CHECK(img->px[50 * 71 + 1] == FC_RGB(200, 100, 50) && img->px[48 * 71 + 35] == FC_RGB(200, 100, 50), "art");
    CHECK(img->px[40 * 71 + 1] == FC_RGB(127, 127, 127), "art over white %08x", img->px[40 * 71 + 1]);
    {
        int bad = 0;
        for (y = 0; y < 96; y++)
            for (x = 0; x < 71; x++) {
                uint32_t p = img->px[y * 71 + x], a = A(p);
                double cov = fc_round_rect_coverage(0, 0, 71, 96, 3.6, x, y);
                bad += fabs(a - cov * 255) > 1.01 || R(p) > a || G(p) > a || B(p) > a;
            }
        CHECK(bad == 0, "%d pixels where alpha != shape coverage", bad);
    }
    fc_image_free(img);
}

/* The precomputed card shape gives exactly fc_image_card_finish (which the card set used per card in
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
            FcImage *a = fc_image_new(w, h), *b = fc_image_new(w, h);
            FcCardShape *shape = fc_card_shape_new(w, h, r, t);
            uint32_t frame = ri == 2 ? FC_ARGB(200, 30, 60, 90) : FC_RGB(0, 0, 0);
            for (i = 0; i < w * h; i++)
                a->px[i] = b->px[i] = rnd() % 3 ? random_premul() : FC_RGB(rnd() & 255, rnd() & 255, rnd() & 255);
            fc_image_card_finish(a, r, t, frame, FC_RGB(255, 255, 255));
            fc_image_card_finish_shape(b, shape, frame, FC_RGB(255, 255, 255));
            bad += memcmp(a->px, b->px, sizeof(uint32_t) * (size_t)w * h) != 0;
            n++;
            CHECK(fc_card_shape_is(shape, w, h, r, t) && !fc_card_shape_is(shape, w, h, r, t + 0.25) &&
                  !fc_card_shape_is(shape, w + 1, h, r, t) && !fc_card_shape_is(NULL, w, h, r, t), "shape key");
            fc_card_shape_free(shape);
            fc_image_free(a);
            fc_image_free(b);
        }
    CHECK(bad == 0, "%d of %d shapes differ from fc_image_card_finish", bad, n);
}

/* Bevel ring: AA ring, light/dark split at the 45-degree mitre, nothing inside. */
static void test_bevel_ring(void)
{
    FcImage *r = fc_bevel_ring_new(191, 258, 2.6875, 9.6, FC_RGB(0, 0, 0), FC_RGB(0, 255, 0));
    CHECK(r && r->w == 191 && r->h == 258, "ring size");
    if (!r)
        return;
    CHECK(r->px[100 * 191 + 0] == FC_RGB(0, 0, 0) && r->px[0 * 191 + 95] == FC_RGB(0, 0, 0), "top/left dark");
    CHECK(r->px[100 * 191 + 190] == FC_RGB(0, 255, 0) && r->px[257 * 191 + 95] == FC_RGB(0, 255, 0),
          "bottom/right light");
    CHECK(r->px[100 * 191 + 95] == 0 && r->px[0] == 0 && r->px[100 * 191 + 4] == 0, "inside/corner empty");
    CHECK(A(r->px[100 * 191 + 2]) > 0 && A(r->px[100 * 191 + 2]) < 255, "fractional edge %08x", r->px[100 * 191 + 2]);
    fc_image_free(r);
}

static void test_png(void)
{
#ifdef FC_WITH_PNG_WRITER
    FcImage *img = fc_image_new(5, 3), *back;
    int i, ok = 1;
    const char *path = "build/host/test_image_roundtrip.png";
    FILE *f;
    long n;
    void *data;
    img->px[0] = FC_RGB(255, 0, 0);
    img->px[1] = FC_ARGB(128, 64, 0, 128);
    img->px[2] = 0;
    img->px[3] = FC_ARGB(255, 1, 2, 3);
    for (i = 4; i < 15; i++)
        img->px[i] = FC_ARGB(200, 100, 50, 0);
    if (!fc_image_write_png(img, path)) {
        printf("skip PNG round trip (cannot write %s)\n", path);
        fc_image_free(img);
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
    back = fc_image_decode_png(data, (size_t)n);
    CHECK(ok && back && back->w == 5 && back->h == 3, "decode");
    for (i = 0; back && i < 15; i++)
        CHECK(back->px[i] == img->px[i], "px %d: %08x != %08x", i, back->px[i], img->px[i]);
    CHECK(fc_image_decode_png(data, 10) == NULL, "truncated PNG rejected");
    free(data);
    fc_image_free(back);
    fc_image_free(img);
#endif
    CHECK(fc_image_decode_png("not a png", 9) == NULL, "garbage rejected");
    CHECK(fc_image_decode_png(NULL, 0) == NULL, "null rejected");
}

int main(void)
{
    test_constant();
    test_box_values();
    test_alpha_edges();
    test_premul_invariant();
    test_compositing();
    test_shapes();
    test_card_shape();
    test_bevel_ring();
    test_png();
    printf("test_image: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
