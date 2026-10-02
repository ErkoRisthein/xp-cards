/*
 * Card engine, Win32 — the back buffer: a 32-bpp top-down DIB section that the game's renderer draws
 * into (as a CeImage), painted with BitBlt, plus a small scratch DIB for flicker-free animation
 * frames, and the live-resize bookkeeping.
 *
 * Painting model (FreeCell HD's, see docs/DESIGN.md "Rendering"): the back buffer always holds the
 * board as last rendered; the game re-renders only what changed and invalidates exactly that;
 * WM_PAINT just copies from the back buffer (ce_backbuf_paint), and WM_ERASEBKGND does nothing.
 *
 * Live resize: between WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE the buffer grows in 256-px steps and is
 * reused when shrinking, and the game should build its sprites at quality 0 (ce_backbuf_quality);
 * when the loop ends after a resize, ce_backbuf_exit_sizemove asks for one more layout at quality 1.
 */
#ifndef CE_BACKBUF_H
#define CE_BACKBUF_H

#include <windows.h>
#include <stdint.h>
#include "engine/geom.h"
#include "engine/image.h"

typedef struct CeBackBuf {
    HDC       memdc;
    HBITMAP   dib, old_bmp;
    uint32_t *bits;             /* NULL until the first successful ce_backbuf_ensure */
    int       buf_w, buf_h;     /* allocated DIB size (may exceed the client) */
    CeImage   fb;               /* client-sized view of the DIB: what the game draws into */

    HDC       sdc;              /* scratch DIB for animation frames */
    HBITMAP   sdib, sold_bmp;
    uint32_t *sbits;
    int       s_w, s_h;

    int       in_sizemove;      /* between WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE */
    int       sized_in_loop;    /* the game re-laid out during it (ce_backbuf_note_layout) */
} CeBackBuf;

/* Back buffer for a w x h client (fb becomes its w x h view). While live-resizing it grows in 256-px
 * steps and is reused when shrinking; otherwise it is reallocated when it is much larger than needed.
 * Returns 0 (keeping the old buffer and view) if memory runs out. */
int  ce_backbuf_ensure(CeBackBuf *bb, int w, int h);
void ce_backbuf_free(CeBackBuf *bb);                 /* both DIBs; the struct can be reused */

/* Live resize. WM_ENTERSIZEMOVE: ce_backbuf_enter_sizemove. After every re-layout the game calls
 * ce_backbuf_note_layout. WM_EXITSIZEMOVE: if ce_backbuf_exit_sizemove returns 1 (the client was laid
 * out during the loop and is not minimized or empty), lay out again at *w x *h with the best quality. */
void ce_backbuf_enter_sizemove(CeBackBuf *bb);
void ce_backbuf_note_layout(CeBackBuf *bb);
int  ce_backbuf_exit_sizemove(CeBackBuf *bb, HWND hwnd, int *w, int *h);
int  ce_backbuf_quality(const CeBackBuf *bb);        /* sprite quality now: 0 while live-resizing, else 1 */

/* WM_PAINT: copy rc from the back buffer (if ready, i.e. the game has a layout and the buffer
 * exists), filling any part outside it (or everything when not ready) with bg. */
void ce_backbuf_paint(CeBackBuf *bb, HDC dc, const RECT *rc, int ready, HBRUSH bg);

/* Copy (x, y, w, h) of the back buffer to dc (nothing if empty). */
void ce_backbuf_blit(CeBackBuf *bb, HDC dc, int x, int y, int w, int h);

/* Show region r of the back buffer with the sprite s drawn on top at (sx, sy), as one flicker-free
 * frame (an animation step): only the part under the sprite's rect (sx, sy, sw, sh) goes through the
 * scratch DIB; the rest is copied straight from the back buffer; every pixel is written once, and the
 * back buffer itself is left untouched. s may be NULL (then only the back buffer shows). */
void ce_backbuf_present(CeBackBuf *bb, HDC dc, CeRect r, const CeImage *s, int sx, int sy, int sw, int sh);

/* One image of a frame: drawn (src-over) with its top-left at (x, y). */
typedef struct CeLayer {
    const CeImage *img;
    int            x, y;
} CeLayer;

/* ce_backbuf_present with several images on top, in order (the last one uppermost): only the part of r
 * under their rects goes through the scratch DIB; layers with a NULL image are skipped. */
void ce_backbuf_present_layers(CeBackBuf *bb, HDC dc, CeRect r, const CeLayer *ly, int n);

#endif
