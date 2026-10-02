/*
 * Card engine — clipped drawing into a framebuffer (see draw.h).
 *
 * Every primitive works in board coordinates and draws into the clip rect's view shifted by its
 * origin, with integer coordinates only, so the pixels of a clipped render equal those of a full one.
 */
#include "draw.h"

#include <stddef.h>

int ce_draw_begin(CeDraw *d, CeImage *fb, CeRect r)
{
    if (!fb || !ce_rect_clip(&r, fb->w, fb->h))
        return 0;
    d->img = ce_image_wrap(r.w, r.h, fb->stride, fb->px + (size_t)r.y * fb->stride + r.x);
    d->ox = r.x;
    d->oy = r.y;
    d->clip = r;
    return 1;
}

int ce_draw_visible(const CeDraw *d, int x, int y, int w, int h)
{
    return ce_rect_overlaps(&d->clip, x, y, w, h);
}

void ce_draw_fill(CeDraw *d, int x, int y, int w, int h, uint32_t argb)
{
    if (w > 0 && h > 0)
        ce_fill_rect(&d->img, x - d->ox, y - d->oy, w, h, argb);
}

void ce_draw_bevel(CeDraw *d, CeRect r, int b, uint32_t tl, uint32_t br)
{
    int i;
    if (!ce_rect_overlaps(&d->clip, r.x, r.y, r.w, r.h))
        return;
    for (i = 0; i < b && 2 * i + 1 < r.w && 2 * i + 1 < r.h; i++) {
        ce_draw_fill(d, r.x + i, r.y + i, 1, r.h - 1 - 2 * i, tl);                 /* left   */
        ce_draw_fill(d, r.x + i, r.y + i, r.w - 1 - 2 * i, 1, tl);                 /* top    */
        ce_draw_fill(d, r.x + r.w - 1 - i, r.y + 1 + i, 1, r.h - 1 - 2 * i, br);   /* right  */
        ce_draw_fill(d, r.x + 1 + i, r.y + r.h - 1 - i, r.w - 1 - 2 * i, 1, br);   /* bottom */
    }
}

/* premultiplied src-over, rounded */
static uint32_t blend(uint32_t s, uint32_t dst)
{
    uint32_t ia = 255 - (s >> 24), out = 0;
    int sh;
    for (sh = 0; sh < 32; sh += 8)
        out |= (((s >> sh) & 255) + (((dst >> sh) & 255) * ia + 127) / 255) << sh;
    return out;
}

void ce_draw_ring(CeDraw *d, CeCardSet *cache, CeRect r, double t, double rad, uint32_t tl, uint32_t br)
{
    int x0, y0, x1, y1, px, py, band = (int)(t + rad) + 2;
    const CeImage *ring;
    CeImage *tmp = NULL;
    if (!ce_rect_overlaps(&d->clip, r.x, r.y, r.w, r.h))
        return;
    ring = ce_cardset_bevel(cache, r.w, r.h, t, rad, tl, br);
    if (!ring)
        ring = tmp = ce_bevel_ring_new(r.w, r.h, t, rad, tl, br);   /* no card set (yet) */
    if (!ring)
        return;
    x0 = r.x > d->clip.x ? r.x : d->clip.x;
    y0 = r.y > d->clip.y ? r.y : d->clip.y;
    x1 = r.x + r.w < d->clip.x + d->clip.w ? r.x + r.w : d->clip.x + d->clip.w;
    y1 = r.y + r.h < d->clip.y + d->clip.h ? r.y + r.h : d->clip.y + d->clip.h;
    for (py = y0; py < y1; py++) {
        uint32_t *dp = d->img.px + (size_t)(py - d->oy) * d->img.stride - d->ox;
        const uint32_t *s = ring->px + (size_t)(py - r.y) * ring->stride;
        int edge_row = py < r.y + band || py >= r.y + r.h - band;
        for (px = x0; px < x1; px++) {
            uint32_t p;
            if (!edge_row && px >= r.x + band && px < r.x + r.w - band) {
                px = r.x + r.w - band - 1;   /* skip the inside of the ring */
                continue;
            }
            p = s[px - r.x];
            if (p)
                dp[px] = p >= 0xff000000u ? p : blend(p, dp[px]);
        }
    }
    ce_image_free(tmp);
}

void ce_draw_sprite(CeDraw *d, const CeImage *s, int x, int y, int inverted)
{
    if (!s || !ce_rect_overlaps(&d->clip, x, y, s->w, s->h))
        return;
    if (inverted)
        ce_blit_inverted(&d->img, s, x - d->ox, y - d->oy);
    else
        ce_blit(&d->img, s, x - d->ox, y - d->oy);
}

void ce_draw_invert_mask(CeDraw *d, const CeImage *mask, int x, int y)
{
    if (mask && ce_rect_overlaps(&d->clip, x, y, mask->w, mask->h))
        ce_invert_masked(&d->img, mask, x - d->ox, y - d->oy);
}

void ce_draw_invert_mask_level(CeDraw *d, const CeImage *mask, int x, int y, int level)
{
    if (mask && ce_rect_overlaps(&d->clip, x, y, mask->w, mask->h))
        ce_invert_masked_level(&d->img, mask, x - d->ox, y - d->oy, level);
}

static void invert_rect(CeDraw *d, int x, int y, int w, int h)
{
    CeRect r = ce_rect(x - d->ox, y - d->oy, w, h);
    int i, j;
    if (!ce_rect_clip(&r, d->img.w, d->img.h))
        return;
    for (j = 0; j < r.h; j++) {
        uint32_t *p = d->img.px + (size_t)(r.y + j) * d->img.stride + r.x;
        for (i = 0; i < r.w; i++) {
            uint32_t a = p[i] >> 24;
            p[i] = (a << 24) | ((a - ((p[i] >> 16) & 255)) << 16) | ((a - ((p[i] >> 8) & 255)) << 8) |
                   (a - (p[i] & 255));
        }
    }
}

void ce_draw_invert_frame(CeDraw *d, CeRect r, int t)
{
    if (t < 1 || r.w <= 0 || r.h <= 0 || !ce_rect_overlaps(&d->clip, r.x, r.y, r.w, r.h))
        return;
    if (2 * t >= r.w || 2 * t >= r.h) {
        invert_rect(d, r.x, r.y, r.w, r.h);                          /* all frame */
        return;
    }
    invert_rect(d, r.x, r.y, r.w, t);                                /* top    */
    invert_rect(d, r.x, r.y + r.h - t, r.w, t);                      /* bottom */
    invert_rect(d, r.x, r.y + t, t, r.h - 2 * t);                    /* left   */
    invert_rect(d, r.x + r.w - t, r.y + t, t, r.h - 2 * t);          /* right  */
}
