/*
 * Card engine — easing curves and flight durations (see ease.h).
 */
#include "ease.h"

#include <stdint.h>

/* control points in thousandths */
static const int points[CE_EASE_COUNT][4] = {
    { 0, 0, 1000, 1000 },          /* linear */
    { 200, 0, 0, 1000 },           /* standard */
    { 0, 0, 200, 1000 },           /* decelerate */
    { 300, 0, 1000, 1000 },        /* accelerate */
};

static int32_t lut[CE_EASE_COUNT][CE_EASE_STEPS + 1];
static int built;

/* One coordinate of the Bezier at parameter u (0..ONE), for control values c1, c2 (thousandths), in
 * 1/ONE: 3 (1-u)^2 u c1 + 3 (1-u) u^2 c2 + u^3. */
static int64_t bez(int64_t u, int c1, int c2)
{
    const int64_t one = CE_EASE_ONE;
    int64_t v = one - u;
    int64_t a = v * v / one * u / one;                /* (1-u)^2 u, in 1/ONE */
    int64_t b = v * u / one * u / one;                /* (1-u) u^2 */
    int64_t c = u * u / one * u / one;                /* u^3 */
    return (3 * a * c1 + 3 * b * c2) / 1000 + c;
}

static void build(void)
{
    int e, i;
    for (e = 0; e < CE_EASE_COUNT; e++) {
        const int *p = points[e];
        for (i = 0; i <= CE_EASE_STEPS; i++) {
            int64_t x = (int64_t)i * CE_EASE_ONE / CE_EASE_STEPS, lo = 0, hi = CE_EASE_ONE, y;
            /* x(u) is monotonic (0 <= x1, x2 <= 1): the u with x(u) = x by bisection, 2^-16 exact */
            while (hi - lo > 1) {
                int64_t mid = (lo + hi) / 2;
                if (bez(mid, p[0], p[2]) < x)
                    lo = mid;
                else
                    hi = mid;
            }
            /* the closer of lo, hi */
            if (x - bez(lo, p[0], p[2]) < bez(hi, p[0], p[2]) - x)
                hi = lo;
            y = bez(hi, p[1], p[3]);
            if (i == 0 || y < 0)
                y = 0;
            if (i == CE_EASE_STEPS || y > CE_EASE_ONE)
                y = CE_EASE_ONE;
            if (i > 0 && y < lut[e][i - 1])
                y = lut[e][i - 1];                    /* (rounding) keep it monotonic */
            lut[e][i] = (int32_t)y;
        }
    }
    built = 1;
}

int ce_ease(CeEase e, int t, int d)
{
    int64_t x;
    int i, f;
    if (t <= 0 && d > 0)
        return 0;
    if (d <= 0 || t >= d)
        return CE_EASE_ONE;
    if ((unsigned)e >= CE_EASE_COUNT)
        e = CE_EASE_LINEAR;
    if (e == CE_EASE_LINEAR)
        return (int)((int64_t)t * CE_EASE_ONE / d);
    if (!built)
        build();
    x = (int64_t)t * CE_EASE_STEPS * CE_EASE_ONE / d;  /* time in table steps, 16-bit fraction */
    i = (int)(x >> 16);
    f = (int)(x & 0xffff);
    return lut[e][i] + (int)(((int64_t)(lut[e][i + 1] - lut[e][i]) * f) >> 16);
}

int ce_ease_lerp(int a, int b, int p)
{
    int64_t d = (int64_t)(b - a) * p;
    return a + (int)(d >= 0 ? (d + CE_EASE_ONE / 2) >> 16 : -((-d + CE_EASE_ONE / 2) >> 16));
}

void ce_ease_points(CeEase e, int pts[4])
{
    int i;
    if ((unsigned)e >= CE_EASE_COUNT)
        e = CE_EASE_LINEAR;
    for (i = 0; i < 4; i++)
        pts[i] = points[e][i];
}

int ce_isqrt(unsigned long v)
{
    unsigned long r = 0, bit = 1ul << 30;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (int)r;
}

int ce_flight_ms_xp(int dist, int px_per_frame, int frame_ms)
{
    int n = px_per_frame > 0 ? dist / px_per_frame : 1;
    return (n < 1 ? 1 : n) * frame_ms;
}

int ce_flight_ms(int dist, int scale_milli, int px_per_frame, int frame_ms)
{
    int xp = ce_flight_ms_xp(dist, px_per_frame, frame_ms);
    long d = scale_milli > 0 ? (long)dist * 1000 / scale_milli : dist;
    int env;
    if (d > CE_FLIGHT_FAR_XP)
        d = CE_FLIGHT_FAR_XP;
    if (d < 0)
        d = 0;
    env = CE_FLIGHT_MIN_MS + (int)(d * (CE_FLIGHT_MAX_MS - CE_FLIGHT_MIN_MS) / CE_FLIGHT_FAR_XP);
    return xp < env ? xp : env;
}

int ce_cascade_ms(int dur)
{
    int c = dur * CE_CASCADE_PERCENT / 100;
    return dur > 0 && c < 1 ? 1 : c;
}

int ce_flight_delay(int dur, int prev_end)
{
    return prev_end > dur ? prev_end - dur : 0;
}
