/*
 * Card engine — native tests of the portable engine beyond images (test_image.c): rect helpers,
 * clipped drawing (engine/draw.h) against full renders, the persistence helpers (engine/store.h),
 * and the card set (engine/cardset.h): faces, quality and size semantics, card backs (lazy, missing,
 * the edge colour), the ring cache.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets_native.h"
#include "engine/cardset.h"
#include "engine/draw.h"
#include "engine/geom.h"
#include "engine/image.h"
#include "engine/store.h"

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

#define GREEN CE_RGB(0, 127, 0)
#define LIGHT CE_RGB(0, 255, 0)
#define DARK  CE_RGB(0, 0, 0)

static int same_image(const CeImage *a, const CeImage *b)
{
    int y;
    if (!a || !b || a->w != b->w || a->h != b->h)
        return 0;
    for (y = 0; y < a->h; y++)
        if (memcmp(a->px + (size_t)y * a->stride, b->px + (size_t)y * b->stride, (size_t)a->w * 4) != 0)
            return 0;
    return 1;
}

/* ---- geometry ----------------------------------------------------------------------------------- */

static void test_geom(void)
{
    CeRect a = ce_rect(10, 20, 30, 40), u, c;
    CHECK(ce_rect_overlaps(&a, 39, 59, 1, 1) && !ce_rect_overlaps(&a, 40, 20, 5, 5) &&
          !ce_rect_overlaps(&a, 10, 60, 5, 5) && !ce_rect_overlaps(&a, 0, 0, 10, 20), "overlaps (half-open)");
    CHECK(ce_rect_overlaps(&a, 10, 20, 30, 40) && !ce_rect_overlaps(&a, 9, 19, 1, 1), "edges");
    u = ce_rect_union(a, ce_rect(-5, 30, 10, 100));
    CHECK(u.x == -5 && u.y == 20 && u.w == 45 && u.h == 110, "union %d %d %d %d", u.x, u.y, u.w, u.h);
    c = ce_rect(-5, -6, 20, 20);
    CHECK(ce_rect_clip(&c, 10, 10) && c.x == 0 && c.y == 0 && c.w == 10 && c.h == 10, "clip");
    c = ce_rect(12, 0, 5, 5);
    CHECK(!ce_rect_clip(&c, 10, 10), "clipped away");
}

/* ---- drawing ------------------------------------------------------------------------------------ */

/* A little scene with every primitive; rendered whole and in tiles, the pixels must agree. */
static void scene(CeImage *fb, CeRect r, CeCardSet *cs, const CeImage *sprite)
{
    CeDraw d;
    if (!ce_draw_begin(&d, fb, r))
        return;
    ce_draw_fill(&d, d.clip.x, d.clip.y, d.clip.w, d.clip.h, GREEN);
    ce_draw_bevel(&d, ce_rect(3, 4, 71, 96), 1, DARK, LIGHT);
    ce_draw_bevel(&d, ce_rect(80, 4, 71, 96), 3, LIGHT, DARK);
    ce_draw_ring(&d, cs, ce_rect(160, 4, 142, 192), 2.6875, 7.1, DARK, LIGHT);
    ce_draw_sprite(&d, sprite, 7, 110, 0);
    ce_draw_sprite(&d, sprite, 90, 120, 1);
    ce_draw_invert_mask(&d, sprite, 170, 210);
    ce_draw_invert_frame(&d, ce_rect(250, 220, 60, 80), 2);
}

