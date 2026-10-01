/*
 * Prototype of the proposed card resampler (crisplab finalists C/D), written against the runtime's
 * image.c (included textually so the static taps_build/box path is reused unchanged).
 *
 *   fc_image_resample_card(src, w, h, g, a)
 *     src  opaque premultiplied master (alpha 255 everywhere: clean_master() output)
 *     g    space gamma in (0, 1]: box-average v^g instead of v (dark-biased, "stem darkening")
 *     a    3-tap sharpening amount >= 0 at output scale, kernel [-a/4, 1 + a/2, -a/4] in x then y
 *   The caller ramps both with the downscale factor f = src->h / h:
 *     t = clamp((f - 2) / 2, 0, 1);  g_eff = 1 - (1 - g) * t;  a_eff = a * t
 *   With g_eff == 1 and a_eff == 0 this is exactly fc_image_resample(src, w, h, 1).
 *
 * Integer only in the pixel loops (x87-free): uint16 power-space samples, 16.16 box weights (sum
 * exactly 65536), 4.12 sharpening weights, int32 intermediates. pow() only builds 256 + 4096 LUT
 * entries per size change.
 */
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "image.c"
#include "cardset.c"

FcImage *fc_image_resample_card(const FcImage *src, int w, int h, double g, double a)
{
    uint16_t fwd[256];
    uint8_t inv[4096];
    Taps tx, ty;
    uint32_t *acc;
    uint16_t *row;
    int32_t *plane, *tmp = NULL;
    FcImage *dst;
    int sw = src->w, x, y, i, c;
    int32_t k1 = (int32_t)floor(a / 4 * 4096 + 0.5), k0 = 4096 + 2 * k1;
    if (g >= 1.0 && k1 == 0)
        return fc_image_resample(src, w, h, 1);
    for (i = 0; i < 256; i++)
        fwd[i] = (uint16_t)floor(65535.0 * pow(i / 255.0, g) + 0.5);
    for (i = 0; i < 4096; i++)
        inv[i] = (uint8_t)floor(255.0 * pow((i * 16 + 8) / 65535.0, 1.0 / g) + 0.5);
    dst = fc_image_new(w, h);
    memset(&tx, 0, sizeof tx);
    memset(&ty, 0, sizeof ty);
    acc = (uint32_t *)malloc(sizeof(uint32_t) * 3 * (size_t)sw);
    row = (uint16_t *)malloc(sizeof(uint16_t) * 3 * (size_t)sw);
    plane = (int32_t *)malloc(sizeof(int32_t) * 3 * (size_t)w * h);
    if (k1)
        tmp = (int32_t *)malloc(sizeof(int32_t) * 3 * (size_t)w * h);
    if (!dst || !acc || !row || !plane || (k1 && !tmp) || !taps_build(&tx, sw, w, 1) ||
        !taps_build(&ty, src->h, h, 1))
        return NULL;   /* (prototype: leaks on failure) */
    /* box resample in power space: vertical pass streamed per output row, then horizontal */
    for (y = 0; y < h; y++) {
        const uint32_t *wy = ty.w + ty.off[y];
        memset(acc, 0, sizeof(uint32_t) * 3 * (size_t)sw);
        for (i = 0; i < ty.n[y]; i++) {
            const uint32_t *sp = src->px + (size_t)(ty.start[y] + i) * src->stride;
            uint32_t wt = wy[i], *ap = acc;
            if (!wt)
                continue;
            for (x = 0; x < sw; x++, ap += 3) {
                uint32_t p = sp[x];
                ap[0] += fwd[p & 255] * wt;            /* <= 65535 * 65536 < 2^32 */
                ap[1] += fwd[(p >> 8) & 255] * wt;
                ap[2] += fwd[(p >> 16) & 255] * wt;
            }
        }
        for (x = 0; x < 3 * sw; x++)
            row[x] = (uint16_t)((acc[x] + 32768u) >> 16);
        for (x = 0; x < w; x++) {
            const uint32_t *wx = tx.w + tx.off[x];
            const uint16_t *r = row + 3 * tx.start[x];
            uint32_t s0 = 0, s1 = 0, s2 = 0;
            for (i = 0; i < tx.n[x]; i++, r += 3) {
                s0 += r[0] * wx[i];
                s1 += r[1] * wx[i];
                s2 += r[2] * wx[i];
            }
            plane[3 * ((size_t)y * w + x) + 0] = (int32_t)((s0 + 32768u) >> 16);
            plane[3 * ((size_t)y * w + x) + 1] = (int32_t)((s1 + 32768u) >> 16);
            plane[3 * ((size_t)y * w + x) + 2] = (int32_t)((s2 + 32768u) >> 16);
        }
    }
    /* optional sharpening [-k1, k0, -k1]/4096, clamp-to-edge, x then y, no clamp in between */
    if (k1) {
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                for (c = 0; c < 3; c++) {
                    size_t o = 3 * ((size_t)y * w + x) + c;
                    int32_t l = plane[o - (x > 0 ? 3 : 0)], r = plane[o + (x < w - 1 ? 3 : 0)];
                    tmp[o] = (k0 * plane[o] - k1 * (l + r) + 2048) >> 12;   /* |.| < 2^31 */
                }
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                for (c = 0; c < 3; c++) {
                    size_t o = 3 * ((size_t)y * w + x) + c, st = 3 * (size_t)w;
                    int32_t u = tmp[o - (y > 0 ? st : 0)], d = tmp[o + (y < h - 1 ? st : 0)];
                    plane[o] = (k0 * tmp[o] - k1 * (u + d) + 2048) >> 12;
                }
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            const int32_t *v = plane + 3 * ((size_t)y * w + x);
            uint32_t o[3];
            for (c = 0; c < 3; c++) {
                int32_t q = v[c] < 0 ? 0 : (v[c] > 65535 ? 65535 : v[c]);
                o[c] = inv[q >> 4];
            }
            dst->px[(size_t)y * dst->stride + x] = 0xff000000u | (o[2] << 16) | (o[1] << 8) | o[0];
        }
    free(acc);
    free(row);
    free(plane);
    free(tmp);
    taps_free(&tx);
    taps_free(&ty);
    return dst;
}

