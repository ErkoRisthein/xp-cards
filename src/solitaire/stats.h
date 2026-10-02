/*
 * Solitaire HD — statistics (an extra; XP Solitaire kept none). Platform independent.
 *
 * Kept per mode: Draw One / Draw Three x Standard / Vegas / None scoring (6 modes; Vegas with and
 * without Cumulative is one mode, Timed game is not part of the mode). A game counts as played once
 * an action was made (a draw, a move, a card turned over); it is won by the win cascade (also the
 * Alt+Shift+2 cheat's, as XP FreeCell's cheat counts) and lost when it is left unfinished: a new deal,
 * an Options change that deals, Exit (unless "Save game on exit" keeps it for the next start).
 *
 *   played, won        win % = won / played, rounded half up, 100% only when every game was won
 *   current streak     wins (> 0) or losses (< 0) in a row
 *   longest streaks    of wins and of losses
 *   best time          the fastest won Timed game, in seconds (0 = none)
 *   best score         the highest final score of any played game (Standard: with the time bonus;
 *                      Vegas: the game's own result, without the Cumulative carry-over); none with
 *                      None scoring
 *
 * File (%APPDATA%\xp-cards\Solitaire HD\statistics.bin through a CeBlobIO; all integers little-endian):
 *   "SOLS", u32 version 1, u32 modes (6), then per mode 8 x u32: played won streak(i32) win_streak
 *   loss_streak best_time best_score(i32) has_score; then u32 CRC-32 of everything before it.
 * A damaged file (magic, version, size, CRC, won > played) is rejected as a whole.
 */
#ifndef SOL_STATS_H
#define SOL_STATS_H

#include <stddef.h>
#include <stdint.h>

#include "engine/store.h"

#define SOL_STATS_MODES 6
#define SOL_STATS_FILE_SIZE (12u + SOL_STATS_MODES * 32u + 4u)

typedef struct SolModeStats {
    uint32_t played, won;
    int32_t  streak;              /* > 0 wins in a row, < 0 losses in a row */
    uint32_t win_streak;          /* longest */
    uint32_t loss_streak;         /* longest */
    uint32_t best_time;           /* seconds, 0 = no timed win yet */
    int32_t  best_score;
    uint32_t has_score;           /* best_score is set */
} SolModeStats;

typedef struct SolStats {
    SolModeStats m[SOL_STATS_MODES];
} SolStats;

/* The mode of a game: draw 1 or 3, scoring SOL_SCORING_* -> 0..5 (draw one: 0 Standard, 1 Vegas,
 * 2 None; draw three: 3, 4, 5). */
int  sol_stats_mode(int draw, int scoring);
void sol_stats_clear(SolStats *st);

void sol_stats_played(SolStats *st, int mode);
/* A won game: seconds = the clock (0 when the game was not timed: no best time), score = the final
 * score (has_score 0 with None scoring). */
void sol_stats_won(SolStats *st, int mode, int seconds, int score, int has_score);
void sol_stats_lost(SolStats *st, int mode, int score, int has_score);

/* Win %, as the dialog shows it. */
unsigned sol_stats_percent(const SolModeStats *m);

/* The dialog's values, one per line ('\n'): games played, games won, win %, current streak ("0",
 * "1 win", "3 wins", "1 loss", "2 losses"), longest winning streak, longest losing streak, best time
 * ("m:ss", or "-"), best score ("-" with None or none yet; Vegas as the status bar shows money, with
 * iCurrency). */
void sol_stats_format(const SolModeStats *m, int scoring, int icurrency, char *buf, size_t n);

size_t sol_stats_serialize(const SolStats *st, uint8_t out[SOL_STATS_FILE_SIZE]);
int    sol_stats_deserialize(SolStats *st, const uint8_t *data, size_t len);   /* 1, or 0 (st unchanged) */
/* Load: 1 = loaded, 0 = no file (empty statistics), -1 = damaged (empty statistics). */
int    sol_stats_load(SolStats *st, const CeBlobIO *io);
int    sol_stats_save(const SolStats *st, const CeBlobIO *io);    /* 1 on success */

#endif
