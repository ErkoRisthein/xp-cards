/*
 * Card engine — clipped drawing into a framebuffer (platform independent).
 *
 * A game's renderer draws a board region by region: ce_draw_begin makes a view of the framebuffer
 * restricted to a clip rect, and every primitive below takes board coordinates, skips what lies
 * outside the clip and draws the rest exactly as a full render would (all coordinates are integers),
 * so a partial repaint gives the same pixels as a full one.
 */
#ifndef CE_DRAW_H
#define CE_DRAW_H

#include <stdint.h>
#include "geom.h"
#include "image.h"
#include "cardset.h"

typedef struct CeDraw {
    CeImage img;    /* view of the framebuffer restricted to the clip rect */
    int     ox, oy; /* the clip rect's origin */
    CeRect  clip;   /* in board coordinates */
} CeDraw;

/* Clip r to fb and start drawing there; 0 (nothing to draw) if the clipped rect is empty. */
int  ce_draw_begin(CeDraw *d, CeImage *fb, CeRect r);

/* Does the rect (x, y, w, h) reach into the clip rect? */
int  ce_draw_visible(const CeDraw *d, int x, int y, int w, int h);

/* Opaque fill. */
void ce_draw_fill(CeDraw *d, int x, int y, int w, int h, uint32_t argb);

/* XP's 1-px bevel generalised to width b (tl on the top/left edges, br on the bottom/right): the
 * lines stop one pixel short at the top-right and bottom-left corners, leaving a background diagonal
 * there (a natural mitre). Pixel-identical to XP for b = 1. */
void ce_draw_bevel(CeDraw *d, CeRect r, int b, uint32_t tl, uint32_t br);

/* The same bevel for scaled-up boards: an anti-aliased ring of thickness t with rounded corners
 * (outer radius rad), mitred at 45 degrees through the top-right and bottom-left corners
 * (ce_bevel_ring_new). The ring is taken from cache's ring cache (built once per size, so clipped
 * renders stay identical to full ones); cache may be NULL (a temporary ring is built). */
void ce_draw_ring(CeDraw *d, CeCardSet *cache, CeRect r, double t, double rad, uint32_t tl, uint32_t br);

/* A sprite (src-over), or its inversion (the colour channels c -> a - c: XP's selected card). NULL
 * draws nothing. */
void ce_draw_sprite(CeDraw *d, const CeImage *s, int x, int y, int inverted);

/* Invert the opaque framebuffer where mask covers it (e.g. any card sprite: a card-shaped area). */
void ce_draw_invert_mask(CeDraw *d, const CeImage *mask, int x, int y);

/* Invert a frame of thickness t (>= 1) along the inside of r: XP's R2_NOT outline (Solitaire's
 * "Outline dragging"). Drawing it twice restores the pixels. */
void ce_draw_invert_frame(CeDraw *d, CeRect r, int t);

#endif
