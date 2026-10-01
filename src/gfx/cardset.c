/*
 * FreeCell HD — card and king sprites at the current display size (see cardset.h).
 *
 * Masters: 52 card faces 400x560 (docs/card-art.md §2) decoded and premultiplied once, plus the
 * three king PNGs. Scaled sprites are built lazily per card on first use after a size change:
 * HQ resample (quality 1) or fast bilinear (quality 0), then fc_image_card_finish draws XP's crisp
 * dark frame over the art's own (sub-pixel at these sizes) outline and gives the sprite an exact
 * rounded shape with transparent corners.
 *
 * Memory: decoded masters take 46.6 MB. They are kept while the scaled set is small (fast
 * rebuilds); when the sprites themselves get large (> FC_KEEP_MASTERS_BYTES, i.e. ch > ~380, a 4K
 * screen) each master is dropped right after its sprite is built and re-decoded (about 0.5 ms each
 * natively) on the next size change, which keeps the peak well under ~80 MB at every size.
 */
#include "cardset.h"

#include <stdlib.h>
#include <string.h>

#define FC_KEEP_MASTERS_BYTES (24u << 20)
#define FC_KING_KEEP_PX       256        /* small king masters kept at most this size */
#define FC_PIXEL_ART_PX       64         /* king masters this small are pixel art: scaled sharp */
#define FC_FRAME_COLOUR       FC_RGB(0, 0, 0)       /* XP draws a black frame on every card (layout.md §3) */
#define FC_CARD_WHITE         FC_RGB(255, 255, 255)

struct FcCardSet {
    FcAssetLoader loader;
    void         *ctx;
    FcImage      *master[52];
    FcImage      *king_master[3];   /* right/left reduced to <= FC_KING_KEEP_PX, smile full size;
                                       NULL: asset missing, a placeholder is drawn */
    int           keep_masters;
    int           cw, ch, king_px, big_king_px, quality;
    FcImage      *card[52];
    FcImage      *king[3][2];
};

static FcImage *load(FcCardSet *cs, int id)
{
    size_t len = 0;
    const void *data = cs->loader(id, &len, cs->ctx);
    return data ? fc_image_decode_png(data, len) : NULL;
}

/* Make a card master opaque white along its edge: the art's own 1.67-px outline (and its AA rim)
 * would otherwise leave a faint grey line just inside the crisp frame drawn after scaling. The
 * sprite's shape comes from fc_image_card_finish, so the master needs no alpha. */
static void clean_master(FcImage *m)
{
    const double band = 3.5 * m->h / 560.0;       /* outline 1.67 px + anti-aliasing, master px */
    const double r = 0.0372 * m->h;               /* outer corner radius (card-art.md §3) */
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
                p[x] = FC_CARD_WHITE;
            else if (p[x] < 0xff000000u) {
                /* premultiplied src-over white */
                uint32_t a = p[x] >> 24, k = 255 - a;
                p[x] += (k << 24) | (k << 16) | (k << 8) | k;
            }
        }
    }
}

static FcImage *load_card(FcCardSet *cs, Card c)
{
    FcImage *m = load(cs, FC_ASSET_CARD0 + c);
    if (m && (m->w < 8 || m->h < 8)) {
        fc_image_free(m);
        m = NULL;
    }
    if (m)
        clean_master(m);
    return m;
}

FcCardSet *fc_cardset_new(FcAssetLoader loader, void *ctx)
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
    cs->keep_masters = 1;
    cs->quality = 1;
    /* kings first, while nothing else is resident (a large PNG decode has a sizeable transient).
     * The small-box kings are at most ~200 px (4K), so a reduced copy is kept; the smiling king
     * is kept at full size for the big win king. */
    for (i = 0; i < 3; i++) {
        FcImage *k = load(cs, FC_ASSET_KING_RIGHT + i);
        if (k && i != FC_KING_SMILE && (k->w > FC_KING_KEEP_PX || k->h > FC_KING_KEEP_PX)) {
            int w = k->w >= k->h ? FC_KING_KEEP_PX : k->w * FC_KING_KEEP_PX / k->h;
            int h = k->h >= k->w ? FC_KING_KEEP_PX : k->h * FC_KING_KEEP_PX / k->w;
            FcImage *r = fc_image_resample(k, w > 0 ? w : 1, h > 0 ? h : 1, 1);
            fc_image_free(k);
            k = r;
        }
        cs->king_master[i] = k;
    }
    for (i = 0; i < 52; i++) {
        cs->master[i] = load_card(cs, i);
        if (!cs->master[i]) {
            fc_cardset_free(cs);
            return NULL;
        }
    }
    return cs;
}

static void drop_cards(FcCardSet *cs)
{
    int i;
    for (i = 0; i < 52; i++) {
        fc_image_free(cs->card[i]);
        cs->card[i] = NULL;
    }
}

static void drop_kings(FcCardSet *cs, int big)
{
    int i;
    for (i = 0; i < 3; i++) {
        fc_image_free(cs->king[i][big]);
        cs->king[i][big] = NULL;
    }
}

