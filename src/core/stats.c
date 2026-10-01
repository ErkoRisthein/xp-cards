/*
 * FreeCell HD — statistics and options model (rules.md §8, §9).
 */
#include "stats.h"

#include <stdio.h>
#include <string.h>

static uint32_t store_get(const FcStore *s, const char *name, uint32_t def)
{
    uint32_t v;
    return s->get && s->get(s->ctx, name, &v) ? v : def;
}

static void store_set(const FcStore *s, const char *name, uint32_t v)
{
    if (s->set) s->set(s->ctx, name, v);
}

static void store_del(const FcStore *s, const char *name)
{
    if (s->del) s->del(s->ctx, name);
}

void fc_stats_init(FcStats *st, const FcStore *store)
{
    memset(st, 0, sizeof *st);
    if (store) st->store = *store;
}

uint32_t fc_stats_get(const FcStats *st, const char *name, uint32_t def) { return store_get(&st->store, name, def); }
void fc_stats_set(const FcStats *st, const char *name, uint32_t v) { store_set(&st->store, name, v); }

int fc_stats_migrate(FcStats *st)
{
    static const char *const keys[] = { "lost", "won", "losses", "wins", "streak", "stype" };
    const FcStore *s = &st->store;
    if (store_get(s, "AlreadyPlayed", 0) != 0) return 0;
    for (int i = 0; i < 6; i++) {
        uint32_t v;
        if (s->legacy_get && s->legacy_get(s->ctx, keys[i], &v)) store_set(s, keys[i], v);
    }
    store_set(s, "AlreadyPlayed", 1);
    return 1;
}

void fc_stats_record_loss(FcStats *st, int game)        /* RecordLoss, 0x1003E12 */
{
    const FcStore *s = &st->store;
    if (game > 0 && game != st->last_recorded) {
        store_set(s, "lost", store_get(s, "lost", 0) + 1);
        st->session_lost++;
        uint32_t streak;
        if (store_get(s, "stype", 1) == 1) { store_set(s, "stype", 0); streak = 1; }
        else streak = store_get(s, "streak", 0) + 1;
        store_set(s, "streak", streak);
        if (store_get(s, "losses", 0) < streak) store_set(s, "losses", streak);
    }
    st->last_recorded = game;
}

void fc_stats_record_win(FcStats *st, int game)         /* win path in CommitMoves, 0x10050D8 */
{
    const FcStore *s = &st->store;
    if (game != st->last_recorded) {                    /* no game > 0 test: -1/-2 wins count */
        store_set(s, "won", store_get(s, "won", 0) + 1);
        st->session_won++;
        uint32_t streak;
        if (store_get(s, "stype", 0) == 0) { store_set(s, "stype", 1); streak = 1; }
        else streak = store_get(s, "streak", 0) + 1;
        store_set(s, "streak", streak);
        if (store_get(s, "wins", 0) < streak) store_set(s, "wins", streak);
    }
    st->last_recorded = game;
}

void fc_stats_clear(FcStats *st)
{
    static const char *const keys[] = { "won", "lost", "wins", "losses", "streak", "stype" };
    for (int i = 0; i < 6; i++) store_del(&st->store, keys[i]);
    st->session_won = st->session_lost = 0;
}

unsigned fc_stats_percent(uint32_t won, uint32_t lost)
{
    uint64_t t = (uint64_t)won + lost;
    if (t == 0) return 0;
    uint64_t p = ((uint64_t)won * 200 + t) / (2 * t);
    if (p >= 100 && lost != 0) p = 99;
    return (unsigned)p;
}

void fc_stats_view(const FcStats *st, FcStatsView *v)
{
    const FcStore *s = &st->store;
    v->session_won = st->session_won;
    v->session_lost = st->session_lost;
    v->won = store_get(s, "won", 0);
    v->lost = store_get(s, "lost", 0);
    v->wins = store_get(s, "wins", 0);
    v->losses = store_get(s, "losses", 0);
    v->streak = store_get(s, "streak", 0);
    v->stype = store_get(s, "stype", 0);
}

