/*
 * FreeCell HD — software images: PNG decode, high-quality resampling, compositing (see image.h).
 *
 * All pixels are premultiplied 0xAARRGGBB. Inner loops are integer/fixed point (no SSE assumption:
 * the exe targets -march=i686).
 */
#include "image.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_THREAD_LOCALS        /* no TLS (emutls) in the XP build */
#define STBI_ASSERT(x) ((void)0)     /* assert() under UNICODE imports _wassert, absent on XP */
#include "stb_image.h"
#ifdef FC_WITH_PNG_WRITER
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* ---- basics ----------------------------------------------------------------------------------- */

FcImage *fc_image_new(int w, int h)
{
    FcImage *img;
    if (w <= 0 || h <= 0 || (size_t)w > ((size_t)-1 / 4) / (size_t)h)
        return NULL;
    img = (FcImage *)malloc(sizeof *img);
    if (!img)
        return NULL;
    img->px = (uint32_t *)calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if (!img->px) {
        free(img);
        return NULL;
    }
    img->w = w;
    img->h = h;
    img->stride = w;
    img->owns = 1;
    return img;
}

FcImage fc_image_wrap(int w, int h, int stride, uint32_t *px)
{
    FcImage img;
    img.w = w;
    img.h = h;
    img.stride = stride;
    img.px = px;
    img.owns = 0;
    return img;
}

void fc_image_free(FcImage *img)
{
    if (!img)
        return;
    if (img->owns)
        free(img->px);
    free(img);
}

/* x * a / 255 rounded, exact for x, a in 0..255 */
static inline uint32_t mul255(uint32_t x, uint32_t a)
{
    uint32_t t = x * a + 128;
    return (t + (t >> 8)) >> 8;
}

FcImage *fc_image_decode_png(const void *data, size_t len)
{
    int w, h, n, i;
    unsigned char *rgba;
    FcImage *img;
    if (!data || len == 0 || len > 0x7fffffff)
        return NULL;
    rgba = stbi_load_from_memory((const stbi_uc *)data, (int)len, &w, &h, &n, 4);
    if (!rgba)
        return NULL;
    img = (FcImage *)malloc(sizeof *img);
    if (!img) {
        stbi_image_free(rgba);
        return NULL;
    }
    /* premultiply in place and adopt stb's buffer (STBI_FREE is free): no second copy */
    for (i = 0; i < w * h; i++) {
        const unsigned char *p = rgba + 4 * (size_t)i;
        uint32_t a = p[3], v = 0;
        if (a == 255)
            v = FC_ARGB(255, p[0], p[1], p[2]);
        else if (a)
            v = FC_ARGB(a, mul255(p[0], a), mul255(p[1], a), mul255(p[2], a));
        ((uint32_t *)(void *)rgba)[i] = v;
    }
    img->w = w;
    img->h = h;
    img->stride = w;
    img->px = (uint32_t *)(void *)rgba;
    img->owns = 1;
    return img;
}

/* ---- resampling --------------------------------------------------------------------------------
 * Separable filter with 16.16 fixed-point weights that sum to exactly 65536 per output pixel, so a
 * constant image stays constant and premultiplied colour never exceeds alpha. Vertical pass first,
 * streamed one output row at a time (one accumulator row of source width), then horizontal. */

typedef struct {
    int      *start;  /* first source index per output pixel */
    int      *n;      /* taps per output pixel */
    int      *off;    /* offset into w */
    uint32_t *w;      /* weights */
} Taps;

static void taps_free(Taps *t)
{
    free(t->start);
    free(t->n);
    free(t->off);
    free(t->w);
}

