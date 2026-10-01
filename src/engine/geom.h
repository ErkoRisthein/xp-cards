/*
 * Card engine — integer rectangles (platform independent).
 *
 * Board coordinates are client-area pixels, x to the right and y down; rects are half-open
 * ([x, x + w) x [y, y + h)).
 */
#ifndef CE_GEOM_H
#define CE_GEOM_H

typedef struct CeRect { int x, y, w, h; } CeRect;

static inline CeRect ce_rect(int x, int y, int w, int h)
{
    CeRect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

/* Does a overlap the rect (x, y, w, h)? (Half-open ranges; meant for non-empty rects.) */
static inline int ce_rect_overlaps(const CeRect *a, int x, int y, int w, int h)
{
    return x < a->x + a->w && a->x < x + w && y < a->y + a->h && a->y < y + h;
}

/* The smallest rect holding both p and q. */
static inline CeRect ce_rect_union(CeRect p, CeRect q)
{
    CeRect r;
    int x2 = p.x + p.w > q.x + q.w ? p.x + p.w : q.x + q.w;
    int y2 = p.y + p.h > q.y + q.h ? p.y + p.h : q.y + q.h;
    r.x = p.x < q.x ? p.x : q.x;
    r.y = p.y < q.y ? p.y : q.y;
    r.w = x2 - r.x;
    r.h = y2 - r.y;
    return r;
}

/* Clip r to (0, 0, w, h); returns 1 if anything is left. */
static inline int ce_rect_clip(CeRect *r, int w, int h)
{
    if (r->x < 0) { r->w += r->x; r->x = 0; }
    if (r->y < 0) { r->h += r->y; r->y = 0; }
    if (r->x + r->w > w) r->w = w - r->x;
    if (r->y + r->h > h) r->h = h - r->y;
    return r->w > 0 && r->h > 0;
}

#endif