void fc_stats_format_session(const FcStatsView *v, char *buf, size_t n)
{
    snprintf(buf, n, "This session\t\t\t%u%%\n\twon:\t\t%u \n\tlost:\t\t%u\n\n",
             fc_stats_percent(v->session_won, v->session_lost), (unsigned)v->session_won,
             (unsigned)v->session_lost);
}

void fc_stats_format_total(const FcStatsView *v, char *buf, size_t n)
{
    snprintf(buf, n, "Total\t\t\t\t%u%%\n\twon:\t\t%u \n\tlost:\t\t%u\n\n",
             fc_stats_percent(v->won, v->lost), (unsigned)v->won, (unsigned)v->lost);
}

void fc_stats_format_current(uint32_t streak, uint32_t stype, char *buf, size_t n)
{
    if (streak == 0) snprintf(buf, n, "0");
    else if (streak == 1) snprintf(buf, n, stype == 1 ? "1 win" : "1 loss");
    else snprintf(buf, n, stype == 1 ? "%u wins" : "%u losses", (unsigned)streak);
}

void fc_stats_format_streaks(const FcStatsView *v, char *buf, size_t n)
{
    char cur[32];
    fc_stats_format_current(v->streak, v->stype, cur, sizeof cur);
    snprintf(buf, n, "Streaks\n\twins:\t\t%u \n\tlosses:\t\t%u \n\tcurrent:\t\t%s",
             (unsigned)v->wins, (unsigned)v->losses, cur);
}

/* ---- Options --------------------------------------------------------------------------------- */

void fc_options_default(FcOptions *o)
{
    o->messages = 1;
    o->quick = 0;
    o->dblclick = 1;
}

void fc_options_load(FcOptions *o, const FcStore *store)    /* 0x1002B37 */
{
    fc_options_default(o);
    if (!store) return;
    o->messages = store_get(store, "messages", 1) != 0;
    o->quick = store_get(store, "quick", 0) != 0;
    o->dblclick = store_get(store, "dblclick", 1) != 0;
}

void fc_options_save(const FcOptions *o, const FcStore *store)   /* 0x1002B90 */
{
    if (!store) return;
    if (o->messages) store_del(store, "messages"); else store_set(store, "messages", 0);
    if (o->quick) store_set(store, "quick", 1); else store_del(store, "quick");
    if (o->dblclick) store_del(store, "dblclick"); else store_set(store, "dblclick", 0);
    if (store->flush) store->flush(store->ctx);
}

/* ---- Extras ------------------------------------------------------------------------------------ */

void fc_extras_default(FcExtras *x)
{
    memset(x, 0, sizeof *x);
}

void fc_extras_load(FcExtras *x, const FcStore *store)
{
    fc_extras_default(x);
    if (!store) return;
    x->show_time_moves = store_get(store, "ShowTimeMoves", 0) != 0;
    x->standard_supermove = store_get(store, "StandardSupermove", 0) != 0;
    x->full_range = store_get(store, "FullRangeDeals", 0) != 0;
    x->full_screen = store_get(store, "FullScreen", 0) != 0;
    x->warn_unwinnable = store_get(store, "WarnUnwinnable", 0) != 0;
    x->auto_finish = store_get(store, "AutoFinish", 0) != 0;
}

void fc_extras_save(const FcExtras *x, const FcStore *store)
{
    if (!store) return;
    store_set(store, "ShowTimeMoves", x->show_time_moves != 0);
    store_set(store, "StandardSupermove", x->standard_supermove != 0);
    store_set(store, "FullRangeDeals", x->full_range != 0);
    store_set(store, "FullScreen", x->full_screen != 0);
    store_set(store, "WarnUnwinnable", x->warn_unwinnable != 0);
    store_set(store, "AutoFinish", x->auto_finish != 0);
    if (store->flush) store->flush(store->ctx);
}
