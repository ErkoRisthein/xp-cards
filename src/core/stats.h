/*
 * FreeCell HD — statistics and options model (platform independent).
 *
 * XP semantics (docs/xp-reference/rules.md §8, §9). Values live in a key/value store of 4-byte DWORDs
 * addressed by the XP value names; the Win32 layer maps it onto
 * HKCU\Software\Microsoft\Windows\CurrentVersion\Applets\FreeCell with REG_BINARY 4-byte values, so the
 * statistics are shared with the real XP FreeCell.
 *
 * Value names: won lost wins losses streak stype AlreadyPlayed messages quick dblclick.
 */
#ifndef FC_STATS_H
#define FC_STATS_H

#include <stddef.h>
#include <stdint.h>

typedef struct FcStore {
    void *ctx;
    /* Read value `name`; return 1 and set *value if present, 0 if missing (caller uses its default). */
    int  (*get)(void *ctx, const char *name, uint32_t *value);
    void (*set)(void *ctx, const char *name, uint32_t value);   /* REG_BINARY, 4 bytes, LE */
    void (*del)(void *ctx, const char *name);                   /* RegDeleteValue */
    void (*flush)(void *ctx);                                   /* RegFlushKey (may be NULL) */
    /* Optional first-run migration source: entpack.ini [FreeCell] <key> (GetPrivateProfileInt).
     * Return 1 and set *value, or 0 = not available (that value is then left untouched). May be NULL. */
    int  (*legacy_get)(void *ctx, const char *key, uint32_t *value);
} FcStore;

typedef struct FcStats {
    FcStore  store;          /* copied; all-NULL callbacks = nothing persists, reads give defaults */
    int      last_recorded;  /* game number whose result was recorded last (XP 0x10074E0) */
    uint32_t session_won;    /* in memory only (XP 0x1007808 / 0x1008350) */
    uint32_t session_lost;
} FcStats;

/* Snapshot of everything the Statistics dialog shows. */
typedef struct FcStatsView {
    uint32_t session_won, session_lost;
    uint32_t won, lost, wins, losses, streak, stype;   /* stype: 1 = wins, else losses */
} FcStatsView;

typedef struct FcOptions {
    int messages;   /* "Display messages on illegal moves", default 1 */
    int quick;      /* "Quick play (no animation)",          default 0 */
    int dblclick;   /* "Double click moves card to free cell", default 1 */
} FcOptions;

/* Extras (not in XP), all off by default so the game behaves as XP. They live in our own store
 * (HKCU\Software\xp-cards\FreeCell HD, REG_DWORD values ShowTimeMoves StandardSupermove
 * FullRangeDeals FullScreen WarnUnwinnable AutoFinish), never in XP's key. */
typedef struct FcExtras {
    int show_time_moves;     /* "Show time and moves" in the menu bar */
    int standard_supermove;  /* "Standard multi-card moves": (f+1)*2^e instead of XP's (f+1)(e+1) */
    int full_range;          /* "New Game picks from all 1,000,000 games" instead of XP's 1..32767 */
    int full_screen;         /* window state (Game > Full Screen), remembered with the placement */
    int warn_unwinnable;     /* "Warn when the game can't be won" (v1.2) */
    int auto_finish;         /* "Finish automatically" (v1.2) */
} FcExtras;

/* ---- Statistics --------------------------------------------------------------------------------- */
void     fc_stats_init(FcStats *st, const FcStore *store);   /* last_recorded = 0, session = 0 */
uint32_t fc_stats_get(const FcStats *st, const char *name, uint32_t def);   /* XP RegRead */
void     fc_stats_set(const FcStats *st, const char *name, uint32_t v);

/* One-time migration at startup (InitInstance 0x1001732): if AlreadyPlayed is 0/missing, copy
 * lost won losses wins streak stype from legacy_get into the store, then set AlreadyPlayed = 1.
 * Returns 1 if the migration ran. */
int      fc_stats_migrate(FcStats *st);

/* RecordWin / RecordLoss exactly as XP (§8): a result is never recorded twice in a row for the same
 * game number; losses need game > 0, wins do not. Both set last_recorded = game. */
void     fc_stats_record_win(FcStats *st, int game);
void     fc_stats_record_loss(FcStats *st, int game);

/* Statistics dialog "Clear" (after the 309 confirmation): deletes won lost wins losses streak stype
 * (not AlreadyPlayed) and zeroes the session counters. */
void     fc_stats_clear(FcStats *st);

/* Percent(won, lost), 0x1002330: rounded half up, 100% only with zero losses. */
unsigned fc_stats_percent(uint32_t won, uint32_t lost);

void     fc_stats_view(const FcStats *st, FcStatsView *v);

/* Statistics dialog texts as XP renders them (strings 319/320/321 after wsprintf, so without the stray
 * '%'), for static controls 212, 213, 214. '\t' and '\n' are literal tab / LF (the Win32 layer must
 * expand tabs in the static control, e.g. SS_LEFT with the default tab stops, as XP). */
void     fc_stats_format_session(const FcStatsView *v, char *buf, size_t n);  /* 212 */
void     fc_stats_format_total(const FcStatsView *v, char *buf, size_t n);    /* 213 */
void     fc_stats_format_streaks(const FcStatsView *v, char *buf, size_t n);  /* 214 */
/* "current" streak: "0", "1 win" / "1 loss" (310/311), "%u wins" / "%u losses" (312/313). */
void     fc_stats_format_current(uint32_t streak, uint32_t stype, char *buf, size_t n);

/* ---- Options (§9) -------------------------------------------------------------------------------- */
void     fc_options_default(FcOptions *o);
void     fc_options_load(FcOptions *o, const FcStore *store);
/* Store only non-default values (messages=0, quick=1, dblclick=0), delete the others, then flush.
 * XP does this only on WM_CLOSE. */
void     fc_options_save(const FcOptions *o, const FcStore *store);

/* ---- Extras -------------------------------------------------------------------------------------- */
void     fc_extras_default(FcExtras *x);            /* all 0 */
void     fc_extras_load(FcExtras *x, const FcStore *store);   /* missing values = default */
void     fc_extras_save(const FcExtras *x, const FcStore *store);   /* every value (0/1), then flush */

#endif
