/*
 * Card engine — native tests of the easing curves and flight timing (engine/ease.h, ROADMAP 2d): the
 * fixed-point tables against a floating-point cubic-bezier, the endpoints, monotonicity, each curve's
 * shape (the standard curve ahead of a straight flight after its first steps, the decelerating one
 * always ahead and leaving fast, the accelerating one always behind), the rounding of positions, and
 * the durations: never longer than XP's straight flight of the same distance, at most 160 ms, a
 * cascade overlapping so it ends sooner than XP's, and cards bound for one pile landing in order.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "engine/ease.h"

static int failures, checks;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            printf("FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond);         \
            printf(__VA_ARGS__);                                            \
            printf("\n");                                                   \
        }                                                                   \
    } while (0)

static const char *const names[CE_EASE_COUNT] = { "linear", "standard", "decelerate", "accelerate" };

/* The reference: cubic-bezier(x1, y1, x2, y2) at time x, in doubles (bisection on the curve parameter). */
static double bez1(double u, double c1, double c2)
{
    double v = 1 - u;
    return 3 * v * v * u * c1 + 3 * v * u * u * c2 + u * u * u;
}

static double reference(CeEase e, double x)
{
    int p[4], i;
    double lo = 0, hi = 1;
    ce_ease_points(e, p);
    for (i = 0; i < 60; i++) {
        double mid = (lo + hi) / 2;
        if (bez1(mid, p[0] / 1000.0, p[2] / 1000.0) < x)
            lo = mid;
        else
            hi = mid;
    }
    return bez1((lo + hi) / 2, p[1] / 1000.0, p[3] / 1000.0);
}

static void test_points(void)
{
    int p[4];
    ce_ease_points(CE_EASE_STANDARD, p);
    CHECK(p[0] == 200 && p[1] == 0 && p[2] == 0 && p[3] == 1000, "standard = cubic-bezier(0.2, 0, 0, 1)");
    ce_ease_points(CE_EASE_DECEL, p);
    CHECK(p[0] == 0 && p[1] == 0 && p[2] == 200 && p[3] == 1000, "decelerate = cubic-bezier(0, 0, 0.2, 1)");
    ce_ease_points(CE_EASE_ACCEL, p);
    CHECK(p[0] == 300 && p[1] == 0 && p[2] == 1000 && p[3] == 1000, "accelerate = cubic-bezier(0.3, 0, 1, 1)");
    ce_ease_points(CE_EASE_LINEAR, p);
    CHECK(p[0] == 0 && p[1] == 0 && p[2] == 1000 && p[3] == 1000, "linear");
}

static void test_endpoints(void)
{
    int e, d;
    static const int durs[] = { 1, 2, 7, 10, 60, 100, 160, 1000, 30000 };
    for (e = 0; e < CE_EASE_COUNT; e++)
        for (d = 0; d < (int)(sizeof durs / sizeof durs[0]); d++) {
            int D = durs[d];
            CHECK(ce_ease((CeEase)e, 0, D) == 0, "%s(0 of %d) = %d", names[e], D, ce_ease((CeEase)e, 0, D));
            CHECK(ce_ease((CeEase)e, -5, D) == 0, "%s before the start", names[e]);
            CHECK(ce_ease((CeEase)e, D, D) == CE_EASE_ONE, "%s(%d of %d)", names[e], D, D);
            CHECK(ce_ease((CeEase)e, D + 9, D) == CE_EASE_ONE, "%s after the end", names[e]);
        }
    for (e = 0; e < CE_EASE_COUNT; e++) {
        CHECK(ce_ease((CeEase)e, 0, 0) == CE_EASE_ONE, "%s: a flight of no time is there at once", names[e]);
        CHECK(ce_ease((CeEase)e, 3, -1) == CE_EASE_ONE, "%s: negative duration", names[e]);
    }
    CHECK(ce_ease((CeEase)99, 50, 100) == CE_EASE_ONE / 2, "an unknown curve is linear");
}

