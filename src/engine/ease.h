/*
 * Card engine — easing curves and flight durations for card motion (platform independent; ROADMAP 2d).
 *
 * The curves are CSS / Material cubic-bezier(x1, y1, x2, y2) timing functions: time x and progress y
 * both run from 0 to 1 along a cubic Bezier through (0, 0), (x1, y1), (x2, y2), (1, 1). Each is turned
 * once (on first use) into a table of CE_EASE_STEPS + 1 progress values at equal time steps, computed in
 * 64-bit fixed point (no floating point: the same numbers natively and on the Athlon XP); at run time a
 * curve is one table lookup and a linear interpolation, integer only.
 *
 *   CE_EASE_STANDARD  cubic-bezier(0.2, 0, 0, 1)   a card moving between piles
 *   CE_EASE_DECEL     cubic-bezier(0, 0, 0.2, 1)   a card arriving (home, a dealt card): leaves at full speed
 *   CE_EASE_ACCEL     cubic-bezier(0.3, 0, 1, 1)   a card leaving the table
 *   CE_EASE_LINEAR    XP's straight flight
 *
 * Durations are snappy (the user's rule: never slower than XP's pace): ce_flight_ms takes the straight
 * flight XP's code would make for the same distance (ce_flight_ms_xp: one frame per px_per_frame pixels)
 * and caps it with the envelope 60 ms + 100 ms * d / 640 XP px, 160 ms at most (d = the distance / the
 * board's scale). Consecutive cards of a cascade overlap: the next starts when the previous one has
 * flown CE_CASCADE_PERCENT of its time.
 */
#ifndef CE_EASE_H
#define CE_EASE_H

typedef enum CeEase {
    CE_EASE_LINEAR = 0,
    CE_EASE_STANDARD,
    CE_EASE_DECEL,
    CE_EASE_ACCEL,
    CE_EASE_COUNT
} CeEase;

#define CE_EASE_ONE        65536   /* progress 1.0 (and time 1.0 inside the tables) */
#define CE_EASE_STEPS      256     /* table resolution in time */
#define CE_CASCADE_PERCENT 60      /* a cascade's next card starts at 60% of the previous one's flight */
#define CE_FLIGHT_MIN_MS   60      /* the envelope: a short hop ... */
#define CE_FLIGHT_MAX_MS   160     /* ... up to the longest flight (at CE_FLIGHT_FAR_XP XP pixels and beyond) */
#define CE_FLIGHT_FAR_XP   640

/* Progress (0 .. CE_EASE_ONE) at time t of an animation lasting d: t <= 0 gives 0, t >= d (or d <= 0)
 * gives CE_EASE_ONE. Monotonic in t. */
int ce_ease(CeEase e, int t, int d);

/* a + (b - a) * p / CE_EASE_ONE, rounded to the nearest pixel (exactly a at p = 0, b at CE_EASE_ONE). */
int ce_ease_lerp(int a, int b, int p);

/* The curve's control points in thousandths (x1, y1, x2, y2); LINEAR is (0, 0, 1000, 1000). */
void ce_ease_points(CeEase e, int pts[4]);

/* floor(sqrt(v)) for v >= 0 (the distance of a flight, as XP's code took it). */
int ce_isqrt(unsigned long v);

/* XP's straight flight for a distance of dist pixels: N = max(1, dist / px_per_frame) frames of frame_ms
 * (px_per_frame <= 0: one frame). The card arrives at (N - 1) frames and the next one may start at N. */
int ce_flight_ms_xp(int dist, int px_per_frame, int frame_ms);

/* The eased flight's duration (ms) for dist pixels on a board of scale scale_milli / 1000:
 * min(ce_flight_ms_xp(dist, px_per_frame, frame_ms), 60 + 100 * min(d, 640) / 640) with d the distance
 * in XP pixels. Never longer than XP's pace, never longer than 160 ms. */
int ce_flight_ms(int dist, int scale_milli, int px_per_frame, int frame_ms);

/* When a cascade's next card may start: CE_CASCADE_PERCENT of dur (at least 1 ms for dur > 0). */
int ce_cascade_ms(int dur);

/* How long a flight of dur ms must wait before it starts so that it lands no earlier than the card bound
 * for the same pile that lands in prev_end ms from now (prev_end <= dur: none, 0). */
int ce_flight_delay(int dur, int prev_end);

#endif