static double ramp(double f) { double t = (f - 2.0) / 2.0; return t < 0 ? 0 : (t > 1 ? 1 : t); }

/* usage: resample_card <master.png> <ch> <g> <a> <out.raw>      -> one sprite (raw ARGB LE)
 *        resample_card --bench <dir-with-52-pngs> <ch> <g> <a>   -> ms for 52 cards, vs box */
int main(int argc, char **argv)
{
    static unsigned char buf[1 << 22];
    if (argc == 6 && strcmp(argv[1], "--bench")) {
        FILE *f = fopen(argv[1], "rb");
        size_t len = fread(buf, 1, sizeof buf, f);
        int ch = atoi(argv[2]), cw = (int)floor(71 * ch / 96.0 + 0.5);
        double t, g = atof(argv[3]), a = atof(argv[4]);
        FcImage *m, *s;
        fclose(f);
        m = fc_image_decode_png(buf, len);
        clean_master(m);
        t = ramp((double)m->h / ch);
        s = fc_image_resample_card(m, cw, ch, 1.0 - (1.0 - g) * t, a * t);
        fc_image_card_finish(s, 0.0372 * ch, frame_px(ch), FC_FRAME_COLOUR, FC_CARD_WHITE);
        f = fopen(argv[5], "wb");
        fwrite(s->px, 4, (size_t)cw * ch, f);
        fclose(f);
        return 0;
    }
    if (argc == 6) {
        static FcImage *m[52];
        const char *names[13] = {"A","2","3","4","5","6","7","8","9","T","J","Q","K"}, *suits = "CDHS";
        int ch = atoi(argv[3]), cw = (int)floor(71 * ch / 96.0 + 0.5), i, k, reps = 5;
        double g = atof(argv[4]), a = atof(argv[5]), tt, best[2] = {1e9, 1e9};
        for (i = 0; i < 52; i++) {
            char p[1024];
            FILE *f;
            size_t len;
            snprintf(p, sizeof p, "%s/%s%c.png", argv[2], names[i / 4], suits[i % 4]);
            f = fopen(p, "rb");
            len = fread(buf, 1, sizeof buf, f);
            fclose(f);
            m[i] = fc_image_decode_png(buf, len);
            clean_master(m[i]);
        }
        tt = ramp(560.0 / ch);
        for (k = 0; k < reps; k++) {
            int mode;
            for (mode = 0; mode < 2; mode++) {
                clock_t c0 = clock();
                for (i = 0; i < 52; i++) {
                    FcImage *s = mode ? fc_image_resample_card(m[i], cw, ch, 1.0 - (1.0 - g) * tt, a * tt)
                                      : fc_image_resample(m[i], cw, ch, 1);
                    fc_image_free(s);
                }
                double ms = 1000.0 * (clock() - c0) / CLOCKS_PER_SEC;
                if (ms < best[mode]) best[mode] = ms;
            }
        }
        printf("ch=%d cw=%d t=%.3f g_eff=%.3f a_eff=%.3f  box %.1f ms  card %.1f ms  ratio %.2f\n",
               ch, cw, tt, 1.0 - (1.0 - g) * tt, a * tt, best[0], best[1], best[1] / best[0]);
        return 0;
    }
    fprintf(stderr, "usage\n");
    return 2;
}
