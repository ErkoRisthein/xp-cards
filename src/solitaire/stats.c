/*
 * Solitaire HD — statistics model and file (see stats.h).
 */
#include "stats.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"

int sol_stats_mode(int draw, int scoring)
{
    int sc = scoring == SOL_SCORING_VEGAS ? 1 : scoring == SOL_SCORING_NONE ? 2 : 0;
    return (draw == 1 ? 0 : 3) + sc;
}

void sol_stats_clear(SolStats *st)
{
    memset(st, 0, sizeof *st);
}

static SolModeStats *mode_of(SolStats *st, int mode)
{
    return mode >= 0 && mode < SOL_STATS_MODES ? &st->m[mode] : NULL;
}

void sol_stats_played(SolStats *st, int mode)
{
    SolModeStats *m = mode_of(st, mode);
    if (m && m->played < 0xFFFFFFFFu)
        m->played++;
}

int sol_stats_mode_scoring(int mode)
{
    int k = mode % 3;
    return k == 1 ? SOL_SCORING_VEGAS : k == 2 ? SOL_SCORING_NONE : SOL_SCORING_STANDARD;
}

static int32_t sat_add(int32_t a, int32_t b)
{
    int64_t v = (int64_t)a + b;
    return v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (int32_t)v;
}

/* A finished game's score: the best score, the high-score table, the money (Vegas). */
static void note_score(SolModeStats *m, int mode, int score, int has_score, int timed, uint32_t date)
{
    int sc = sol_stats_mode_scoring(mode), t, i, n;
    if (!has_score || sc == SOL_SCORING_NONE)
        return;
    if (!m->has_score || score > m->best_score) {
        m->best_score = score;
        m->has_score = 1;
    }
    t = sc == SOL_SCORING_STANDARD && timed ? SOL_TOP_TIMED : SOL_TOP_UNTIMED;
    n = (int)m->ntop[t];
    for (i = n; i > 0 && m->top[t][i - 1].score < score; i--) {}   /* below the equal, older ones */
    if (i < SOL_STATS_TOP) {
        int last = n < SOL_STATS_TOP ? n : SOL_STATS_TOP - 1;
        memmove(&m->top[t][i + 1], &m->top[t][i], (size_t)(last - i) * sizeof m->top[t][0]);
        m->top[t][i].score = score;
        m->top[t][i].date = sol_stats_date_ok(date) ? date : 0;
        if (n < SOL_STATS_TOP)
            m->ntop[t]++;
    }
    if (sc == SOL_SCORING_VEGAS) {
        if (!m->has_money || score > m->most_won)
            m->most_won = score;
        if (!m->has_money || score < m->most_lost)
            m->most_lost = score;
        m->winnings = sat_add(m->has_money ? m->winnings : 0, score);
        m->has_money = 1;
    }
}

void sol_stats_won(SolStats *st, int mode, int seconds, int score, int has_score, int timed, uint32_t date)
{
    SolModeStats *m = mode_of(st, mode);
    if (!m)
        return;
    if (m->won < m->played)
        m->won++;
    m->streak = m->streak > 0 ? m->streak + 1 : 1;
    if ((uint32_t)m->streak > m->win_streak)
        m->win_streak = (uint32_t)m->streak;
    if (seconds > 0 && (m->best_time == 0 || (uint32_t)seconds < m->best_time))
        m->best_time = (uint32_t)seconds;
    note_score(m, mode, score, has_score, timed, date);
}

void sol_stats_lost(SolStats *st, int mode, int score, int has_score, int timed, uint32_t date)
{
    SolModeStats *m = mode_of(st, mode);
    if (!m)
        return;
    m->streak = m->streak < 0 ? m->streak - 1 : -1;
    if ((uint32_t)-m->streak > m->loss_streak)
        m->loss_streak = (uint32_t)-m->streak;
    note_score(m, mode, score, has_score, timed, date);
}

int sol_stats_date_ok(uint32_t date)
{
    uint32_t y = date / 10000u, mo = date / 100u % 100u, d = date % 100u;
    return date == 0 || (y >= 1970 && y <= 9999 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31);
}

void sol_stats_date_text(uint32_t date, char *buf, size_t n)
{
    if (!n)
        return;
    if (!date || !sol_stats_date_ok(date))
        buf[0] = 0;
    else
        snprintf(buf, n, "%04u-%02u-%02u", date / 10000u, date / 100u % 100u, date % 100u);
}

unsigned sol_stats_percent(const SolModeStats *m)
{
    unsigned long long p;
    if (!m->played)
        return 0;
    p = ((unsigned long long)m->won * 100u + m->played / 2u) / m->played;
    if (p >= 100 && m->won < m->played)
        p = 99;
    return (unsigned)p;
}