/* mode: 0 = identity/bilinear/box chosen from sizes and quality */
static int taps_build(Taps *t, int src_n, int dst_n, int quality)
{
    int i, j, k = 0, cap;
    int box = quality > 0 && dst_n < src_n;
    cap = box ? dst_n * (src_n / dst_n + 2) : dst_n * 2;
    t->start = (int *)malloc(sizeof(int) * (size_t)dst_n);
    t->n = (int *)malloc(sizeof(int) * (size_t)dst_n);
    t->off = (int *)malloc(sizeof(int) * (size_t)dst_n);
    t->w = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)cap);
    if (!t->start || !t->n || !t->off || !t->w) {
        taps_free(t);
        return 0;
    }
    for (i = 0; i < dst_n; i++) {
        t->off[i] = k;
        if (src_n == dst_n) {
            t->start[i] = i;
            t->n[i] = 1;
            t->w[k++] = 65536;
        } else if (box) {
            /* Exact footprint in units of 1/dst_n: output i covers [i*src_n, (i+1)*src_n); source j
             * covers [j*dst_n, (j+1)*dst_n). Cumulative rounding makes the weights sum to 65536. */
            long long f0 = (long long)i * src_n, f1 = f0 + src_n, cum = 0, prev = 0;
            int j0 = (int)(f0 / dst_n), j1 = (int)((f1 - 1) / dst_n);
            t->start[i] = j0;
            t->n[i] = j1 - j0 + 1;
            for (j = j0; j <= j1; j++) {
                long long a = f0 > (long long)j * dst_n ? f0 : (long long)j * dst_n;
                long long b = f1 < (long long)(j + 1) * dst_n ? f1 : (long long)(j + 1) * dst_n;
                long long q;
                cum += b - a;
                q = (cum * 65536 + src_n / 2) / src_n;
                t->w[k++] = (uint32_t)(q - prev);
                prev = q;
            }
        } else {
            /* bilinear, pixel centres aligned: centre = (i + 0.5) * src/dst - 0.5 */
            long long num = (long long)(2 * i + 1) * src_n - dst_n, den = 2LL * dst_n;
            long long j0 = num >= 0 ? num / den : -((-num + den - 1) / den);
            uint32_t w1 = (uint32_t)(((num - j0 * den) * 65536 + den / 2) / den);
            if (j0 < 0) {
                t->start[i] = 0;
                t->n[i] = 1;
                t->w[k++] = 65536;
            } else if (j0 >= src_n - 1) {
                t->start[i] = src_n - 1;
                t->n[i] = 1;
                t->w[k++] = 65536;
            } else if (w1 == 0 || w1 == 65536) {
                t->start[i] = (int)j0 + (w1 == 65536);
                t->n[i] = 1;
                t->w[k++] = 65536;
            } else {
                t->start[i] = (int)j0;
                t->n[i] = 2;
                t->w[k++] = 65536 - w1;
                t->w[k++] = w1;
            }
        }
    }
    return 1;
}

/* lerp two premultiplied pixels, weight f in 0..256 for b; two channels per multiply */
static inline uint32_t lerp(uint32_t a, uint32_t b, uint32_t f)
{
    uint32_t g = 256 - f;
    uint32_t rb = (((a & 0x00ff00ffu) * g + (b & 0x00ff00ffu) * f) >> 8) & 0x00ff00ffu;
    uint32_t ag = (((a >> 8) & 0x00ff00ffu) * g + ((b >> 8) & 0x00ff00ffu) * f) & 0xff00ff00u;
    return rb | ag;
}

/* bilinear sample position for output i: 16.16 fixed point, clamped to [0, n-1] */
static int32_t bl_pos(int i, int src_n, int dst_n)
{
    long long v = ((long long)(2 * i + 1) * src_n * 65536) / (2LL * dst_n) - 32768;
    if (v < 0) v = 0;
    if (v > (long long)(src_n - 1) * 65536) v = (long long)(src_n - 1) * 65536;
    return (int32_t)v;
}

/* Quality 0: direct 2D bilinear with 8-bit weights (cost proportional to the output size). */
static FcImage *resample_fast(const FcImage *src, int w, int h)
{
    FcImage *dst = fc_image_new(w, h);
    int32_t *xs;
    int x, y;
    if (!dst)
        return NULL;
    xs = (int32_t *)malloc(sizeof(int32_t) * (size_t)w);
    if (!xs) {
        fc_image_free(dst);
        return NULL;
    }
    for (x = 0; x < w; x++)
        xs[x] = bl_pos(x, src->w, w);
    for (y = 0; y < h; y++) {
        int32_t fy = bl_pos(y, src->h, h);
        int y0 = fy >> 16, y1 = y0 + 1 < src->h ? y0 + 1 : y0;
        uint32_t wy = (uint32_t)(fy & 0xffff) >> 8;
        const uint32_t *r0 = src->px + (size_t)y0 * src->stride, *r1 = src->px + (size_t)y1 * src->stride;
        uint32_t *out = dst->px + (size_t)y * dst->stride;
        for (x = 0; x < w; x++) {
            int x0 = xs[x] >> 16, x1 = x0 + 1 < src->w ? x0 + 1 : x0;
            uint32_t wx = (uint32_t)(xs[x] & 0xffff) >> 8;
            out[x] = lerp(lerp(r0[x0], r1[x0], wy), lerp(r0[x1], r1[x1], wy), wx);
        }
    }
    free(xs);
    return dst;
}

