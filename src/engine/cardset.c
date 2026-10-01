/*
 * Card engine — card sprites at the current display size (see cardset.h).
 *
 * Masters: 52 card faces 400x560 (docs/card-art.md §2) decoded and premultiplied once; card backs are
 * decoded on first use. Scaled sprites are built lazily per card on first use after a size change:
 * HQ resample (quality 1: ce_image_resample_card, the dark-biased, sharpened box of docs/DESIGN.md
 * "Crispness decisions") or fast bilinear (quality 0), then ce_image_card_finish draws XP's crisp
 * dark frame over the art's own (sub-pixel at these sizes) outline and gives the sprite an exact
 * rounded shape with transparent corners.
 *
 * The rounded shape (an analytic coverage, floating point) is the same for every card of a size, so
 * it is computed once per size (CeCardShape) and applied with integer math; likewise the HQ
 * resampler's LUTs and taps (CeCardFilter).
 *
 * Quality: a fast-quality request at an unchanged card size keeps the sprites it has (a live resize
 * that only moves a border of a height-limited window rebuilds nothing); a new size, or a better
 * quality, rebuilds.
 *
 * Memory: decoded face masters take 46.6 MB. They are kept while the scaled set is small (fast
 * rebuilds); when the sprites themselves get large (> CE_KEEP_MASTERS_BYTES for 52 sprites, i.e.
 * ch > ~400: a client of about 2660 x 1570 or more) each master is dropped right after its sprite is
 * built and re-decoded (about 0.6 ms each natively) for the next best-quality rebuild, which keeps the
 * peak well under ~80 MB at every size. Meanwhile a half-size copy of each master (11.6 MB in all) is
 * kept as the source of the fast-quality sprites until its master is back, so a live resize never
 * decodes a PNG. Backs follow the same policy once decoded.
 *
 * Also cached here: anti-aliased bevel rings (empty cells, frames; see ce_draw_ring in draw.h).
 */
#include "cardset.h"

#include <stdlib.h>
#include <string.h>

#define CE_KEEP_MASTERS_BYTES (24u << 20)
#define CE_FRAME_COLOUR       CE_RGB(0, 0, 0)       /* XP draws a black frame on every card */
#define CE_CARD_WHITE         CE_RGB(255, 255, 255)

#define CE_BEVEL_SLOTS        4
#define NSLOTS                (CE_NCARDS + CE_MAX_BACKS)   /* faces 0..51, then the backs */

typedef struct BevelSlot {
    CeImage *img;
    int      w, h;
    double   t, rad;
    uint32_t tl, br;
} BevelSlot;

struct CeCardSet {
    CeAssetLoader loader;
    void         *ctx;
    int           nbacks;
    CeImage      *master[NSLOTS];
    CeImage      *mip[NSLOTS];      /* half-size masters: made when the masters are dropped, freed
                                       when a master is kept again */
    unsigned char failed[NSLOTS];   /* a back whose asset is missing or broken: not asked for again */
    int           keep_masters;
    int           cw, ch;
    int           q;                /* quality of the current sprites (0 fast, 1 best) */
    CeImage      *sprite[NSLOTS];
    CeCardShape  *shape;            /* the card shape at cw x ch */
    CeCardFilter *filter;           /* the HQ card resampler, master size -> cw x ch */
    BevelSlot     bevel[CE_BEVEL_SLOTS];
    int           bevel_next;
};

static int asset_id(int slot)
{
    return slot < CE_NCARDS ? CE_ASSET_CARD0 + slot : CE_ASSET_BACK0 + (slot - CE_NCARDS);
}

/* Make a master opaque along its edge: the art's own 1.67-px outline (and its AA rim) would otherwise
 * leave a faint grey line just inside the crisp frame drawn after scaling. The band takes the colour
 * 'fill'; the sprite's shape comes from ce_image_card_finish, so the master needs no alpha. */
typedef struct EdgeGeom { double w, h, band, r, ri; } EdgeGeom;

static int master_edge(const EdgeGeom *g, int x, double cy, double dy)
{
    double cx = x + 0.5, dx = cx < g->r ? g->r - cx : (cx > g->w - g->r ? cx - (g->w - g->r) : 0);
    return cx < g->band || cy < g->band || cx > g->w - g->band || cy > g->h - g->band ||
           (dx > 0 && dy > 0 && dx * dx + dy * dy > g->ri * g->ri);
}

static void edge_geom(EdgeGeom *g, const CeImage *m)
{
    g->w = m->w;
    g->h = m->h;
    g->band = 3.5 * m->h / 560.0;                 /* outline 1.67 px + anti-aliasing, master px */
    g->r = 0.0372 * m->h;                         /* outer corner radius (card-art.md §3) */
    g->ri = g->r - g->band;
}

