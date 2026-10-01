/*
 * Solitaire HD — statistics model and file (see stats.h).
 */
#include "stats.h"

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

static void note_score(SolModeStats *m, int score, int has_score)
{
    if (has_score && (!m->has_score || score > m->best_score)) {
        m->best_score = score;
        m->has_score = 1;
    }
}

void sol_stats_won(SolStats *st, int mode, int seconds, int score, int has_score)
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
    note_score(m, score, has_score);
}

void sol_stats_lost(SolStats *st, int mode, int score, int has_score)
{
    SolModeStats *m = mode_of(st, mode);
    if (!m)
        return;
    m->streak = m->streak < 0 ? m->streak - 1 : -1;
    if ((uint32_t)-m->streak > m->loss_streak)
        m->loss_streak = (uint32_t)-m->streak;
    note_score(m, score, has_score);
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
    put32(p + 4, 1);
    put32(p + 8, SOL_STATS_MODES);
    p += 12;
    for (int i = 0; i < SOL_STATS_MODES; i++, p += 32) {
        const SolModeStats *m = &st->m[i];
        put32(p, m->played);
        put32(p + 4, m->won);
        put32(p + 8, (uint32_t)m->streak);
        put32(p + 12, m->win_streak);
        put32(p + 16, m->loss_streak);
        put32(p + 20, m->best_time);
        put32(p + 24, (uint32_t)m->best_score);
        put32(p + 28, m->has_score);
    }
    put32(p, ce_crc32(out, (size_t)(p - out)));
    return SOL_STATS_FILE_SIZE;
}

int sol_stats_deserialize(SolStats *st, const uint8_t *data, size_t len)
{
    SolStats t;
    const uint8_t *p;
    if (!data || len != SOL_STATS_FILE_SIZE || memcmp(data, "SOLS", 4) || get32(data + 4) != 1 ||
        get32(data + 8) != SOL_STATS_MODES || ce_crc32(data, len - 4) != get32(data + len - 4))
        return 0;
    p = data + 12;
    for (int i = 0; i < SOL_STATS_MODES; i++, p += 32) {
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
    }
    *st = t;
    return 1;
}

int sol_stats_load(SolStats *st, const CeBlobIO *io)
{
    uint8_t buf[SOL_STATS_FILE_SIZE];
    long n;
    sol_stats_clear(st);
    if (!io || !io->read)
        return 0;
    n = io->read(io->ctx, buf, sizeof buf);
    if (n < 0)
        return 0;
    if (n == 0)
        return 0;                                /* an empty file: nothing yet */
    return sol_stats_deserialize(st, buf, (size_t)n) ? 1 : -1;
}

int sol_stats_save(const SolStats *st, const CeBlobIO *io)
{
    uint8_t buf[SOL_STATS_FILE_SIZE];
    if (!io || !io->write)
        return 0;
    return io->write(io->ctx, buf, sol_stats_serialize(st, buf));
}