FcImage *fc_image_resample(const FcImage *src, int w, int h, int quality)
{
    FcImage *dst;
    Taps tx, ty;
    uint32_t *acc;
    uint16_t *row;
    int sw, x, y, i;
    if (!src || w <= 0 || h <= 0)
        return NULL;
    if (quality <= 0 && (w != src->w || h != src->h))
        return resample_fast(src, w, h);
    sw = src->w;
    dst = fc_image_new(w, h);
    if (!dst)
        return NULL;
    memset(&tx, 0, sizeof tx);
    memset(&ty, 0, sizeof ty);
    acc = (uint32_t *)malloc(sizeof(uint32_t) * 4 * (size_t)sw);
    row = (uint16_t *)malloc(sizeof(uint16_t) * 4 * (size_t)sw);
    if (!acc || !row || !taps_build(&tx, sw, w, quality) || !taps_build(&ty, src->h, h, quality)) {
        free(acc);
        free(row);
        taps_free(&tx);
        taps_free(&ty);
        fc_image_free(dst);
        return NULL;
    }
    for (y = 0; y < h; y++) {
        const uint32_t *wy = ty.w + ty.off[y];
        uint32_t *out = dst->px + (size_t)y * dst->stride;
        int n = ty.n[y];
        /* vertical pass into acc (channel sums scaled by 65536) */
        memset(acc, 0, sizeof(uint32_t) * 4 * (size_t)sw);
        for (i = 0; i < n; i++) {
            const uint32_t *sp = src->px + (size_t)(ty.start[y] + i) * src->stride;
            uint32_t wt = wy[i], *a = acc;
            if (!wt)
                continue;
            for (x = 0; x < sw; x++, a += 4) {
                uint32_t p = sp[x];
                if (!p)
                    continue;
                a[0] += (p & 255) * wt;
                a[1] += ((p >> 8) & 255) * wt;
                a[2] += ((p >> 16) & 255) * wt;
                a[3] += (p >> 24) * wt;
            }
        }
        for (x = 0; x < 4 * sw; x++)
            row[x] = (uint16_t)((acc[x] + 128) >> 8);   /* 8.8 fixed point, <= 65280 */
        /* horizontal pass */
        for (x = 0; x < w; x++) {
            const uint32_t *wx = tx.w + tx.off[x];
            const uint16_t *r = row + 4 * tx.start[x];
            uint32_t b = 0, g = 0, rr = 0, a = 0;
            int m = tx.n[x];
            for (i = 0; i < m; i++, r += 4) {
                uint32_t wt = wx[i];
                b += r[0] * wt;
                g += r[1] * wt;
                rr += r[2] * wt;
                a += r[3] * wt;
            }
            out[x] = (((a + (1u << 23)) >> 24) << 24) | (((rr + (1u << 23)) >> 24) << 16) |
                     (((g + (1u << 23)) >> 24) << 8) | ((b + (1u << 23)) >> 24);
        }
    }
    free(acc);
    free(row);
    taps_free(&tx);
    taps_free(&ty);
    return dst;
}

/* ---- card masters: dark-biased box + clamped 3-tap sharpen ---------------------------------------
 * docs/DESIGN.md "Crispness decisions"; the numpy model is tools/crisplab (candidate
 * 'art+darkbias+sharpen+clamp'), which this code matches within 1 LSB.
 *
 * Per size (floating point, once): f = src_h / h, t = clamp((f - 2) / 2, 0, 1), g = 1 - 0.4 t,
 * a = 0.2 t; t < 0.1 is plain fc_image_resample. LUTs: fwd[v] = 65535 (v/255)^g (uint16) and
 * inv[j] = 255 ((16 j + 8)/65535)^(1/g) (4096 entries, at most 0.12 LSB off), so a flat colour maps
 * to itself. Per card (integer only):
 *   1. the exact-area box (taps_build quality 1, 16.16 weights summing to 65536) on fwd[v]: vertical
 *      pass into uint32 sums (<= 65535 * 65536 < 2^32), rounded to 16 bits, then horizontal;
 *   2. [-k1, k0, -k1] / 4096 (k1 = round(a/4 * 4096), k0 = 4096 + 2 k1) along x, then along y on
 *      that result, clamp-to-edge; each sample is clamped to the [min, max] of the three it was
 *      computed from, which removes the overshoot halos of a plain sharpen and keeps every value in
 *      0..65535 (uint16 rows; int32 sums below 2^29);
 *   3. inv[v >> 4] per channel; alpha 255.
 * The y pass runs on a 3-row ring of x-sharpened rows, so the card is streamed row by row. */

#define CARD_T_MIN   0.1     /* below this ramp value (h >= 255 from 560): plain box */
#define CARD_G_DROP  0.4     /* g = 1 - 0.4 t */
#define CARD_SHARPEN 0.2     /* a = 0.2 t */

struct FcCardFilter {
    int      sw, sh, w, h;
    int      box;            /* 1: fc_image_resample(src, w, h, 1), nothing below is set */
    int32_t  k0, k1;         /* sharpen weights, 4.12 */
    Taps     tx, ty;
    uint16_t fwd[256];
    uint8_t  inv[4096];
};