/* premultiplied src-over white: opaque */
static uint32_t over_white(uint32_t p)
{
    uint32_t a = p >> 24, k = 255 - a;
    return p + ((k << 24) | (k << 16) | (k << 8) | k);
}

static void clean_master(CeImage *m, uint32_t fill)
{
    EdgeGeom g;
    int x, y;
    edge_geom(&g, m);
    for (y = 0; y < m->h; y++) {
        uint32_t *p = m->px + (size_t)y * m->stride;
        double cy = y + 0.5, dy = cy < g.r ? g.r - cy : (cy > g.h - g.r ? cy - (g.h - g.r) : 0);
        int l, r;
        /* The edge test only weakens towards the middle of a row (dx falls, then rises), so it holds
         * on a prefix and a suffix: evaluate it there only, a few pixels per row instead of all. */
        for (l = 0; l < m->w && master_edge(&g, l, cy, dy); l++)
            p[l] = fill;
        for (r = m->w; r > l && master_edge(&g, r - 1, cy, dy); r--)
            p[r - 1] = fill;
        for (x = l; x < r; x++)
            if (p[x] < 0xff000000u)
                p[x] = over_white(p[x]);
    }
}

/* A back's band colour: the art just inside the band, half-way down the left edge, made opaque. */
static uint32_t back_fill(const CeImage *m)
{
    EdgeGeom g;
    int x;
    edge_geom(&g, m);
    x = (int)(g.band + 1.5);
    if (x >= m->w)
        x = m->w - 1;
    return over_white(m->px[(size_t)(m->h / 2) * m->stride + x]);
}

static CeImage *load_master(CeCardSet *cs, int slot)
{
    size_t len = 0;
    const void *data = cs->loader(asset_id(slot), &len, cs->ctx);
    CeImage *m = data ? ce_image_decode_png(data, len) : NULL;
    if (m && (m->w < 8 || m->h < 8)) {
        ce_image_free(m);
        m = NULL;
    }
    if (m)
        clean_master(m, slot < CE_NCARDS ? CE_CARD_WHITE : back_fill(m));
    return m;
}

CeCardSet *ce_cardset_new(CeAssetLoader loader, void *ctx, int nbacks)
{
    CeCardSet *cs;
    int i;
    if (!loader || nbacks < 0 || nbacks > CE_MAX_BACKS)
        return NULL;
    cs = (CeCardSet *)calloc(1, sizeof *cs);
    if (!cs)
        return NULL;
    cs->loader = loader;
    cs->ctx = ctx;
    cs->nbacks = nbacks;
    cs->keep_masters = 1;
    cs->q = 1;
    for (i = 0; i < CE_NCARDS; i++) {
        cs->master[i] = load_master(cs, i);
        if (!cs->master[i]) {
            ce_cardset_free(cs);
            return NULL;
        }
    }
    return cs;
}

static void drop_sprites(CeCardSet *cs)
{
    int i;
    for (i = 0; i < NSLOTS; i++) {
        ce_image_free(cs->sprite[i]);
        cs->sprite[i] = NULL;
    }
}

void ce_cardset_free(CeCardSet *cs)
{
    int i;
    if (!cs)
        return;
    drop_sprites(cs);
    for (i = 0; i < NSLOTS; i++) {
        ce_image_free(cs->master[i]);
        ce_image_free(cs->mip[i]);
    }
    for (i = 0; i < CE_BEVEL_SLOTS; i++)
        ce_image_free(cs->bevel[i].img);
    ce_card_shape_free(cs->shape);
    ce_card_filter_free(cs->filter);
    free(cs);
}

static CeImage *half_size(const CeImage *m)
{
    return ce_image_resample(m, (m->w + 1) / 2, (m->h + 1) / 2, 1);   /* 2x2 box average */
}

void ce_cardset_set_size(CeCardSet *cs, int cw, int ch, int quality)
{
    int i;
    if (!cs)
        return;
    quality = quality > 0;
    /* a new size, or a better quality than the sprites have, rebuilds; a fast-quality request at the
     * same size keeps what is there (and keeps building at that quality) */
    if (cw != cs->cw || ch != cs->ch || quality > cs->q) {
        drop_sprites(cs);
        cs->q = quality;
    }
    cs->cw = cw;
    cs->ch = ch;
    cs->keep_masters = (size_t)CE_NCARDS * 4 * (size_t)(cw > 0 ? cw : 0) * (size_t)(ch > 0 ? ch : 0) <=
                       CE_KEEP_MASTERS_BYTES;
    if (!cs->keep_masters)
        for (i = 0; i < NSLOTS; i++)
            if (cs->master[i]) {
                /* big sprites: release the masters before building them (re-decoded per card for
                 * the best-quality sprites); a half-size copy stays for the fast ones */
                if (!cs->mip[i])
                    cs->mip[i] = half_size(cs->master[i]);
                ce_image_free(cs->master[i]);
                cs->master[i] = NULL;
            }
}