/* %u only: XP's msvcrt printf does not know "ll" */
void sol_stats_format(const SolModeStats *m, int scoring, int icurrency, char *buf, size_t n)
{
    char cur[24], tm[24], sc[32];
    unsigned k = m->streak < 0 ? (unsigned)-m->streak : (unsigned)m->streak;
    if (!n)
        return;
    if (k == 0)
        snprintf(cur, sizeof cur, "0");
    else
        snprintf(cur, sizeof cur, "%u %s", k, m->streak > 0 ? (k == 1 ? "win" : "wins") : (k == 1 ? "loss" : "losses"));
    if (m->best_time)
        snprintf(tm, sizeof tm, "%u:%02u", m->best_time / 60u, m->best_time % 60u);
    else
        snprintf(tm, sizeof tm, "-");
    if (scoring == SOL_SCORING_NONE || !m->has_score)
        snprintf(sc, sizeof sc, "-");
    else
        sol_format_score(m->best_score, scoring == SOL_SCORING_VEGAS, icurrency, sc, sizeof sc);
    snprintf(buf, n, "%u\n%u\n%u%%\n%s\n%u\n%u\n%s\n%s", m->played, m->won, sol_stats_percent(m), cur,
             m->win_streak, m->loss_streak, tm, sc);
}

/* Line `line` (0 = the first) of a '\n'-separated column. */
static void put_line(char *buf, size_t n, int line, const char *text)
{
    size_t k = strlen(buf);
    if (k + 1 < n)
        snprintf(buf + k, n - k, "%s%s", line ? "\n" : "", text);
}

int sol_stats_format_top(const SolModeStats *m, int scoring, int icurrency, char *labels, size_t nl,
                         char *values, size_t nv, uint32_t dates[SOL_STATS_TOP_LINES])
{
    char v[32], lab[8];
    int lines = 0, t, i;
    if (nl) labels[0] = 0;
    if (nv) values[0] = 0;
    memset(dates, 0, SOL_STATS_TOP_LINES * sizeof dates[0]);
    if (scoring == SOL_SCORING_NONE) {
        put_line(labels, nl, 0, "No scores with None scoring.");
        put_line(values, nv, 0, "");
        return 1;
    }
    for (t = scoring == SOL_SCORING_STANDARD ? SOL_TOP_TIMED : SOL_TOP_UNTIMED; t >= 0; t--) {
        put_line(labels, nl, lines, scoring == SOL_SCORING_VEGAS ? "High scores:"
                                    : t == SOL_TOP_TIMED            ? "Timed games:"
                                                                    : "Not timed:");
        put_line(values, nv, lines, "");
        lines++;
        for (i = 0; i < SOL_STATS_TOP; i++) {
            snprintf(lab, sizeof lab, "%d.", i + 1);
            put_line(labels, nl, lines, lab);
            if ((uint32_t)i < m->ntop[t] && i < SOL_STATS_TOP) {
                sol_format_score(m->top[t][i].score, scoring == SOL_SCORING_VEGAS, icurrency, v, sizeof v);
                dates[lines] = m->top[t][i].date;
            } else {
                snprintf(v, sizeof v, "-");
            }
            put_line(values, nv, lines, v);
            lines++;
        }
    }
    if (scoring == SOL_SCORING_VEGAS) {
        static const char *const names[3] = { "Most money won:", "Most money lost:", "Current winnings:" };
        for (i = 0; i < 3; i++) {
            put_line(labels, nl, lines, names[i]);
            if (!m->has_money)
                snprintf(v, sizeof v, "-");
            else if (i == 0)
                sol_format_score(m->most_won > 0 ? m->most_won : 0, 1, icurrency, v, sizeof v);
            else if (i == 1)
                sol_format_score(m->most_lost < 0 ? (m->most_lost == INT32_MIN ? INT32_MAX : -m->most_lost) : 0, 1,
                                 icurrency, v, sizeof v);
            else
                sol_format_score(m->winnings, 1, icurrency, v, sizeof v);
            put_line(values, nv, lines, v);
            lines++;
        }
    }
    return lines;
}

/* ---- file ---------------------------------------------------------------------------------------- */

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

