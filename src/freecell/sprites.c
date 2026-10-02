/*
 * FreeCell HD — the board's sprites (see sprites.h): the engine card set for the faces, and the three
 * kings, built lazily per size like the cards.
 *
 * King masters: the small-box kings are at most ~200 px (4K), so a copy reduced to FC_KING_KEEP_PX is
 * kept (a bigger right/left king, which XP never shows, is re-decoded); the smiling king is kept at
 * full size for the big win king. A king master no larger than FC_PIXEL_ART_PX is pixel art and is
 * scaled sharp.
 */
#include "sprites.h"

#include <stdlib.h>
#include <string.h>

#define FC_KING_KEEP_PX       256        /* small king masters kept at most this size */
#define FC_PIXEL_ART_PX       64         /* king masters this small are pixel art: scaled sharp */

struct FcCardSet {
    CeAssetLoader loader;
    void         *ctx;
    CeCardSet    *cards;
    CeImage      *king_master[3];   /* right/left reduced to <= FC_KING_KEEP_PX, smile full size;
                                       NULL: asset missing, a placeholder is drawn */
    int           king_px, big_king_px;
    int           king_q[2];        /* quality of the current king sprites (0 fast, 1 best) */
    CeImage      *king[3][2];
};

static CeImage *load(FcCardSet *cs, int id)
{
    size_t len = 0;
    const void *data = cs->loader(id, &len, cs->ctx);
    return data ? ce_image_decode_png(data, len) : NULL;
}

FcCardSet *fc_cardset_new(CeAssetLoader loader, void *ctx)
{
    return fc_cardset_new_faces(loader, ctx, CE_FACES_NORMAL);
}

FcCardSet *fc_cardset_new_faces(CeAssetLoader loader, void *ctx, int faces)
{
    FcCardSet *cs;
    int i;
    if (!loader)
        return NULL;
    cs = (FcCardSet *)calloc(1, sizeof *cs);
    if (!cs)
        return NULL;
    cs->loader = loader;
    cs->ctx = ctx;
    cs->king_q[0] = cs->king_q[1] = 1;
    /* kings first, while nothing else is resident (a large PNG decode has a sizeable transient) */
    for (i = 0; i < 3; i++) {
        CeImage *k = load(cs, FC_ASSET_KING_RIGHT + i);
        if (k && i != FC_KING_SMILE && (k->w > FC_KING_KEEP_PX || k->h > FC_KING_KEEP_PX)) {
            int w = k->w >= k->h ? FC_KING_KEEP_PX : k->w * FC_KING_KEEP_PX / k->h;
            int h = k->h >= k->w ? FC_KING_KEEP_PX : k->h * FC_KING_KEEP_PX / k->w;
            CeImage *r = ce_image_resample(k, w > 0 ? w : 1, h > 0 ? h : 1, 1);
            ce_image_free(k);
            k = r;
        }
        cs->king_master[i] = k;
    }
    cs->cards = ce_cardset_new_faces(loader, ctx, 0, faces);
    if (!cs->cards) {
        fc_cardset_free(cs);
        return NULL;
    }
    return cs;
}

static void drop_kings(FcCardSet *cs, int big)
{
    int i;
    for (i = 0; i < 3; i++) {
        ce_image_free(cs->king[i][big]);
        cs->king[i][big] = NULL;
    }
}

void fc_cardset_free(FcCardSet *cs)
{
    int i;
    if (!cs)
        return;
    ce_cardset_free(cs->cards);
    drop_kings(cs, 0);
    drop_kings(cs, 1);
    for (i = 0; i < 3; i++)
        ce_image_free(cs->king_master[i]);
    free(cs);
}

void fc_cardset_set_size(FcCardSet *cs, int cw, int ch, int king_px, int big_king_px, int quality)
{
    if (!cs)
        return;
    quality = quality > 0;
    ce_cardset_set_size(cs->cards, cw, ch, quality);
    /* as the cards: a new size or a better quality rebuilds, a fast request keeps what is there */
    if (king_px != cs->king_px || quality > cs->king_q[0]) {
        drop_kings(cs, 0);
        cs->king_q[0] = quality;
    }
    if (big_king_px != cs->big_king_px || quality > cs->king_q[1]) {
        drop_kings(cs, 1);
        cs->king_q[1] = quality;
    }
    cs->king_px = king_px;
    cs->big_king_px = big_king_px;
}

