/*
 * Card engine, Win32 — frame-timed animation.
 *
 * Frames are paced against timeGetTime at a fixed period (FreeCell: 10 ms), with the system timer at
 * 1 ms (timeBeginPeriod) while something moves: a plain Sleep(10) lasts a whole clock tick on XP
 * (10-15.6 ms), which made flights up to twice as slow as designed. Late frames are dropped (the frame
 * due now is drawn next), the landing frame never is. While it waits, the loop lets Windows see the
 * thread retrieve messages (PeekMessage PM_NOREMOVE: only sent messages are dispatched, so posted
 * input waits until the animation is over), so a long replay never looks "Not Responding".
 *
 * A clock is begun by the first animation and kept at 1 ms until the game says it is idle (after the
 * whole replay of a move, not after each card), since timeBeginPeriod/timeEndPeriod per flight cost
 * time on XP.
 */
#ifndef CE_ANIM_H
#define CE_ANIM_H

#include <windows.h>
#include "engine/geom.h"
#include "engine/image.h"
#include "backbuf.h"

typedef struct CeAnimClock {
    int period;                  /* timeBeginPeriod(1) is in effect */
} CeAnimClock;

void ce_anim_begin(CeAnimClock *c);   /* 1-ms timer resolution from now (no-op if already) */
void ce_anim_idle(CeAnimClock *c);    /* back to normal (no-op if not begun) */

/* Frame i (1-based) of an animation that started at t0 (timeGetTime) is on screen until
 * t0 + i * frame_ms: wait for that (GdiFlush first, PeekMessage after), and return the next frame to
 * draw, the one due now (at least i + 1). */
int  ce_anim_frame_wait(DWORD t0, int i, int frame_ms);

/* 1 = stop the animation now (e.g. the layout changed, so the sprite and the rects are stale). Called
 * before every frame. */
typedef int (*CeAnimAbort)(void *ctx);

/* A straight flight of sprite s (may be NULL: nothing drawn on top) from from.(x, y) to to.(x, y),
 * XP's AnimateCard: N = distance / px_per_frame frames (at least 1), frames i = 1 .. N-1 at
 * from + d * i / N, then the destination, each presented as the union of the sprite's old and new rects
 * (ce_backbuf_present; the sprite rect is (x, y, from.w, from.h)). The back buffer must already show
 * the board without the flying sprite, and the screen must show it at its source. Begins the clock.
 * Returns the number of frames drawn; *frames gets N. */
int  ce_anim_fly(CeAnimClock *c, CeBackBuf *bb, HDC dc, CeRect from, CeRect to, int px_per_frame,
                 int frame_ms, const CeImage *s, CeAnimAbort abort, void *ctx, int *frames);

#endif