static void test_draw(CeCardSet *cs)
{
    CeImage *full = ce_image_new(320, 320), *tiled = ce_image_new(320, 320), *probe;
    const CeImage *sprite;
    CeDraw d;
    int x, y, bad = 0;
    ce_cardset_set_size(cs, 71, 96, 1);
    sprite = ce_cardset_card(cs, 3);
    CHECK(sprite && sprite->w == 71 && sprite->h == 96, "sprite");
    scene(full, ce_rect(0, 0, 320, 320), cs, sprite);
    for (y = 0; y < 320; y += 37)
        for (x = 0; x < 320; x += 53)
            scene(tiled, ce_rect(x, y, 53, 37), cs, sprite);
    CHECK(same_image(full, tiled), "tiled render == full render");
    scene(tiled, ce_rect(0, 0, 320, 320), NULL, sprite);      /* no ring cache: a temporary ring */
    CHECK(same_image(full, tiled), "ring without a cache == cached ring");

    /* XP's 1-px bevel at (3, 4): black top-left, green bottom-right, background diagonal at the
     * top-right / bottom-left corners */
    CHECK(full->px[4 * 320 + 3] == DARK && full->px[99 * 320 + 73] == LIGHT, "bevel colours");
    CHECK(full->px[4 * 320 + 73] == GREEN && full->px[99 * 320 + 3] == GREEN, "bevel mitre");
    /* the frame: inverted green (255 - c) on the two edge pixels, untouched inside */
    CHECK(full->px[220 * 320 + 250] == CE_RGB(255, 128, 255) && full->px[221 * 320 + 251] == CE_RGB(255, 128, 255),
          "inverted frame %08x", full->px[220 * 320 + 250]);
    CHECK(full->px[222 * 320 + 252] == GREEN && full->px[260 * 320 + 280] == GREEN, "frame inside untouched");
    CHECK(full->px[299 * 320 + 309] == CE_RGB(255, 128, 255), "frame bottom-right corner");

    /* inverting a frame twice restores the pixels; a frame thicker than half the rect inverts it all */
    probe = ce_image_new(40, 30);
    ce_fill_rect(probe, 0, 0, 40, 30, CE_RGB(10, 20, 30));
    ce_draw_begin(&d, probe, ce_rect(0, 0, 40, 30));
    ce_draw_invert_frame(&d, ce_rect(5, 5, 20, 10), 5);
    CHECK(probe->px[5 * 40 + 5] == CE_RGB(245, 235, 225) && probe->px[10 * 40 + 15] == CE_RGB(245, 235, 225),
          "thick frame: all inverted");
    ce_draw_invert_frame(&d, ce_rect(5, 5, 20, 10), 5);
    for (x = 0, y = 0; y < 30 * 40; y++)
        x += probe->px[y] != CE_RGB(10, 20, 30);
    CHECK(x == 0, "inverted twice: restored (%d px differ)", x);
    ce_draw_invert_frame(&d, ce_rect(30, 20, 30, 30), 1);   /* partly outside the framebuffer */
    CHECK(probe->px[20 * 40 + 39] == CE_RGB(245, 235, 225) && probe->px[29 * 40 + 30] == CE_RGB(245, 235, 225),
          "clipped frame");
    CHECK(!ce_draw_begin(&d, probe, ce_rect(40, 0, 5, 5)) && !ce_draw_begin(&d, NULL, ce_rect(0, 0, 5, 5)),
          "nothing to draw");
    for (y = 0; y < 320 * 320; y++)
        bad += (full->px[y] >> 24) != 255;
    CHECK(bad == 0, "the framebuffer stays opaque (%d)", bad);
    ce_image_free(probe);
    ce_image_free(full);
    ce_image_free(tiled);
}

/* ---- store -------------------------------------------------------------------------------------- */

typedef struct Mem { const char *name[8]; uint32_t v[8]; int n, flushes; } Mem;

static int mem_get(void *ctx, const char *name, uint32_t *v)
{
    Mem *m = ctx;
    int i;
    for (i = 0; i < m->n; i++)
        if (m->name[i] && !strcmp(m->name[i], name)) {
            *v = m->v[i];
            return 1;
        }
    return 0;
}

static void mem_set(void *ctx, const char *name, uint32_t v)
{
    Mem *m = ctx;
    int i;
    for (i = 0; i < m->n; i++)
        if (m->name[i] && !strcmp(m->name[i], name))
            break;
    if (i == m->n && m->n < 8)
        m->n++;
    if (i < 8) {
        m->name[i] = name;
        m->v[i] = v;
    }
}

static void mem_del(void *ctx, const char *name)
{
    Mem *m = ctx;
    int i;
    for (i = 0; i < m->n; i++)
        if (m->name[i] && !strcmp(m->name[i], name))
            m->name[i] = NULL;
}

static void mem_flush(void *ctx) { ((Mem *)ctx)->flushes++; }