const CeImage *fc_cardset_card(FcCardSet *cs, Card c)
{
    return cs ? ce_cardset_card(cs->cards, c) : NULL;
}

CeCardSet *fc_cardset_cards(FcCardSet *cs)
{
    return cs ? cs->cards : NULL;
}

const CeImage *fc_cardset_bevel(FcCardSet *cs, int w, int h, double t, double rad, uint32_t tl, uint32_t br)
{
    return cs ? ce_cardset_bevel(cs->cards, w, h, t, rad, tl, br) : NULL;
}

/* ---- kings ------------------------------------------------------------------------------------ */

/* A 16x16 pixel-art crown used when a king PNG is missing (the loader returned NULL). */
static CeImage *placeholder_king(int which)
{
    static const char *art[16] = {
        "................",
        "................",
        "..Y....Y....Y...",
        "..YY..YYY..YY...",
        "..YYYYYYYYYYY...",
        "..YRYYYBYYYRY...",
        "..YYYYYYYYYYY...",
        "...KKKKKKKKK....",
        "...KSSSSSSSK....",
        "...KSESSSESK....",
        "...KSSSSSSSK....",
        "...KSSSMSSSK....",
        "...KSSSSSSSK....",
        "....KKKKKKK.....",
        "...BBBBBBBBB....",
        "..BBBBBBBBBBB...",
    };
    CeImage *img = ce_image_new(16, 16);
    int x, y;
    if (!img)
        return NULL;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            /* KingLeft looks left: mirror the eyes by mirroring the whole sprite */
            int sx = which == FC_KING_LEFT ? 15 - x : x;
            uint32_t p = 0;
            switch (art[y][sx]) {
            case 'Y': p = CE_RGB(255, 255, 0); break;
            case 'R': p = CE_RGB(255, 0, 0); break;
            case 'B': p = CE_RGB(0, 0, 255); break;
            case 'K': p = CE_RGB(0, 0, 0); break;
            case 'S': p = CE_RGB(255, 255, 255); break;
            case 'E': p = CE_RGB(0, 0, 0); break;
            case 'M': p = which == FC_KING_SMILE ? CE_RGB(255, 0, 255) : CE_RGB(0, 0, 0); break;
            }
            img->px[y * 16 + x] = p;
        }
    return img;
}

/* Scale a king to size x size. Pixel art (a small master) is enlarged by an integer nearest-
 * neighbour factor first, then area-averaged down: crisp pixels, clean edges. */
static CeImage *scale_king(const CeImage *src, int size, int quality)
{
    CeImage *big, *out;
    int k, x, y;
    if ((src->w >= size && src->h >= size) || src->w > FC_PIXEL_ART_PX || src->h > FC_PIXEL_ART_PX)
        return ce_image_resample(src, size, size, quality);
    k = (size + src->w - 1) / src->w;
    if ((size + src->h - 1) / src->h > k)
        k = (size + src->h - 1) / src->h;
    big = ce_image_new(src->w * k, src->h * k);
    if (!big)
        return NULL;
    for (y = 0; y < big->h; y++)
        for (x = 0; x < big->w; x++)
            big->px[(size_t)y * big->stride + x] = src->px[(size_t)(y / k) * src->stride + x / k];
    out = big->w == size && big->h == size ? big : ce_image_resample(big, size, size, 1);
    if (out != big)
        ce_image_free(big);
    return out;
}

const CeImage *fc_cardset_king(FcCardSet *cs, int which, int big)
{
    int size;
    CeImage *src, *tmp = NULL;
    if (!cs || which < 0 || which > 2)
        return NULL;
    big = big ? 1 : 0;
    if (cs->king[which][big])
        return cs->king[which][big];
    size = big ? cs->big_king_px : cs->king_px;
    if (size <= 0)
        return NULL;
    src = cs->king_master[which];
    if (src && size > src->w && src->w > FC_PIXEL_ART_PX && which != FC_KING_SMILE) {
        CeImage *full = load(cs, FC_ASSET_KING_RIGHT + which);   /* a big right/left king (unused by XP) */
        if (full)
            src = tmp = full;
    }
    if (!src)
        src = tmp = placeholder_king(which);
    if (!src)
        return NULL;
    cs->king[which][big] = scale_king(src, size, cs->king_q[big]);
    ce_image_free(tmp);
    return cs->king[which][big];
}