FcCardFilter *fc_card_filter_new(int src_w, int src_h, int w, int h)
{
    FcCardFilter *f;
    double t, g, a;
    int i;
    if (src_w <= 0 || src_h <= 0 || w <= 0 || h <= 0)
        return NULL;
    f = (FcCardFilter *)calloc(1, sizeof *f);
    if (!f)
        return NULL;
    f->sw = src_w;
    f->sh = src_h;
    f->w = w;
    f->h = h;
    t = ((double)src_h / h - 2.0) / 2.0;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    if (t < CARD_T_MIN) {
        f->box = 1;
        return f;
    }
    g = 1.0 - CARD_G_DROP * t;
    a = CARD_SHARPEN * t;
    f->k1 = (int32_t)floor(a / 4 * 4096 + 0.5);
    f->k0 = 4096 + 2 * f->k1;
    for (i = 0; i < 256; i++)
        f->fwd[i] = (uint16_t)floor(65535.0 * pow(i / 255.0, g) + 0.5);
    for (i = 0; i < 4096; i++)
        f->inv[i] = (uint8_t)floor(255.0 * pow((16.0 * i + 8) / 65535.0, 1.0 / g) + 0.5);
    if (!taps_build(&f->tx, src_w, w, 1) || !taps_build(&f->ty, src_h, h, 1)) {
        fc_card_filter_free(f);
        return NULL;
    }
    return f;
}

void fc_card_filter_free(FcCardFilter *f)
{
    if (f) {
        taps_free(&f->tx);
        taps_free(&f->ty);
        free(f);
    }
}

int fc_card_filter_is(const FcCardFilter *f, int src_w, int src_h, int w, int h)
{
    return f && f->sw == src_w && f->sh == src_h && f->w == w && f->h == h;
}

/* [-k1, k0, -k1] / 4096 on (l, c, r), clamped to their [min, max]. The sum is negative beside a dark
 * edge (c = 0, l or r > 0); >> of a negative int32 is implementation-defined in C99 and arithmetic in
 * GCC, Clang and MSVC, so it stays negative, below min >= 0, and the clamp gives min (a logical shift
 * would give a large value and clamp to max instead). Flat runs (most of a card is white) return at
 * once. */
static inline uint32_t sharpen3(int32_t l, int32_t c, int32_t r, int32_t k0, int32_t k1)
{
    int32_t v, lo, hi;
    if (l == c && r == c)
        return (uint32_t)c;
    v = (k0 * c - k1 * (l + r) + 2048) >> 12;
    lo = l < r ? l : r;
    hi = l < r ? r : l;
    if (c < lo) lo = c;
    if (c > hi) hi = c;
    return (uint32_t)(v < lo ? lo : (v > hi ? hi : v));
}

FcImage *fc_image_resample_card_filter(const FcImage *src, const FcCardFilter *f)
{
    FcImage *dst;
    uint32_t *acc, opaque = 0xffffffffu;
    uint16_t *row, *line, *ring;
    int sw, w, h, x, y, i, n3;
    if (!src || !f || src->w != f->sw || src->h != f->sh)
        return NULL;
    if (f->box)
        return fc_image_resample(src, f->w, f->h, 1);
    sw = src->w;
    w = f->w;
    h = f->h;
    n3 = 3 * w;
    dst = fc_image_new(w, h);
    acc = (uint32_t *)malloc(sizeof(uint32_t) * 3 * (size_t)sw);
    row = (uint16_t *)malloc(sizeof(uint16_t) * 3 * (size_t)sw);
    line = (uint16_t *)malloc(sizeof(uint16_t) * 4 * (size_t)n3);   /* box row + the 3-row ring */
    if (!dst || !acc || !row || !line) {
        fc_image_free(dst);
        free(acc);
        free(row);
        free(line);
        return NULL;
    }
    ring = line + n3;
    for (y = 0; y <= h; y++) {
        if (y < h) {
            const uint32_t *wy = f->ty.w + f->ty.off[y];
            uint16_t *u = ring + (size_t)(y % 3) * n3;
            int n = f->ty.n[y];
            /* box, vertical: power-space sums of the source rows */
            memset(acc, 0, sizeof(uint32_t) * 3 * (size_t)sw);
            for (i = 0; i < n; i++) {
                const uint32_t *sp = src->px + (size_t)(f->ty.start[y] + i) * src->stride;
                uint32_t wt = wy[i], *a = acc;
                if (!wt)
                    continue;
                for (x = 0; x < sw; x++, a += 3) {
                    uint32_t p = sp[x];
                    opaque &= p;
                    a[0] += f->fwd[p & 255] * wt;
                    a[1] += f->fwd[(p >> 8) & 255] * wt;
                    a[2] += f->fwd[(p >> 16) & 255] * wt;
                }
            }
            for (x = 0; x < 3 * sw; x++)
                row[x] = (uint16_t)((acc[x] + 32768u) >> 16);
            /* box, horizontal */
            for (x = 0; x < w; x++) {
                const uint32_t *wx = f->tx.w + f->tx.off[x];
                const uint16_t *r = row + 3 * f->tx.start[x];
                uint32_t s0 = 0, s1 = 0, s2 = 0;
                int m = f->tx.n[x];
                for (i = 0; i < m; i++, r += 3) {
                    uint32_t wt = wx[i];
                    s0 += r[0] * wt;
                    s1 += r[1] * wt;
                    s2 += r[2] * wt;
                }
                line[3 * x] = (uint16_t)((s0 + 32768u) >> 16);
                line[3 * x + 1] = (uint16_t)((s1 + 32768u) >> 16);
                line[3 * x + 2] = (uint16_t)((s2 + 32768u) >> 16);
            }
            /* sharpen along x into the ring (channels interleaved: neighbours are 3 apart) */
            for (x = 0; x < 3; x++) {
                u[x] = (uint16_t)sharpen3(line[x], line[x], line[w > 1 ? x + 3 : x], f->k0, f->k1);
                u[n3 - 3 + x] = (uint16_t)sharpen3(line[w > 1 ? n3 - 6 + x : x], line[n3 - 3 + x],
                                                   line[n3 - 3 + x], f->k0, f->k1);
            }
            for (x = 3; x < n3 - 3; x++)
                u[x] = (uint16_t)sharpen3(line[x - 3], line[x], line[x + 3], f->k0, f->k1);
        }
        /* sharpen along y and convert output row y - 1 (rows y - 2 .. y, clamped to the image) */
        if (y > 0) {
            int oy = y - 1;
            const uint16_t *up = ring + (size_t)((oy > 0 ? oy - 1 : 0) % 3) * n3;
            const uint16_t *mid = ring + (size_t)(oy % 3) * n3;
            const uint16_t *dn = ring + (size_t)((oy + 1 < h ? oy + 1 : oy) % 3) * n3;
            uint32_t *out = dst->px + (size_t)oy * dst->stride;
            for (x = 0; x < w; x++, up += 3, mid += 3, dn += 3)
                out[x] = 0xff000000u |
                         (uint32_t)f->inv[sharpen3(up[2], mid[2], dn[2], f->k0, f->k1) >> 4] << 16 |
                         (uint32_t)f->inv[sharpen3(up[1], mid[1], dn[1], f->k0, f->k1) >> 4] << 8 |
                         f->inv[sharpen3(up[0], mid[0], dn[0], f->k0, f->k1) >> 4];
        }
    }
    free(acc);
    free(row);
    free(line);
    if ((opaque >> 24) != 255) {
        /* not a card master: a power of a premultiplied value is not premultiplied */
        fc_image_free(dst);
        return fc_image_resample(src, w, h, 1);
    }
    return dst;
}

