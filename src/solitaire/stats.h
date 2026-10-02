/*
 * Solitaire HD — statistics (an extra; XP Solitaire kept none). Platform independent.
 *
 * Kept per mode: Draw One / Draw Three x Standard / Vegas / None scoring (6 modes; Vegas with and
 * without Cumulative is one mode, Timed game is not part of the mode). A game counts as played once
 * an action was made (a draw, a move, a card turned over); it is won by the win cascade (also the
 * Alt+Shift+2 cheat's, as XP FreeCell's cheat counts) and lost when it is left unfinished: a new deal,
 * an Options change that deals, Exit (unless "Save game on exit" keeps it for the next start), "End
 * Game" in the No More Moves question (2c).
 *
 *   played, won        win % = won / played, rounded half up, 100% only when every game was won
 *   current streak     wins (> 0) or losses (< 0) in a row
 *   longest streaks    of wins and of losses
 *   best time          the fastest won Timed game, in seconds (0 = none)
 *   best score         the highest final score of any played game (Standard: with the time bonus;
 *                      Vegas: the game's own result, without the Cumulative carry-over); none with
 *                      None scoring
 *   high scores (2c)   the five highest final scores of the mode's games (won or lost, as the best
 *                      score), each with its date (the local date the game ended, YYYYMMDD; 0 unknown),
 *                      best first, an equal score below the older one. Standard keeps two tables, Timed
 *                      games and games not timed (Windows 7's "Standard Timed" / "Standard Non-Timed");
 *                      Vegas one (the game's own result); None none.
 *   money (2c, Vegas)  most money won (the best game result), most money lost (the worst), current
 *                      winnings (the sum of every game's result since the statistics were reset)
 *
 * File (%APPDATA%\xp-cards\Solitaire HD\statistics.bin through a CeBlobIO; all integers little-endian):
 *   "SOLS", u32 version 2, u32 modes (6), then per mode: 8 x u32 played won streak(i32) win_streak
 *   loss_streak best_time best_score(i32) has_score; u32 high scores kept in table 0 and table 1 (0..5);
 *   2 x 5 x (i32 score, u32 date) (unused entries zero); u32 has_money, i32 most_won, most_lost,
 *   winnings; then u32 CRC-32 of everything before it (832 bytes).
 *   Version 1 (Solitaire HD 1.1 / 1.2: the 8 values per mode only, 208 bytes) is read too: the high
 *   scores and the money start empty, and the next write is version 2.
 * A damaged file (magic, version, size, CRC, won > played, a table out of order, a date that is no date)
 * is rejected as a whole.
 */
#ifndef SOL_STATS_H
#define SOL_STATS_H

#include <stddef.h>
#include <stdint.h>

#include "engine/store.h"

#define SOL_STATS_MODES 6
#define SOL_STATS_TOP   5
#define SOL_STATS_FILE_SIZE_V1 (12u + SOL_STATS_MODES * 32u + 4u)
#define SOL_STATS_FILE_SIZE    (12u + SOL_STATS_MODES * 136u + 4u)
enum { SOL_TOP_UNTIMED = 0, SOL_TOP_TIMED = 1 };   /* the two Standard tables (Vegas uses 0) */

typedef struct SolHighScore {
    int32_t  score;
    uint32_t date;                /* YYYYMMDD (local), 0 = unknown */
} SolHighScore;

typedef struct SolModeStats {
    uint32_t played, won;
    int32_t  streak;              /* > 0 wins in a row, < 0 losses in a row */
    uint32_t win_streak;          /* longest */
    uint32_t loss_streak;         /* longest */
    uint32_t best_time;           /* seconds, 0 = no timed win yet */
    int32_t  best_score;
    uint32_t has_score;           /* best_score is set */
    /* version 2 (2c) */
    uint32_t ntop[2];             /* entries used in top[0] / top[1] */
    SolHighScore top[2][SOL_STATS_TOP];   /* best first */
    uint32_t has_money;           /* Vegas: a game has a result */
    int32_t  most_won, most_lost; /* Vegas: the best and the worst game result */
    int32_t  winnings;            /* Vegas: the sum of the results (saturating) */
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
 * score (has_score 0 with None scoring), timed = the game was timed (the Standard table it goes to),
 * date = the day it ended (YYYYMMDD, 0 = unknown). */
void sol_stats_won(SolStats *st, int mode, int seconds, int score, int has_score, int timed, uint32_t date);
void sol_stats_lost(SolStats *st, int mode, int score, int has_score, int timed, uint32_t date);
/* The scoring a mode is kept for: SOL_SCORING_STANDARD / _VEGAS / _NONE. */
int  sol_stats_mode_scoring(int mode);
/* The date as plain text: "2026-10-02" ("" for 0). The Win32 dialog shows the locale's short date. */
void sol_stats_date_text(uint32_t date, char *buf, size_t n);
int  sol_stats_date_ok(uint32_t date);      /* 0, or a plausible YYYYMMDD */

/* Win %, as the dialog shows it. */
unsigned sol_stats_percent(const SolModeStats *m);

/* The dialog's values, one per line ('\n'): games played, games won, win %, current streak ("0",
 * "1 win", "3 wins", "1 loss", "2 losses"), longest winning streak, longest losing streak, best time
 * ("m:ss", or "-"), best score ("-" with None or none yet; Vegas as the status bar shows money, with
 * iCurrency). */
void sol_stats_format(const SolModeStats *m, int scoring, int icurrency, char *buf, size_t n);
/* The dialog's "High Scores" box (2c), one entry per line ('\n'), in three columns: labels, values
 * (scores and money as the status bar shows them, "-" for an empty place) and dates[] (one per line,
 * 0 = none; the UI formats them). Standard: "Timed games:", 1. .. 5., "Not timed:", 1. .. 5. (12 lines);
 * Vegas: "High scores:", 1. .. 5., "Most money won:", "Most money lost:", "Current winnings:" (9 lines;
 * money "-" until a Vegas game has a result; the amount lost shown as a positive amount); None: one
 * line, "No scores with None scoring.". Returns the number of lines (at most SOL_STATS_TOP_LINES). */
#define SOL_STATS_TOP_LINES 12
int  sol_stats_format_top(const SolModeStats *m, int scoring, int icurrency, char *labels, size_t nl,
                          char *values, size_t nv, uint32_t dates[SOL_STATS_TOP_LINES]);

size_t sol_stats_serialize(const SolStats *st, uint8_t out[SOL_STATS_FILE_SIZE]);
/* Version 2 or version 1 (migrated: the new values empty). Returns the version read, or 0 (st unchanged). */
int    sol_stats_deserialize(SolStats *st, const uint8_t *data, size_t len);
/* Load: 1 = loaded (either version), 0 = no file (empty statistics), -1 = damaged (empty statistics). */
int    sol_stats_load(SolStats *st, const CeBlobIO *io);
int    sol_stats_save(const SolStats *st, const CeBlobIO *io);    /* 1 on success */

#endif
