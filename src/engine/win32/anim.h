/*
 * Card engine, Win32 — frame-timed animation: the clock, eased card flights (ROADMAP 2d), and the
 * flight scheduler that lets the cards of a cascade overlap.
 *
 * Frames are paced against timeGetTime at a fixed period (both games: 10 ms), with the system timer at
 * 1 ms (timeBeginPeriod) while something moves: a plain Sleep(10) lasts a whole clock tick on XP
 * (10-15.6 ms), which made flights up to twice as slow as designed. Positions come from the time, not
 * from a frame count (late frames are dropped, the motion keeps its speed), and every frame shows the
 * position due one frame later, as XP's AnimateCard did (its frame i of N, drawn at (i - 1) frames, is at
 * i / N of the way): a flight of d ms arrives at d - frame_ms, like XP's N-frame flight with d = N frames.
 * While it waits, the loop lets Windows see the thread retrieve messages (PeekMessage PM_NOREMOVE: only
 * sent messages are dispatched, so posted input waits until the animation is over), so a long replay
 * never looks "Not Responding".
 *
 * A clock is begun by the first animation and kept at 1 ms until the game says it is idle (after the
 * whole replay of a move, not after each card), since timeBeginPeriod/timeEndPeriod per flight cost
 * time on XP.
 *
 * Flights (CeFlights). Each card in the air is a flight: a sprite (and optionally a shadow under it)
 * moving from one rect to another along an easing curve (engine/ease.h), or turning over in place (a
 * flip: one side squeezed to its vertical axis, then the other side widening from it). A frame presents
 * the union of each flight's old and new rects plus whatever the game re-rendered meanwhile
 * (ce_flights_dirty), every sprite composed over the back buffer (ce_backbuf_present_layers: dirty
 * rects only, nothing flickers). The game keeps the cards in the air out of its back buffer (it knows
 * them from fl.f[]) and shows each one when it lands (the land callback: re-render, ce_flights_dirty).
 *
 * A cascade overlaps: the game adds the next card's flight when the previous one has flown
 * CE_CASCADE_PERCENT of its time (ce_flights_add, then ce_flights_run until ce_flights_next). Cards bound
 * for the same pile land in the order they were added, and a card that leaves a pile waits for the cards
 * still landing there (ce_flights_settle). A flight is added held (not landed, even when due) until
 * ce_flights_release, ce_flights_settle or ce_flights_land_all, so a game that moves its card in its
 * board only after the animation call returns never sees it land before that: it releases the held
 * flight in its next animation call.
 */
#ifndef CE_ANIM_H
#define CE_ANIM_H

#include <windows.h>
#include "engine/ease.h"
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

/* One eased flight of sprite s (may be NULL: nothing drawn on top) from from.(x, y) to to.(x, y) in dur
 * ms (frame-timed, see above), with an optional shadow drawn under it at (+sdx, +sdy); each frame is
 * presented as the union of the old and new rects (sprite and shadow). The back buffer must already
 * show the board without the flying sprite, and the screen must show it at its source. Begins the
 * clock. Returns the number of frames drawn (also in *frames, may be NULL). */
int  ce_anim_fly(CeAnimClock *c, CeBackBuf *bb, HDC dc, CeRect from, CeRect to, int dur, CeEase ease, int frame_ms,
                 const CeImage *s, const CeImage *shadow, int sdx, int sdy, CeAnimAbort abort, void *ctx,
                 int *frames);

/* ---- the flight scheduler -------------------------------------------------------------------------- */

#define CE_MAX_FLIGHTS      64
#define CE_MAX_FLIGHT_DIRTY 48

enum { CE_FLIGHT_MOVE = 0, CE_FLIGHT_FLIP = 1 };

typedef struct CeFlight {
    const CeImage *img;          /* the sprite (not owned); a flip: the side shown first */
    const CeImage *img2;         /* a flip: the side shown last */
    const CeImage *shadow;       /* optional (not owned), drawn first, at the sprite's position + (sdx, sdy) */
    int      sdx, sdy;
    int      kind;               /* CE_FLIGHT_MOVE / CE_FLIGHT_FLIP (from == to) */
    CeRect   from, to;           /* the sprite's rect at the start and the end */
    DWORD    t0;                 /* start (timeGetTime) */
    int      dur;                /* ms */
    CeEase   ease;
    int      early;              /* before t0: 1 = drawn at `from` (a card lifted off its pile), 0 = not drawn */
    int      src, dst;           /* the game's pile keys (-1: none) */
    int      user;               /* the game's own value */
    int      hold;               /* not landed, even when due */
    /* drawing state */
    int      on;                 /* drawn: `shown` is on the screen */
    CeRect   shown;              /* the rect drawn last (sprite and shadow) */
    int      lx, ly;             /* where the sprite was drawn last */
    CeImage *tmp;                /* a flip's squeezed side (owned) */
    CeImage  view;               /* the part of tmp drawn last */
    const CeImage *drawn;        /* the image drawn last (img, img2 or &view) */
} CeFlight;

/* A flight arrived (it is no longer in fl.f[]): the game shows its card in the back buffer and reports
 * what it re-rendered with ce_flights_dirty. */
typedef void (*CeFlightLand)(void *ctx, const CeFlight *f);

typedef struct CeFlights {
    CeAnimClock *clock;
    CeBackBuf   *bb;
    HWND         hwnd;
    int          frame_ms;
    CeFlightLand land;
    CeAnimAbort  abort;          /* 1 = the layout changed: every flight is dropped */
    void        *ctx;
    CeFlight     f[CE_MAX_FLIGHTS];
    int          n;
    CeRect       dirty[CE_MAX_FLIGHT_DIRTY];
    int          ndirty;
    int          frames;         /* frames presented since init (for the log) */
    int          aborted;        /* the last run stopped because of abort */
} CeFlights;

void  ce_flights_init(CeFlights *fl, CeAnimClock *clock, CeBackBuf *bb, HWND hwnd, int frame_ms, CeFlightLand land,
                      CeAnimAbort abort, void *ctx);
/* The back buffer changed in r while cards fly (or just before one takes off): present r with the next
 * frame (or at the end of the run). */
void  ce_flights_dirty(CeFlights *fl, CeRect r);
/* A card is about to leave pile key src: the flights still landing there land first (run to the end).
 * Returns 0, or -1 if the run was aborted. */
int   ce_flights_settle(CeFlights *fl, int src);
/* A new flight of img from `from` to `to`, starting now (or later, so that it lands after the flights
 * already bound for dst), dur ms along ease; it is held and drawn at `from` until it starts. NULL if
 * there is no room (everything lands first, then it is added). The caller may set the shadow, a flip,
 * early, t0 (a staggered deal) and hold before the next run. */
CeFlight *ce_flights_add(CeFlights *fl, const CeImage *img, CeRect from, CeRect to, int dur, CeEase ease, int src,
                         int dst, int user);
/* Release every held flight (the game has moved their cards in its board). */
void  ce_flights_release(CeFlights *fl);
/* When a cascade's next card may start: f's start + CE_CASCADE_PERCENT of its time. */
DWORD ce_flights_next(const CeFlight *f);
/* Run frames until the time `until` (timeGetTime). Returns 0, or -1 if aborted. */
int   ce_flights_run(CeFlights *fl, DWORD until);
/* Release every hold and run until every flight has landed; then whatever is still dirty is presented.
 * Returns 0, or -1 if aborted. */
int   ce_flights_land_all(CeFlights *fl);
/* Forget every flight and dirty rect without drawing (the layout changed, the window is going). */
void  ce_flights_drop(CeFlights *fl);

#endif