FcImage *fc_image_resample_card(const FcImage *src, int w, int h, int quality)
{
    FcCardFilter *f;
    FcImage *dst;
    if (!src || w <= 0 || h <= 0)
        return NULL;
    if (quality <= 0)
        return fc_image_resample(src, w, h, quality);
    f = fc_card_filter_new(src->w, src->h, w, h);
    dst = f ? fc_image_resample_card_filter(src, f) : NULL;
    fc_card_filter_free(f);
    return dst;
}

/* ---- drawing ---------------------------------------------------------------------------------- */

/* Clip a w x h rectangle at (*x,*y) against dst; adjusts source offsets *sx,*sy. 0 if empty. */
static int clip(const FcImage *dst, int *x, int *y, int *w, int *h, int *sx, int *sy)
{
    if (*x < 0) { *w += *x; *sx -= *x; *x = 0; }
    if (*y < 0) { *h += *y; *sy -= *y; *y = 0; }
    if (*x + *w > dst->w) *w = dst->w - *x;
    if (*y + *h > dst->h) *h = dst->h - *y;
    return *w > 0 && *h > 0;
}

void fc_fill_rect(FcImage *dst, int x, int y, int w, int h, uint32_t argb)
{
    int sx = 0, sy = 0, i, j;
    if (!dst || !clip(dst, &x, &y, &w, &h, &sx, &sy))
        return;
    for (j = 0; j < h; j++) {
        uint32_t *d = dst->px + (size_t)(y + j) * dst->stride + x;
        for (i = 0; i < w; i++)
            d[i] = argb;
    }
}