static void test_monotonic_and_accuracy(void)
{
    int e, d;
    static const int durs[] = { 3, 10, 17, 60, 113, 160, 257, 4096 };
    double worst[CE_EASE_COUNT] = { 0 };
    for (e = 0; e < CE_EASE_COUNT; e++)
        for (d = 0; d < (int)(sizeof durs / sizeof durs[0]); d++) {
            int D = durs[d], t, prev = 0, bad = 0;
            for (t = 0; t <= D; t++) {
                int p = ce_ease((CeEase)e, t, D);
                double err = fabs(p / (double)CE_EASE_ONE - reference((CeEase)e, t / (double)D));
                if (p < prev || p < 0 || p > CE_EASE_ONE)
                    bad++;
                if (err > worst[e])
                    worst[e] = err;
                prev = p;
            }
            CHECK(bad == 0, "%s over %d ms: %d steps backwards or out of range", names[e], D, bad);
        }
    for (e = 0; e < CE_EASE_COUNT; e++)
        CHECK(worst[e] < 0.002, "%s: the table is off the cubic-bezier by %.5f", names[e], worst[e]);
    printf("  curves: worst error against the double cubic-bezier: linear %.5f, standard %.5f, decelerate %.5f, "
           "accelerate %.5f\n", worst[0], worst[1], worst[2], worst[3]);
}

static void test_shapes(void)
{
    int t;
    const int D = 1000;
    for (t = 1; t < D; t++) {
        int lin = ce_ease(CE_EASE_LINEAR, t, D);
        if (t >= 100)
            CHECK(ce_ease(CE_EASE_STANDARD, t, D) > lin, "standard ahead of a straight flight at %d/1000", t);
        CHECK(ce_ease(CE_EASE_DECEL, t, D) > lin, "decelerate always ahead at %d/1000", t);
        CHECK(ce_ease(CE_EASE_ACCEL, t, D) < lin, "accelerate always behind at %d/1000", t);
    }
    /* a fast start: a card leaves at once */
    CHECK(ce_ease(CE_EASE_DECEL, 100, D) > CE_EASE_ONE * 3 / 10, "decelerate: 30%% of the way in 10%% of the time (%d)",
          ce_ease(CE_EASE_DECEL, 100, D));
    CHECK(ce_ease(CE_EASE_STANDARD, 250, D) > CE_EASE_ONE / 2, "standard: half way in a quarter of the time (%d)",
          ce_ease(CE_EASE_STANDARD, 250, D));
    CHECK(ce_ease(CE_EASE_STANDARD, 500, D) > CE_EASE_ONE * 85 / 100, "standard: 85%% in half the time");
    /* the first frame of a 160-ms flight (look-ahead one 10-ms frame) is under way, the second well ahead */
    CHECK(ce_ease(CE_EASE_STANDARD, 10, 160) > 0 && ce_ease(CE_EASE_STANDARD, 20, 160) > CE_EASE_ONE * 125 / 1000,
          "standard: under way at the first frame, %d / %d", ce_ease(CE_EASE_STANDARD, 10, 160),
          ce_ease(CE_EASE_STANDARD, 20, 160));
}

static void test_lerp(void)
{
    CHECK(ce_ease_lerp(10, 500, 0) == 10 && ce_ease_lerp(10, 500, CE_EASE_ONE) == 500, "endpoints");
    CHECK(ce_ease_lerp(500, 10, 0) == 500 && ce_ease_lerp(500, 10, CE_EASE_ONE) == 10, "endpoints backwards");
    CHECK(ce_ease_lerp(0, 3, CE_EASE_ONE / 2) == 2 && ce_ease_lerp(3, 0, CE_EASE_ONE / 2) == 1,
          "half way rounds away from the start symmetrically: %d %d", ce_ease_lerp(0, 3, CE_EASE_ONE / 2),
          ce_ease_lerp(3, 0, CE_EASE_ONE / 2));
    CHECK(ce_ease_lerp(-100, 100, CE_EASE_ONE / 2) == 0, "negative coordinates");
    CHECK(ce_ease_lerp(0, 1904, CE_EASE_ONE / 4) == 476, "a quarter of 1904");
}

static void test_isqrt(void)
{
    unsigned long v;
    int bad = 0;
    for (v = 0; v < 200000; v += (v < 5000 ? 1 : 7))
        if (ce_isqrt(v) != (int)floor(sqrt((double)v)))
            bad++;
    CHECK(bad == 0, "%d wrong roots", bad);
    CHECK(ce_isqrt(1904ul * 1904 + 996ul * 996) == (int)sqrt(1904.0 * 1904 + 996.0 * 996), "a full-HD diagonal");
}