void fc_cardset_free(FcCardSet *cs)
{
    int i;
    if (!cs)
        return;
    drop_cards(cs);
    drop_kings(cs, 0);
    drop_kings(cs, 1);
    for (i = 0; i < 52; i++)
        fc_image_free(cs->master[i]);
    for (i = 0; i < 3; i++)
        fc_image_free(cs->king_master[i]);
    free(cs);
}

void fc_cardset_set_size(FcCardSet *cs, int cw, int ch, int king_px, int big_king_px, int quality)
{
    if (!cs)
        return;
    quality = quality > 0;
    if (cw != cs->cw || ch != cs->ch || quality != cs->quality)
        drop_cards(cs);
    if (king_px != cs->king_px || quality != cs->quality)
        drop_kings(cs, 0);
    if (big_king_px != cs->big_king_px || quality != cs->quality)
        drop_kings(cs, 1);
    cs->cw = cw;
    cs->ch = ch;
    cs->king_px = king_px;
    cs->big_king_px = big_king_px;
    cs->quality = quality;
    cs->keep_masters = (size_t)52 * 4 * (size_t)(cw > 0 ? cw : 0) * (size_t)(ch > 0 ? ch : 0) <=
                       FC_KEEP_MASTERS_BYTES;
    if (!cs->keep_masters) {
        /* big sprites: release the masters before building them; re-decoded per card on demand */
        int i;
        for (i = 0; i < 52; i++) {
            fc_image_free(cs->master[i]);
            cs->master[i] = NULL;
        }
    }
}

/* Frame thickness: XP's crisp 1-px frame up to ch 300, then growing with the card so it always
 * covers the art's own outline (ch/336 px, card-art.md §3). */
static double frame_px(int ch)
{
    return ch <= 300 ? 1.0 : ch / 300.0;
}

const FcImage *fc_cardset_card(FcCardSet *cs, Card c)
{
    FcImage *m, *s;
    if (!cs || c < 0 || c >= 52 || cs->cw <= 0 || cs->ch <= 0)
        return NULL;
    if (cs->card[c])
        return cs->card[c];
    m = cs->master[c] ? cs->master[c] : load_card(cs, c);
    if (!m)
        return NULL;
    s = fc_image_resample(m, cs->cw, cs->ch, cs->quality);
    if (s)
        fc_image_card_finish(s, 0.0372 * cs->ch, frame_px(cs->ch), FC_FRAME_COLOUR, FC_CARD_WHITE);
    if (cs->keep_masters)
        cs->master[c] = m;
    else {
        fc_image_free(m);
        cs->master[c] = NULL;
    }
    cs->card[c] = s;
    return s;
}

/* ---- kings ------------------------------------------------------------------------------------ */

/* A 16x16 pixel-art crown used when a king PNG is missing (the loader returned NULL). */
static FcImage *placeholder_king(int which)
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
    FcImage *img = fc_image_new(16, 16);
    int x, y;
    if (!img)
        return NULL;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            /* KingLeft looks left: mirror the eyes by mirroring the whole sprite */
            int sx = which == FC_KING_LEFT ? 15 - x : x;
            uint32_t p = 0;
            switch (art[y][sx]) {
            case 'Y': p = FC_RGB(255, 255, 0); break;
            case 'R': p = FC_RGB(255, 0, 0); break;
            case 'B': p = FC_RGB(0, 0, 255); break;
            case 'K': p = FC_RGB(0, 0, 0); break;
            case 'S': p = FC_RGB(255, 255, 255); break;
            case 'E': p = FC_RGB(0, 0, 0); break;
            case 'M': p = which == FC_KING_SMILE ? FC_RGB(255, 0, 255) : FC_RGB(0, 0, 0); break;
            }
            img->px[y * 16 + x] = p;
        }
    return img;
}

/* Scale a king to size x size. Pixel art (a small master) is enlarged by an integer nearest-
 * neighbour factor first, then area-averaged down: crisp pixels, clean edges. */
static FcImage *scale_king(const FcImage *src, int size, int quality)
{
    FcImage *big, *out;
    int k, x, y;
    if ((src->w >= size && src->h >= size) || src->w > FC_PIXEL_ART_PX || src->h > FC_PIXEL_ART_PX)
        return fc_image_resample(src, size, size, quality);
    k = (size + src->w - 1) / src->w;
    if ((size + src->h - 1) / src->h > k)
        k = (size + src->h - 1) / src->h;
    big = fc_image_new(src->w * k, src->h * k);
    if (!big)
        return NULL;
    for (y = 0; y < big->h; y++)
        for (x = 0; x < big->w; x++)
            big->px[(size_t)y * big->stride + x] = src->px[(size_t)(y / k) * src->stride + x / k];
    out = big->w == size && big->h == size ? big : fc_image_resample(big, size, size, 1);
    if (out != big)
        fc_image_free(big);
    return out;
}

const FcImage *fc_cardset_king(FcCardSet *cs, int which, int big)
{
    int size;
    FcImage *src, *tmp = NULL;
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
        FcImage *full = load(cs, FC_ASSET_KING_RIGHT + which);   /* a big right/left king (unused by XP) */
        if (full)
            src = tmp = full;
    }
    if (!src)
        src = tmp = placeholder_king(which);
    if (!src)
        return NULL;
    cs->king[which][big] = scale_king(src, size, cs->quality);
    fc_image_free(tmp);
    return cs->king[which][big];
}