/* d = s + d * (255 - sa) / 255 per channel, two channels per multiply */
static inline uint32_t over(uint32_t s, uint32_t d)
{
    uint32_t inv = 255 - (s >> 24), rb, ag;
    rb = (d & 0x00ff00ffu) * inv + 0x00800080u;
    rb = ((rb + ((rb >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
    ag = ((d >> 8) & 0x00ff00ffu) * inv + 0x00800080u;
    ag = (ag + ((ag >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
    return s + (rb | ag);
}

/* premultiplied inversion: c' = a - c (straight colour 255 - c, alpha kept) */
static inline uint32_t invert(uint32_t p)
{
    return (p & 0xff000000u) | ((p >> 24) * 0x010101u - (p & 0x00ffffffu));
}

static void blit(FcImage *dst, const FcImage *src, int dx, int dy, int inv)
{
    int sx = 0, sy = 0, w, h, i, j;
    if (!dst || !src)
        return;
    w = src->w;
    h = src->h;
    if (!clip(dst, &dx, &dy, &w, &h, &sx, &sy))
        return;
    for (j = 0; j < h; j++) {
        const uint32_t *s = src->px + (size_t)(sy + j) * src->stride + sx;
        uint32_t *d = dst->px + (size_t)(dy + j) * dst->stride + dx;
        for (i = 0; i < w; i++) {
            uint32_t p = s[i];
            if (p < 0x01000000u)
                continue;               /* fully transparent */
            if (inv)
                p = invert(p);
            d[i] = p >= 0xff000000u ? p : over(p, d[i]);
        }
    }
}

void fc_blit(FcImage *dst, const FcImage *src, int dx, int dy) { blit(dst, src, dx, dy, 0); }
void fc_blit_inverted(FcImage *dst, const FcImage *src, int dx, int dy) { blit(dst, src, dx, dy, 1); }

void fc_copy_rect(FcImage *dst, int dx, int dy, const FcImage *src, int sx, int sy, int w, int h)
{
    int j;
    if (!dst || !src)
        return;
    /* clip against the source first, then the destination */
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > src->w) w = src->w - sx;
    if (sy + h > src->h) h = src->h - sy;
    if (w <= 0 || h <= 0 || !clip(dst, &dx, &dy, &w, &h, &sx, &sy))
        return;
    for (j = 0; j < h; j++)
        memmove(dst->px + (size_t)(dy + j) * dst->stride + dx,
                src->px + (size_t)(sy + j) * src->stride + sx, sizeof(uint32_t) * (size_t)w);
}

/* ---- anti-aliased rounded rectangles ------------------------------------------------------------ */

static double overlap1(double a0, double a1, double b0, double b1)
{
    double lo = a0 > b0 ? a0 : b0, hi = a1 < b1 ? a1 : b1;
    return hi > lo ? hi - lo : 0.0;
}

/* Area of pixel (px,py) (the unit square [px,px+1) x [py,py+1)) inside the rounded rect. Straight
 * edges are exact; pixels on a corner arc are supersampled 8x8. */
double fc_round_rect_coverage(double x, double y, double w, double h, double r, int px, int py)
{
    double ox, oy, cx = px + 0.5, cy = py + 0.5, dx = 0, dy = 0, d2;
    int i, j, inside = 0;
    if (w <= 0 || h <= 0)
        return 0.0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;
    ox = overlap1(px, px + 1.0, x, x + w);
    oy = overlap1(py, py + 1.0, y, y + h);
    if (ox <= 0 || oy <= 0)
        return 0.0;
    /* offset of the pixel centre from the rect shrunk by r (zero on the straight parts) */
    if (cx < x + r) dx = x + r - cx;
    else if (cx > x + w - r) dx = cx - (x + w - r);
    if (cy < y + r) dy = y + r - cy;
    else if (cy > y + h - r) dy = cy - (y + h - r);
    if (dx == 0 || dy == 0)
        return ox * oy;
    d2 = dx * dx + dy * dy;
    if (r > 0.7072 && d2 <= (r - 0.7072) * (r - 0.7072))
        return ox * oy;                       /* well inside the arc */
    if (d2 >= (r + 0.7072) * (r + 0.7072))
        return 0.0;                           /* well outside */
    for (j = 0; j < 8; j++) {
        double sy = py + (j + 0.5) / 8;
        for (i = 0; i < 8; i++) {
            double sx = px + (i + 0.5) / 8;
            dx = dy = 0;
            if (sx < x || sx >= x + w || sy < y || sy >= y + h)
                continue;
            if (sx < x + r) dx = x + r - sx;
            else if (sx > x + w - r) dx = sx - (x + w - r);
            if (sy < y + r) dy = y + r - sy;
            else if (sy > y + h - r) dy = sy - (y + h - r);
            if (dx * dx + dy * dy <= r * r)
                inside++;
        }
    }
    return inside / 64.0;
}

void fc_stroke_round_rect(FcImage *dst, double x, double y, double w, double h, double r, double t,
                          uint32_t argb)
{
    int x0, y0, x1, y1, px, py;
    double band, ri;
    uint32_t a = argb >> 24, cr = (argb >> 16) & 255, cg = (argb >> 8) & 255, cb = argb & 255;
    if (!dst || w <= 0 || h <= 0 || t <= 0 || a == 0)
        return;
    ri = r - t > 0 ? r - t : 0;
    band = (r > t ? r : t) + 1;   /* pixels farther than this from the edge are untouched */
    x0 = (int)floor(x);
    y0 = (int)floor(y);
    x1 = (int)ceil(x + w);
    y1 = (int)ceil(y + h);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > dst->w) x1 = dst->w;
    if (y1 > dst->h) y1 = dst->h;
    for (py = y0; py < y1; py++) {
        int mid = py >= y + band && py + 1 <= y + h - band;
        uint32_t *d = dst->px + (size_t)py * dst->stride;
        for (px = x0; px < x1; px++) {
            double cov;
            uint32_t ca, s;
            if (mid && px >= x + band && px + 1 <= x + w - band) {
                px = (int)floor(x + w - band) - 1;   /* skip the interior */
                continue;
            }
            cov = fc_round_rect_coverage(x, y, w, h, r, px, py) -
                  fc_round_rect_coverage(x + t, y + t, w - 2 * t, h - 2 * t, ri, px, py);
            if (cov <= 0)
                continue;
            ca = (uint32_t)(cov * a + 0.5);
            if (!ca)
                continue;
            s = FC_ARGB(ca, mul255(cr, ca), mul255(cg, ca), mul255(cb, ca));
            d[px] = ca == 255 ? s : over(s, d[px]);
        }
    }
}

/* Card shape: per pixel (ring << 8) | in, the frame ring's and the inside's coverage (0..255, ring +
 * in <= 255), or SHAPE_INTERIOR where the pixel is plain art (composited over 'under'). */
#define SHAPE_INTERIOR 0xffffu

struct FcCardShape {
    int       w, h;
    double    r, t;          /* as requested (the key) */
    uint16_t *cov;           /* w * h */
};

typedef struct ShapeGeom { double w, h, r, t, ri, band; } ShapeGeom;

static void shape_geom(ShapeGeom *g, int w, int h, double r, double t)
{
    g->w = w;
    g->h = h;
    if (r > g->w / 2) r = g->w / 2;
    if (r > g->h / 2) r = g->h / 2;
    g->r = r;
    g->t = t;
    g->ri = r - t > 0 ? r - t : 0;
    g->band = r + 1;
}

/* Pixels x0 .. x1-1 of row py. Only the edge pixels need the (floating-point) coverage. */
static void shape_span(const ShapeGeom *g, int py, int x0, int x1, uint16_t *out)
{
    double w = g->w, h = g->h, t = g->t, band = g->band;
    int ey = py < t + 1 || py + 1 > h - t - 1, cy = py < band || py + 1 > h - band, px;
    for (px = x0; px < x1; px++) {
        double co, ci;
        uint32_t ring, in;
        int edge = ey || px < t + 1 || px + 1 > w - t - 1 || (cy && (px < band || px + 1 > w - band));
        if (!edge) {
            *out++ = SHAPE_INTERIOR;
            continue;
        }
        co = fc_round_rect_coverage(0, 0, w, h, g->r, px, py);
        ci = fc_round_rect_coverage(t, t, w - 2 * t, h - 2 * t, g->ri, px, py);
        ring = (uint32_t)((co - ci) * 255 + 0.5);
        in = (uint32_t)(ci * 255 + 0.5);
        if (ring + in > 255)
            ring = 255 - in;
        *out++ = (uint16_t)(ring << 8 | in);
    }
}

/* Apply n shape values to n pixels: result = ring * frame + in * (art over under), the shape's alpha
 * being analytic. fpm: the frame colour premultiplied, B G R A. Integer only. */
static void finish_span(uint32_t *d, const uint16_t *m, int n, const uint32_t fpm[4], uint32_t under)
{
    int i;
    for (i = 0; i < n; i++) {
        uint32_t p = d[i], ring, in, c[4], k;
        if (m[i] == SHAPE_INTERIOR) {
            if (p < 0xff000000u)
                d[i] = over(p, under);
            continue;
        }
        if (p < 0xff000000u)
            p = over(p, under);
        ring = m[i] >> 8;
        in = m[i] & 255;
        for (k = 0; k < 4; k++) {
            c[k] = mul255(fpm[k], ring) + mul255((p >> (8 * k)) & 255, in);
            if (c[k] > 255)
                c[k] = 255;
        }
        if (c[0] > c[3]) c[0] = c[3];
        if (c[1] > c[3]) c[1] = c[3];
        if (c[2] > c[3]) c[2] = c[3];
        d[i] = (c[3] << 24) | (c[2] << 16) | (c[1] << 8) | c[0];
    }
}

static void frame_premul(uint32_t frame, uint32_t fpm[4])
{
    uint32_t fa = frame >> 24;
    fpm[0] = mul255(frame & 255, fa);
    fpm[1] = mul255((frame >> 8) & 255, fa);
    fpm[2] = mul255((frame >> 16) & 255, fa);
    fpm[3] = fa;
}

FcCardShape *fc_card_shape_new(int w, int h, double r, double t)
{
    FcCardShape *s;
    ShapeGeom g;
    int py;
    if (w <= 0 || h <= 0 || (size_t)w > ((size_t)-1 / 2) / (size_t)h)
        return NULL;
    s = (FcCardShape *)malloc(sizeof *s);
    if (!s)
        return NULL;
    s->cov = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)w * (size_t)h);
    if (!s->cov) {
        free(s);
        return NULL;
    }
    s->w = w;
    s->h = h;
    s->r = r;
    s->t = t;
    shape_geom(&g, w, h, r, t);
    for (py = 0; py < h; py++)
        shape_span(&g, py, 0, w, s->cov + (size_t)py * w);
    return s;
}

void fc_card_shape_free(FcCardShape *shape)
{
    if (shape) {
        free(shape->cov);
        free(shape);
    }
}

int fc_card_shape_is(const FcCardShape *s, int w, int h, double r, double t)
{
    return s && s->w == w && s->h == h && s->r == r && s->t == t;
}

void fc_image_card_finish_shape(FcImage *img, const FcCardShape *shape, uint32_t frame, uint32_t under)
{
    uint32_t fpm[4];
    int py;
    if (!img || !shape || img->w != shape->w || img->h != shape->h)
        return;
    frame_premul(frame, fpm);
    for (py = 0; py < img->h; py++)
        finish_span(img->px + (size_t)py * img->stride, shape->cov + (size_t)py * shape->w, img->w, fpm,
                    under);
}

void fc_image_card_finish(FcImage *img, double r, double t, uint32_t frame, uint32_t under)
{
    uint16_t m[256];
    uint32_t fpm[4];
    ShapeGeom g;
    int px, py;
    if (!img)
        return;
    shape_geom(&g, img->w, img->h, r, t);
    frame_premul(frame, fpm);
    for (py = 0; py < img->h; py++)
        for (px = 0; px < img->w; px += 256) {
            int n = img->w - px < 256 ? img->w - px : 256;
            shape_span(&g, py, px, px + n, m);
            finish_span(img->px + (size_t)py * img->stride + px, m, n, fpm, under);
        }
}

/* ---- bevel ring (empty cells and the king frame on scaled-up boards) ----------------------------- */

/* 1 if the point is nearer the top or left edge than the bottom or right one (the 45-degree mitre) */
static int tl_side(int w, int h, double sx, double sy)
{
    double top = sy, left = sx, bottom = h - sy, right = w - sx;
    return (top < left ? top : left) < (bottom < right ? bottom : right);
}

FcImage *fc_bevel_ring_new(int w, int h, double t, double rad, uint32_t tl, uint32_t br)
{
    FcImage *img = fc_image_new(w, h);
    int px, py, band = (int)(t + rad) + 2;
    if (!img)
        return NULL;
    for (py = 0; py < h; py++) {
        uint32_t *d = img->px + (size_t)py * img->stride;
        int edge_row = py < band || py >= h - band;
        for (px = 0; px < w; px++) {
            double cov, f;
            int i, j, n;
            uint32_t a, rr, g, b;
            if (!edge_row && px >= band && px < w - band) {
                px = w - band - 1;   /* skip the inside of the ring */
                continue;
            }
            cov = fc_round_rect_coverage(0, 0, w, h, rad, px, py) -
                  fc_round_rect_coverage(t, t, w - 2 * t, h - 2 * t, rad > t ? rad - t : 0, px, py);
            if (cov <= 0.002)
                continue;
            for (n = 0, j = 0; j < 4; j++)
                for (i = 0; i < 4; i++)
                    n += tl_side(w, h, px + (i + 0.5) / 4, py + (j + 0.5) / 4);
            f = n / 16.0;
            a = (uint32_t)(cov * 255 + 0.5);
            rr = (uint32_t)((f * ((tl >> 16) & 255) + (1 - f) * ((br >> 16) & 255)) * cov + 0.5);
            g = (uint32_t)((f * ((tl >> 8) & 255) + (1 - f) * ((br >> 8) & 255)) * cov + 0.5);
            b = (uint32_t)((f * (tl & 255) + (1 - f) * (br & 255)) * cov + 0.5);
            d[px] = (a << 24) | (rr << 16) | (g << 8) | b;
        }
    }
    return img;
}

/* ---- PNG writer (native tools/tests only) ----------------------------------------------------- */

#ifdef FC_WITH_PNG_WRITER
int fc_image_write_png(const FcImage *img, const char *path)
{
    unsigned char *rgba;
    int i, j, ok;
    if (!img || !path)
        return 0;
    rgba = (unsigned char *)malloc((size_t)img->w * (size_t)img->h * 4);
    if (!rgba)
        return 0;
    for (j = 0; j < img->h; j++) {
        const uint32_t *s = img->px + (size_t)j * img->stride;
        unsigned char *d = rgba + (size_t)j * img->w * 4;
        for (i = 0; i < img->w; i++, d += 4) {
            uint32_t p = s[i], a = p >> 24;
            uint32_t r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            if (a && a < 255) {
                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
            }
            d[0] = (unsigned char)r;
            d[1] = (unsigned char)g;
            d[2] = (unsigned char)b;
            d[3] = (unsigned char)a;
        }
    }
    ok = stbi_write_png(path, img->w, img->h, 4, rgba, img->w * 4) != 0;
    free(rgba);
    return ok;
}
#else
int fc_image_write_png(const FcImage *img, const char *path)
{
    (void)img;
    (void)path;
    return 0;
}
#endif