static void test_store(void)
{
    Mem m;
    CeStore s = { &m, mem_get, mem_set, mem_del, mem_flush, NULL }, none;
    memset(&m, 0, sizeof m);
    memset(&none, 0, sizeof none);
    CHECK(ce_store_get(&s, "Options", 11) == 11, "missing value: the default");
    ce_store_set(&s, "Options", 0x0B);
    ce_store_set(&s, "Back", 3);
    CHECK(ce_store_get(&s, "Options", 0) == 0x0B && ce_store_get(&s, "Back", 0) == 3, "set / get");
    ce_store_del(&s, "Back");
    CHECK(ce_store_get(&s, "Back", 7) == 7, "deleted");
    ce_store_flush(&s);
    CHECK(m.flushes == 1, "flush");
    ce_store_set(&none, "x", 1);
    ce_store_del(&none, "x");
    ce_store_flush(&none);
    CHECK(ce_store_get(&none, "x", 42) == 42, "no callbacks: defaults, nothing written");
    CHECK(ce_crc32("123456789", 9) == 0xCBF43926u && ce_crc32("", 0) == 0, "CRC-32 check values");
}

/* ---- card set ------------------------------------------------------------------------------------ */

typedef struct Loader {
    FcNativeAssets na;
    void          *back_png;      /* back 0: a synthetic PNG */
    size_t         back_len;
    int            calls[CE_MAX_BACKS], face_calls;
} Loader;

static const void *loader(int id, size_t *len, void *ctx)
{
    Loader *l = ctx;
    if (id >= CE_ASSET_BACK0 && id < CE_ASSET_BACK0 + CE_MAX_BACKS) {
        int i = id - CE_ASSET_BACK0;
        l->calls[i]++;
        if (i == 0) {
            *len = l->back_len;
            return l->back_png;
        }
        if (i == 1)                                    /* back 1: the King of Spades' face */
            return fc_native_asset_loader(CE_ASSET_CARD0 + 51, len, &l->na);
        if (i == 3) {                                  /* back 3: not a PNG */
            static const char junk[] = "not a png";
            *len = sizeof junk;
            return junk;
        }
        *len = 0;
        return NULL;                                   /* back 2: missing */
    }
    if (id >= CE_ASSET_CARD0 && id < CE_ASSET_CARD0 + CE_NCARDS)
        l->face_calls++;
    return fc_native_asset_loader(id, len, &l->na);
}

/* A 400x560 back: blue to the edge, inside a 2-px black outline (as the RevK backs). */
static int make_back(Loader *l, const char *dir)
{
    char path[600];
    CeImage *b = ce_image_new(400, 560);
    int ok;
    ce_fill_rect(b, 0, 0, 400, 560, CE_RGB(0, 0, 0));
    ce_fill_rect(b, 2, 2, 396, 556, CE_RGB(42, 111, 214));
    ce_fill_rect(b, 100, 100, 200, 360, CE_RGB(250, 200, 0));
    snprintf(path, sizeof path, "%s/test_engine_back.png", dir);
    ok = ce_image_write_png(b, path);
    ce_image_free(b);
    if (ok)
        l->back_png = fc_native_read_file(path, &l->back_len);
    remove(path);
    return l->back_png != NULL;
}