/* HQ sprite from an (opaque, cleaned) master: the card resampler, its per-size tables kept. */
static CeImage *resample_hq(CeCardSet *cs, const CeImage *m)
{
    if (!ce_card_filter_is(cs->filter, m->w, m->h, cs->cw, cs->ch)) {
        ce_card_filter_free(cs->filter);
        cs->filter = ce_card_filter_new(m->w, m->h, cs->cw, cs->ch);
    }
    if (!cs->filter)
        return ce_image_resample(m, cs->cw, cs->ch, 1);   /* out of memory: the plain box */
    return ce_image_resample_card_filter(m, cs->filter);
}

/* Frame thickness: XP's crisp 1-px frame up to ch 300, then growing with the card so it always
 * covers the art's own outline (ch/336 px, card-art.md §3). */
static double frame_px(int ch)
{
    return ch <= 300 ? 1.0 : ch / 300.0;
}

static void card_finish(CeCardSet *cs, CeImage *s)
{
    double r = 0.0372 * cs->ch, t = frame_px(cs->ch);
    if (!ce_card_shape_is(cs->shape, cs->cw, cs->ch, r, t)) {
        ce_card_shape_free(cs->shape);
        cs->shape = ce_card_shape_new(cs->cw, cs->ch, r, t);
    }
    if (cs->shape)
        ce_image_card_finish_shape(s, cs->shape, CE_FRAME_COLOUR, CE_CARD_WHITE);
    else
        ce_image_card_finish(s, r, t, CE_FRAME_COLOUR, CE_CARD_WHITE);   /* out of memory */
}

static const CeImage *sprite(CeCardSet *cs, int slot)
{
    CeImage *m, *s;
    if (cs->cw <= 0 || cs->ch <= 0)
        return NULL;
    if (cs->sprite[slot])
        return cs->sprite[slot];
    if (cs->failed[slot])
        return NULL;
    if (!cs->master[slot] && cs->q == 0 && cs->mip[slot]) {
        s = ce_image_resample(cs->mip[slot], cs->cw, cs->ch, 0);   /* live resize: no PNG decode */
    } else {
        m = cs->master[slot] ? cs->master[slot] : load_master(cs, slot);
        if (!m) {
            /* a face is retried (its asset decoded at start-up); a back is given up */
            if (slot >= CE_NCARDS)
                cs->failed[slot] = 1;
            return NULL;
        }
        s = cs->q ? resample_hq(cs, m) : ce_image_resample(m, cs->cw, cs->ch, 0);
        if (cs->keep_masters) {
            cs->master[slot] = m;
            ce_image_free(cs->mip[slot]);                         /* the master is back for good */
            cs->mip[slot] = NULL;
        } else {
            if (!cs->mip[slot])
                cs->mip[slot] = half_size(m);
            ce_image_free(m);
            cs->master[slot] = NULL;
        }
    }
    if (s)
        card_finish(cs, s);
    cs->sprite[slot] = s;
    return s;
}

const CeImage *ce_cardset_card(CeCardSet *cs, int c)
{
    if (!cs || c < 0 || c >= CE_NCARDS)
        return NULL;
    return sprite(cs, c);
}

const CeImage *ce_cardset_back(CeCardSet *cs, int i)
{
    if (!cs || i < 0 || i >= cs->nbacks)
        return NULL;
    return sprite(cs, CE_NCARDS + i);
}

const CeImage *ce_cardset_bevel(CeCardSet *cs, int w, int h, double t, double rad, uint32_t tl, uint32_t br)
{
    BevelSlot *b;
    int i;
    if (!cs || w <= 0 || h <= 0)
        return NULL;
    for (i = 0; i < CE_BEVEL_SLOTS; i++) {
        b = &cs->bevel[i];
        if (b->img && b->w == w && b->h == h && b->t == t && b->rad == rad && b->tl == tl && b->br == br)
            return b->img;
    }
    b = &cs->bevel[cs->bevel_next];
    cs->bevel_next = (cs->bevel_next + 1) % CE_BEVEL_SLOTS;
    ce_image_free(b->img);
    b->img = ce_bevel_ring_new(w, h, t, rad, tl, br);
    b->w = w;
    b->h = h;
    b->t = t;
    b->rad = rad;
    b->tl = tl;
    b->br = br;
    return b->img;
}