/* XP's straight flight: N = max(1, dist / px) frames (FreeCell 37 s, Solitaire's Finish 60 s and zip 36 s). */
static void test_xp_pace(void)
{
    CHECK(ce_flight_ms_xp(0, 99, 10) == 10, "no distance: one frame");
    CHECK(ce_flight_ms_xp(98, 99, 10) == 10, "under one step: one frame");
    CHECK(ce_flight_ms_xp(99 * 7 + 98, 99, 10) == 70, "seven whole steps");
    CHECK(ce_flight_ms_xp(500, 0, 10) == 10, "no step size: one frame");
}

/* Never slower than XP's pace, never over 160 ms, the envelope beyond, monotonic in the distance. */
static void test_durations(void)
{
    static const int scales[] = { 500, 1000, 1500, 2000, 2667, 2677, 3250 };
    static const int steps[] = { 36, 37, 60 };
    int si, pi, dist, slower = 0, over = 0, back = 0, env = 0;
    for (si = 0; si < (int)(sizeof scales / sizeof scales[0]); si++)
        for (pi = 0; pi < 3; pi++) {
            int s = scales[si], ppf = (steps[pi] * s + 500) / 1000, prev = 0;
            for (dist = 0; dist <= 4000; dist++) {
                int xp = ce_flight_ms_xp(dist, ppf, 10), d = ce_flight_ms(dist, s, ppf, 10);
                long dxp = (long)dist * 1000 / s;
                int e = CE_FLIGHT_MIN_MS + (int)((dxp > CE_FLIGHT_FAR_XP ? CE_FLIGHT_FAR_XP : dxp) * 100 / CE_FLIGHT_FAR_XP);
                if (d > xp)
                    slower++;
                if (d > CE_FLIGHT_MAX_MS)
                    over++;
                if (d < prev)
                    back++;
                if (d != (xp < e ? xp : e))
                    env++;
                prev = d;
            }
        }
    CHECK(slower == 0, "%d durations longer than XP's straight flight", slower);
    CHECK(over == 0, "%d durations over 160 ms", over);
    CHECK(back == 0, "%d durations shorter for a longer distance", back);
    CHECK(env == 0, "%d durations off min(XP, envelope)", env);
    CHECK(ce_flight_ms(5000, 1000, 37, 10) == 160, "far: the envelope's 160 ms");
    CHECK(ce_flight_ms(30, 1000, 37, 10) == 10, "a short hop keeps XP's single frame");
}

/* A cascade of n cards: XP flew them one after another (the sum of their straight flights); ours start
 * each at CE_CASCADE_PERCENT of the previous one (and later only to land in order on one pile). */
static void test_cascades(void)
{
    int trial, slower = 0, order = 0;
    srand(2026);
    CHECK(ce_cascade_ms(100) == 60 && ce_cascade_ms(1) == 1 && ce_cascade_ms(0) == 0, "60%%");
    CHECK(ce_flight_delay(100, 50) == 0 && ce_flight_delay(100, 130) == 30 && ce_flight_delay(40, -10) == 0,
          "landing order delay");
    for (trial = 0; trial < 2000; trial++) {
        int n = 2 + rand() % 51, i, xp = 0, start = 0, end = 0, last_end[8] = { 0 }, ppf = 37 + rand() % 70;
        for (i = 0; i < n; i++) {
            int dist = rand() % 1600, pile = rand() % 8, s = 1000 + rand() % 2000;
            int dur = ce_flight_ms(dist, s, ppf, 10), t0;
            xp += ce_flight_ms_xp(dist, ppf, 10);
            t0 = start + ce_flight_delay(dur, last_end[pile] - start);
            if (t0 + dur < last_end[pile])
                order++;
            last_end[pile] = t0 + dur;
            if (t0 + dur > end)
                end = t0 + dur;
            start = t0 + ce_cascade_ms(dur);
        }
        if (end > xp)
            slower++;
    }
    CHECK(slower == 0, "%d of 2000 cascades ended later than XP's one-by-one flights", slower);
    CHECK(order == 0, "%d cards landed before the card under them", order);
}

int main(void)
{
    test_points();
    test_endpoints();
    test_monotonic_and_accuracy();
    test_shapes();
    test_lerp();
    test_isqrt();
    test_xp_pace();
    test_durations();
    test_cascades();
    printf("test_ease: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