size_t sol_stats_serialize(const SolStats *st, uint8_t out[SOL_STATS_FILE_SIZE])
{
    uint8_t *p = out;
    memcpy(p, "SOLS", 4);
    put32(p + 4, 2);
    put32(p + 8, SOL_STATS_MODES);
    p += 12;
    for (int i = 0; i < SOL_STATS_MODES; i++, p += 136) {
        const SolModeStats *m = &st->m[i];
        put32(p, m->played);
        put32(p + 4, m->won);
        put32(p + 8, (uint32_t)m->streak);
        put32(p + 12, m->win_streak);
        put32(p + 16, m->loss_streak);
        put32(p + 20, m->best_time);
        put32(p + 24, (uint32_t)m->best_score);
        put32(p + 28, m->has_score);
        for (int t = 0; t < 2; t++) {
            uint32_t n = m->ntop[t] <= SOL_STATS_TOP ? m->ntop[t] : SOL_STATS_TOP;
            put32(p + 32 + 4 * t, n);
            for (uint32_t k = 0; k < SOL_STATS_TOP; k++) {
                uint8_t *q = p + 40 + 40 * t + 8 * k;
                put32(q, k < n ? (uint32_t)m->top[t][k].score : 0);
                put32(q + 4, k < n ? m->top[t][k].date : 0);
            }
        }
        put32(p + 120, m->has_money != 0);
        put32(p + 124, m->has_money ? (uint32_t)m->most_won : 0);
        put32(p + 128, m->has_money ? (uint32_t)m->most_lost : 0);
        put32(p + 132, m->has_money ? (uint32_t)m->winnings : 0);
    }
    put32(p, ce_crc32(out, (size_t)(p - out)));
    return SOL_STATS_FILE_SIZE;
}

int sol_stats_deserialize(SolStats *st, const uint8_t *data, size_t len)
{
    SolStats t;
    const uint8_t *p;
    uint32_t version, stride;
    if (!data || len < 16 || memcmp(data, "SOLS", 4))
        return 0;
    version = get32(data + 4);
    if (!((version == 1 && len == SOL_STATS_FILE_SIZE_V1) || (version == 2 && len == SOL_STATS_FILE_SIZE)) ||
        get32(data + 8) != SOL_STATS_MODES || ce_crc32(data, len - 4) != get32(data + len - 4))
        return 0;
    stride = version == 1 ? 32u : 136u;
    memset(&t, 0, sizeof t);
    p = data + 12;
    for (int i = 0; i < SOL_STATS_MODES; i++, p += stride) {
        SolModeStats *m = &t.m[i];
        m->played = get32(p);
        m->won = get32(p + 4);
        m->streak = (int32_t)get32(p + 8);
        m->win_streak = get32(p + 12);
        m->loss_streak = get32(p + 16);
        m->best_time = get32(p + 20);
        m->best_score = (int32_t)get32(p + 24);
        m->has_score = get32(p + 28) != 0;
        if (m->won > m->played)
            return 0;
        if (version == 1)
            continue;                            /* migrated: no high scores, no money yet */
        for (int k = 0; k < 2; k++) {
            uint32_t n = get32(p + 32 + 4 * k);
            if (n > SOL_STATS_TOP)
                return 0;
            m->ntop[k] = n;
            for (uint32_t e = 0; e < SOL_STATS_TOP; e++) {
                const uint8_t *q = p + 40 + 40 * k + 8 * e;
                int32_t score = (int32_t)get32(q);
                uint32_t date = get32(q + 4);
                if (e >= n) {
                    if (score || date)
                        return 0;                /* unused places are zero */
                    continue;
                }
                if (!sol_stats_date_ok(date) || (e > 0 && score > m->top[k][e - 1].score))
                    return 0;
                m->top[k][e].score = score;
                m->top[k][e].date = date;
            }
        }
        m->has_money = get32(p + 120);
        m->most_won = (int32_t)get32(p + 124);
        m->most_lost = (int32_t)get32(p + 128);
        m->winnings = (int32_t)get32(p + 132);
        if (m->has_money > 1 || (!m->has_money && (m->most_won || m->most_lost || m->winnings)) ||
            m->most_won < m->most_lost)
            return 0;
    }
    *st = t;
    return (int)version;
}

int sol_stats_load(SolStats *st, const CeBlobIO *io)
{
    uint8_t buf[SOL_STATS_FILE_SIZE];         /* the larger of the two versions */
    long n;
    sol_stats_clear(st);
    if (!io || !io->read)
        return 0;
    n = io->read(io->ctx, buf, sizeof buf);
    if (n < 0)
        return 0;
    if (n == 0)
        return 0;                                /* an empty file: nothing yet */
    if ((size_t)n > sizeof buf)
        return -1;
    return sol_stats_deserialize(st, buf, (size_t)n) ? 1 : -1;
}

int sol_stats_save(const SolStats *st, const CeBlobIO *io)
{
    uint8_t buf[SOL_STATS_FILE_SIZE];
    if (!io || !io->write)
        return 0;
    return io->write(io->ctx, buf, sol_stats_serialize(st, buf));
}
