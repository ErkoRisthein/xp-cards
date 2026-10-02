/*
 * Card engine, Win32 — cards dragged with the mouse: Solitaire HD's drag (XP Solitaire's), shared with
 * FreeCell HD's opt-in drag and drop.
 *
 * The cards being dragged are lifted: the game renders its board without them into the back buffer,
 * and the stack, rendered once into a sprite, floats over it. Every move presents the union of the old
 * and the new rect as one frame (ce_backbuf_present: composed off screen, so nothing flickers); WM_PAINT
 * presents the sprite over what it paints. A refused drop slides the stack back to where it came from
 * (ce_drag_zip_back: a straight flight, ce_anim_fly). A drag without a sprite only keeps the position:
 * the game draws it as part of its board (Solitaire's "Outline dragging", or its fallback when there is
 * no memory for the sprite).
 *
 * A press becomes a drag once the pointer has moved past the system's drag threshold
 * (ce_drag_threshold_passed: SM_CXDRAG / SM_CYDRAG), so a click never moves anything by accident.
 */
#ifndef CE_DRAG_H
#define CE_DRAG_H

#include <windows.h>
#include "engine/geom.h"
#include "engine/image.h"
#include "anim.h"
#include "backbuf.h"

typedef struct CeDrag {
    CeImage *sprite;            /* the lifted stack (owned), or NULL: the game draws the drag itself */
    int      x, y, w, h;        /* the stack's rect on the client */
    int      grab_dx, grab_dy;  /* the pointer minus (x, y): kept while the stack follows the pointer */
} CeDrag;

/* Begin: the stack's rect (x, y, w, h), grabbed at (px, py); sprite (may be NULL) is owned from now on. */
void   ce_drag_begin(CeDrag *d, CeImage *sprite, int x, int y, int w, int h, int px, int py);
CeRect ce_drag_rect(const CeDrag *d);
/* The pointer is at (px, py): the stack follows it (the grab offset kept). With a sprite the window
 * shows the move at once (the union of the old and the new rect). Returns the rect before the move. */
CeRect ce_drag_move(CeDrag *d, CeBackBuf *bb, HWND hwnd, int px, int py);
/* WM_PAINT: rc from the back buffer with the sprite on top; 0 (nothing painted) without a sprite. */
int    ce_drag_paint(const CeDrag *d, CeBackBuf *bb, HDC dc, const RECT *rc);
/* A refused drop: the stack flies back to (x0, y0) (px_per_frame per frame_ms frame, ce_anim_fly; the
 * back buffer must show the board without it), and ends there. Without a sprite nothing is drawn (the
 * game animates its own outline). Returns the frames drawn; *frames (may be NULL) gets their number. */
int    ce_drag_zip_back(CeDrag *d, CeAnimClock *anim, CeBackBuf *bb, HWND hwnd, int x0, int y0, int px_per_frame,
                        int frame_ms, CeAnimAbort abort, void *ctx, int *frames);
/* The drag is over: with a sprite its rect is invalidated (the back buffer shows the board there), then
 * the sprite is freed. ce_drag_free frees it without invalidating (the window is gone, or redrawn). */
void   ce_drag_end(CeDrag *d, HWND hwnd);
void   ce_drag_free(CeDrag *d);
/* The pointer at (x, y) is farther from the press at (x0, y0) than the system's drag threshold. */
int    ce_drag_threshold_passed(int x0, int y0, int x, int y);

#endif
