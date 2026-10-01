/*
 * FreeCell HD — card and king sprites at the current display size (see cardset.h).
 *
 * Masters: 52 card faces 400x560 (docs/card-art.md §2) decoded and premultiplied once, plus the
 * three king PNGs. Scaled sprites are built lazily per card on first use after a size change:
 * HQ resample (quality 1: fc_image_resample_card, the dark-biased, sharpened box of docs/DESIGN.md
 * "Crispness decisions") or fast bilinear (quality 0), then fc_image_card_finish draws XP's crisp
 * dark frame over the art's own (sub-pixel at these sizes) outline and gives the sprite an exact
 * rounded shape with transparent corners.
 *
 * The rounded shape (an analytic coverage, floating point) is the same for every card of a size, so
 * it is computed once per size (FcCardShape) and applied with integer math; likewise the HQ
 * resampler's LUTs and taps (FcCardFilter).
 *
 * Quality: a fast-quality request at an unchanged card size keeps the sprites it has (a live resize
 * that only moves a border of a height-limited window rebuilds nothing); a new size, or a better
 * quality, rebuilds.
 *
 * Memory: decoded masters take 46.6 MB. They are kept while the scaled set is small (fast
 * rebuilds); when the sprites themselves get large (> FC_KEEP_MASTERS_BYTES, i.e. ch > ~400: a
 * client of about 2660 x 1570 or more) each master is dropped right after its sprite is built and
 * re-decoded (about 0.6 ms each natively) for the next best-quality rebuild, which keeps the peak well
 * under ~80 MB at every size. Meanwhile a half-size copy of each master (11.6 MB in all) is kept as
 * the source of the fast-quality sprites until its master is back, so a live resize never decodes a
 * PNG.
 *
 * Also cached here: the anti-aliased bevel rings of the empty cells and the king frame (render.c).
 */
#include "cardset.h"

#include <stdlib.h>
#include <string.h>

#define FC_KEEP_MASTERS_BYTES (24u << 20)
#define FC_KING_KEEP_PX       256        /* small king masters kept at most this size */
#define FC_PIXEL_ART_PX       64         /* king masters this small are pixel art: scaled sharp */
#define FC_FRAME_COLOUR       FC_RGB(0, 0, 0)       /* XP draws a black frame on every card (layout.md §3) */
#define FC_CARD_WHITE         FC_RGB(255, 255, 255)

#define FC_BEVEL_SLOTS        4

typedef struct BevelSlot {
    FcImage *img;
    int      w, h;
    double   t, rad;
    uint32_t tl, br;
} BevelSlot;