static void test_cardset(const char *res, const char *tmp)
{
    Loader l;
    CeCardSet *cs;
    const CeImage *a, *b, *k;
    uint32_t p;
    memset(&l, 0, sizeof l);
    fc_native_assets_init(&l.na, res);
    CHECK(make_back(&l, tmp), "synthetic back PNG");
    CHECK(ce_cardset_new(loader, &l, CE_MAX_BACKS + 1) == NULL && ce_cardset_new(NULL, NULL, 0) == NULL,
          "bad arguments");
    cs = ce_cardset_new(loader, &l, 4);
    CHECK(cs != NULL, "card set");
    if (!cs) {
        fc_native_assets_free(&l.na);
        free(l.back_png);
        return;
    }
    CHECK(l.face_calls == 52 && l.calls[0] == 0 && l.calls[1] == 0, "faces decoded now, backs not (%d)",
          l.face_calls);
    CHECK(ce_cardset_card(cs, 0) == NULL && ce_cardset_back(cs, 0) == NULL, "no size yet: no sprites");
    ce_cardset_set_size(cs, 95, 128, 1);
    CHECK(ce_cardset_card(cs, -1) == NULL && ce_cardset_card(cs, 52) == NULL, "bad card");
    CHECK(ce_cardset_back(cs, -1) == NULL && ce_cardset_back(cs, 4) == NULL, "bad back");

    /* back 1 is the King of Spades' face: through the back path it is exactly the face sprite (the
     * band colour sampled inside a face is white) */
    a = ce_cardset_card(cs, 51);
    b = ce_cardset_back(cs, 1);
    CHECK(a && b && a != b && same_image(a, b), "a face as a back == the face");
    CHECK(ce_cardset_back(cs, 1) == b && l.calls[1] == 1, "back cached, decoded once");

    /* the synthetic back: its colour runs to the frame, the frame is black, the corners transparent */
    k = ce_cardset_back(cs, 0);
    CHECK(k && k->w == 95 && k->h == 128, "back 0 sprite");
    if (k) {
        p = k->px[64 * k->stride + 0];
        CHECK(p == CE_RGB(0, 0, 0), "frame black %08x", p);
        p = k->px[64 * k->stride + 1];
        CHECK(p == CE_RGB(42, 111, 214), "edge keeps the back's colour %08x", p);
        p = k->px[64 * k->stride + 94];
        CHECK(p == CE_RGB(0, 0, 0), "right frame %08x", p);
        CHECK(k->px[0] == 0 && k->px[127 * k->stride + 94] == 0, "transparent corners");
        p = k->px[64 * k->stride + 47];
        CHECK(p == CE_RGB(250, 200, 0), "centre %08x", p);
    }

    /* missing and broken backs: NULL, and not asked for again */
    CHECK(ce_cardset_back(cs, 2) == NULL && ce_cardset_back(cs, 2) == NULL && l.calls[2] == 1, "missing back");
    CHECK(ce_cardset_back(cs, 3) == NULL && ce_cardset_back(cs, 3) == NULL && l.calls[3] == 1, "broken back");

    /* quality: a fast request at the same size keeps the best sprites; a new size rebuilds fast,
     * best quality at it rebuilds again */
    ce_cardset_set_size(cs, 95, 128, 0);
    CHECK(ce_cardset_back(cs, 0) == k && ce_cardset_card(cs, 51) == a, "fast request keeps HQ sprites");
    ce_cardset_set_size(cs, 96, 130, 0);
    b = ce_cardset_back(cs, 0);
    CHECK(b && b->w == 96 && b->h == 130, "fast back at a new size");
    ce_cardset_set_size(cs, 96, 130, 1);
    a = ce_cardset_back(cs, 0);
    CHECK(a && a->w == 96 && a->h == 130 && l.calls[0] == 1, "HQ rebuild from the kept master");

    /* big sprites: the masters are dropped before building (a best rebuild decodes again), the fast
     * sprites come from half-size copies */
    ce_cardset_set_size(cs, 430, 581, 1);
    a = ce_cardset_back(cs, 0);
    CHECK(a && a->w == 430 && l.calls[0] == 2, "big back: master dropped, decoded again (%d)", l.calls[0]);
    ce_cardset_set_size(cs, 428, 578, 0);
    a = ce_cardset_back(cs, 0);
    CHECK(a && a->w == 428 && l.calls[0] == 2, "fast big back from the half-size copy (%d)", l.calls[0]);
    ce_cardset_set_size(cs, 428, 578, 1);
    a = ce_cardset_back(cs, 0);
    CHECK(a && a->w == 428 && l.calls[0] == 3, "best big back: decoded again (%d)", l.calls[0]);
    ce_cardset_set_size(cs, 71, 96, 1);
    a = ce_cardset_back(cs, 0);
    CHECK(a && a->w == 71 && l.calls[0] == 4 && ce_cardset_back(cs, 0) == a, "small again: decoded, kept");
    ce_cardset_set_size(cs, 72, 97, 1);
    CHECK(ce_cardset_back(cs, 0) && l.calls[0] == 4, "master kept for good");

    /* the ring cache */
    a = ce_cardset_bevel(cs, 100, 50, 2.0, 5.0, DARK, LIGHT);
    CHECK(a && ce_cardset_bevel(cs, 100, 50, 2.0, 5.0, DARK, LIGHT) == a, "ring cached");
    CHECK(ce_cardset_bevel(cs, 100, 50, 2.0, 5.0, LIGHT, DARK) != a, "other colours, other ring");
    CHECK(ce_cardset_bevel(NULL, 100, 50, 2.0, 5.0, DARK, LIGHT) == NULL &&
          ce_cardset_bevel(cs, 0, 50, 2.0, 5.0, DARK, LIGHT) == NULL, "no cache / empty ring");

    test_draw(cs);
    ce_cardset_free(cs);
    ce_cardset_free(NULL);
    fc_native_assets_free(&l.na);
    free(l.back_png);
}

int main(int argc, char **argv)
{
    const char *res = argc > 1 ? argv[1] : "res", *tmp = argc > 2 ? argv[2] : "build";
    test_geom();
    test_store();
    test_cardset(res, tmp);
    printf("test_engine: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