struct FcCardSet {
    FcAssetLoader loader;
    void         *ctx;
    FcImage      *master[52];
    FcImage      *mip[52];          /* half-size masters: made when the masters are dropped, freed
                                       when a master is kept again */
    FcImage      *king_master[3];   /* right/left reduced to <= FC_KING_KEEP_PX, smile full size;
                                       NULL: asset missing, a placeholder is drawn */
    int           keep_masters;
    int           cw, ch, king_px, big_king_px;
    int           card_q, king_q[2]; /* quality of the current sprites (0 fast, 1 best) */
    FcImage      *card[52];
    FcImage      *king[3][2];
    FcCardShape  *shape;            /* the card shape at cw x ch */
    FcCardFilter *filter;           /* the HQ card resampler, master size -> cw x ch */
    BevelSlot     bevel[FC_BEVEL_SLOTS];
    int           bevel_next;
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
typedef struct EdgeGeom { double w, h, band, r, ri; } EdgeGeom;

static int master_edge(const EdgeGeom *g, int x, double cy, double dy)
{
    double cx = x + 0.5, dx = cx < g->r ? g->r - cx : (cx > g->w - g->r ? cx - (g->w - g->r) : 0);
    return cx < g->band || cy < g->band || cx > g->w - g->band || cy > g->h - g->band ||
           (dx > 0 && dy > 0 && dx * dx + dy * dy > g->ri * g->ri);
}

static void clean_master(FcImage *m)
{
    EdgeGeom g;
    int x, y;
    g.w = m->w;
    g.h = m->h;
    g.band = 3.5 * m->h / 560.0;                  /* outline 1.67 px + anti-aliasing, master px */
    g.r = 0.0372 * m->h;                          /* outer corner radius (card-art.md §3) */
    g.ri = g.r - g.band;
    for (y = 0; y < m->h; y++) {
        uint32_t *p = m->px + (size_t)y * m->stride;
        double cy = y + 0.5, dy = cy < g.r ? g.r - cy : (cy > g.h - g.r ? cy - (g.h - g.r) : 0);
        int l, r;
        /* The edge test only weakens towards the middle of a row (dx falls, then rises), so it holds
         * on a prefix and a suffix: evaluate it there only, a few pixels per row instead of all. */
        for (l = 0; l < m->w && master_edge(&g, l, cy, dy); l++)
            p[l] = FC_CARD_WHITE;
        for (r = m->w; r > l && master_edge(&g, r - 1, cy, dy); r--)
            p[r - 1] = FC_CARD_WHITE;
        for (x = l; x < r; x++)
            if (p[x] < 0xff000000u) {
                /* premultiplied src-over white */
                uint32_t a = p[x] >> 24, k = 255 - a;
                p[x] += (k << 24) | (k << 16) | (k << 8) | k;
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
    cs->card_q = cs->king_q[0] = cs->king_q[1] = 1;
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
    for (i = 0; i < 52; i++) {
        fc_image_free(cs->master[i]);
        fc_image_free(cs->mip[i]);
    }
    for (i = 0; i < 3; i++)
        fc_image_free(cs->king_master[i]);
    for (i = 0; i < FC_BEVEL_SLOTS; i++)
        fc_image_free(cs->bevel[i].img);
    fc_card_shape_free(cs->shape);
    fc_card_filter_free(cs->filter);
    free(cs);
}

static FcImage *half_size(const FcImage *m)
{
    return fc_image_resample(m, (m->w + 1) / 2, (m->h + 1) / 2, 1);   /* 2x2 box average */
}

void fc_cardset_set_size(FcCardSet *cs, int cw, int ch, int king_px, int big_king_px, int quality)
{
    int i;
    if (!cs)
        return;
    quality = quality > 0;
    /* a new size, or a better quality than the sprites have, rebuilds; a fast-quality request at the
     * same size keeps what is there (and keeps building at that quality) */
    if (cw != cs->cw || ch != cs->ch || quality > cs->card_q) {
        drop_cards(cs);
        cs->card_q = quality;
    }
    if (king_px != cs->king_px || quality > cs->king_q[0]) {
        drop_kings(cs, 0);
        cs->king_q[0] = quality;
    }
    if (big_king_px != cs->big_king_px || quality > cs->king_q[1]) {
        drop_kings(cs, 1);
        cs->king_q[1] = quality;
    }
    cs->cw = cw;
    cs->ch = ch;
    cs->king_px = king_px;
    cs->big_king_px = big_king_px;
    cs->keep_masters = (size_t)52 * 4 * (size_t)(cw > 0 ? cw : 0) * (size_t)(ch > 0 ? ch : 0) <=
                       FC_KEEP_MASTERS_BYTES;
    if (!cs->keep_masters)
        for (i = 0; i < 52; i++)
            if (cs->master[i]) {
                /* big sprites: release the masters before building them (re-decoded per card for
                 * the best-quality sprites); a half-size copy stays for the fast ones */
                if (!cs->mip[i])
                    cs->mip[i] = half_size(cs->master[i]);
                fc_image_free(cs->master[i]);
                cs->master[i] = NULL;
            }
}

/* HQ sprite from an (opaque, cleaned) master: the card resampler, its per-size tables kept. */
static FcImage *resample_hq(FcCardSet *cs, const FcImage *m)
{
    if (!fc_card_filter_is(cs->filter, m->w, m->h, cs->cw, cs->ch)) {
        fc_card_filter_free(cs->filter);
        cs->filter = fc_card_filter_new(m->w, m->h, cs->cw, cs->ch);
    }
    if (!cs->filter)
        return fc_image_resample(m, cs->cw, cs->ch, 1);   /* out of memory: the plain box */
    return fc_image_resample_card_filter(m, cs->filter);
}

/* Frame thickness: XP's crisp 1-px frame up to ch 300, then growing with the card so it always
 * covers the art's own outline (ch/336 px, card-art.md §3). */
static double frame_px(int ch)
{
    return ch <= 300 ? 1.0 : ch / 300.0;
}

static void card_finish(FcCardSet *cs, FcImage *s)
{
    double r = 0.0372 * cs->ch, t = frame_px(cs->ch);
    if (!fc_card_shape_is(cs->shape, cs->cw, cs->ch, r, t)) {
        fc_card_shape_free(cs->shape);
        cs->shape = fc_card_shape_new(cs->cw, cs->ch, r, t);
    }
    if (cs->shape)
        fc_image_card_finish_shape(s, cs->shape, FC_FRAME_COLOUR, FC_CARD_WHITE);
    else
        fc_image_card_finish(s, r, t, FC_FRAME_COLOUR, FC_CARD_WHITE);   /* out of memory */
}

const FcImage *fc_cardset_card(FcCardSet *cs, Card c)
{
    FcImage *m, *s;
    if (!cs || c < 0 || c >= 52 || cs->cw <= 0 || cs->ch <= 0)
        return NULL;
    if (cs->card[c])
        return cs->card[c];
    if (!cs->master[c] && cs->card_q == 0 && cs->mip[c]) {
        s = fc_image_resample(cs->mip[c], cs->cw, cs->ch, 0);   /* live resize: no PNG decode */
    } else {
        m = cs->master[c] ? cs->master[c] : load_card(cs, c);
        if (!m)
            return NULL;
        s = cs->card_q ? resample_hq(cs, m) : fc_image_resample(m, cs->cw, cs->ch, 0);
        if (cs->keep_masters) {
            cs->master[c] = m;
            fc_image_free(cs->mip[c]);                         /* the master is back for good */
            cs->mip[c] = NULL;
        } else {
            if (!cs->mip[c])
                cs->mip[c] = half_size(m);
            fc_image_free(m);
            cs->master[c] = NULL;
        }
    }
    if (s)
        card_finish(cs, s);
    cs->card[c] = s;
    return s;
}

const FcImage *fc_cardset_bevel(FcCardSet *cs, int w, int h, double t, double rad, uint32_t tl, uint32_t br)
{
    BevelSlot *b;
    int i;
    if (!cs || w <= 0 || h <= 0)
        return NULL;
    for (i = 0; i < FC_BEVEL_SLOTS; i++) {
        b = &cs->bevel[i];
        if (b->img && b->w == w && b->h == h && b->t == t && b->rad == rad && b->tl == tl && b->br == br)
            return b->img;
    }
    b = &cs->bevel[cs->bevel_next];
    cs->bevel_next = (cs->bevel_next + 1) % FC_BEVEL_SLOTS;
    fc_image_free(b->img);
    b->img = fc_bevel_ring_new(w, h, t, rad, tl, br);
    b->w = w;
    b->h = h;
    b->t = t;
    b->rad = rad;
    b->tl = tl;
    b->br = br;
    return b->img;
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
    cs->king[which][big] = scale_king(src, size, cs->king_q[big]);
    fc_image_free(tmp);
    return cs->king[which][big];
}
