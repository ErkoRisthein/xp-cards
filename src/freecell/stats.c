/*
 * FreeCell HD — statistics and options model (rules.md §8, §9).
 */
#include "stats.h"

#include <stdio.h>
#include <string.h>

void fc_stats_init(FcStats *st, const CeStore *store)
{
    memset(st, 0, sizeof *st);
    if (store) st->store = *store;
}

uint32_t fc_stats_get(const FcStats *st, const char *name, uint32_t def) { return ce_store_get(&st->store, name, def); }
void fc_stats_set(const FcStats *st, const char *name, uint32_t v) { ce_store_set(&st->store, name, v); }

int fc_stats_migrate(FcStats *st)
{
    static const char *const keys[] = { "lost", "won", "losses", "wins", "streak", "stype" };
    const CeStore *s = &st->store;
    if (ce_store_get(s, "AlreadyPlayed", 0) != 0) return 0;
    for (int i = 0; i < 6; i++) {
        uint32_t v;
        if (s->legacy_get && s->legacy_get(s->ctx, keys[i], &v)) ce_store_set(s, keys[i], v);
    }
    ce_store_set(s, "AlreadyPlayed", 1);
    return 1;
}

void fc_stats_record_loss(FcStats *st, int game)        /* RecordLoss, 0x1003E12 */
{
    const CeStore *s = &st->store;
    if (game > 0 && game != st->last_recorded) {
        ce_store_set(s, "lost", ce_store_get(s, "lost", 0) + 1);
        st->session_lost++;
        uint32_t streak;
        if (ce_store_get(s, "stype", 1) == 1) { ce_store_set(s, "stype", 0); streak = 1; }
        else streak = ce_store_get(s, "streak", 0) + 1;
        ce_store_set(s, "streak", streak);
        if (ce_store_get(s, "losses", 0) < streak) ce_store_set(s, "losses", streak);
    }
    st->last_recorded = game;
}

void fc_stats_record_win(FcStats *st, int game)         /* win path in CommitMoves, 0x10050D8 */
{
    const CeStore *s = &st->store;
    if (game != st->last_recorded) {                    /* no game > 0 test: -1/-2 wins count */
        ce_store_set(s, "won", ce_store_get(s, "won", 0) + 1);
        st->session_won++;
        uint32_t streak;
        if (ce_store_get(s, "stype", 0) == 0) { ce_store_set(s, "stype", 1); streak = 1; }
        else streak = ce_store_get(s, "streak", 0) + 1;
        ce_store_set(s, "streak", streak);
        if (ce_store_get(s, "wins", 0) < streak) ce_store_set(s, "wins", streak);
    }
    st->last_recorded = game;
}

void fc_stats_clear(FcStats *st)
{
    static const char *const keys[] = { "won", "lost", "wins", "losses", "streak", "stype" };
    for (int i = 0; i < 6; i++) ce_store_del(&st->store, keys[i]);
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
    const CeStore *s = &st->store;
    v->session_won = st->session_won;
    v->session_lost = st->session_lost;
    v->won = ce_store_get(s, "won", 0);
    v->lost = ce_store_get(s, "lost", 0);
    v->wins = ce_store_get(s, "wins", 0);
    v->losses = ce_store_get(s, "losses", 0);
    v->streak = ce_store_get(s, "streak", 0);
    v->stype = ce_store_get(s, "stype", 0);
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

void fc_options_load(FcOptions *o, const CeStore *store)    /* 0x1002B37 */
{
    fc_options_default(o);
    if (!store) return;
    o->messages = ce_store_get(store, "messages", 1) != 0;
    o->quick = ce_store_get(store, "quick", 0) != 0;
    o->dblclick = ce_store_get(store, "dblclick", 1) != 0;
}

void fc_options_save(const FcOptions *o, const CeStore *store)   /* 0x1002B90 */
{
    if (!store) return;
    if (o->messages) ce_store_del(store, "messages"); else ce_store_set(store, "messages", 0);
    if (o->quick) ce_store_set(store, "quick", 1); else ce_store_del(store, "quick");
    if (o->dblclick) ce_store_del(store, "dblclick"); else ce_store_set(store, "dblclick", 0);
    if (store->flush) store->flush(store->ctx);
}

/* ---- Extras ------------------------------------------------------------------------------------ */

void fc_extras_default(FcExtras *x)
{
    memset(x, 0, sizeof *x);
}

void fc_extras_load(FcExtras *x, const CeStore *store)
{
    fc_extras_default(x);
    if (!store) return;
    x->show_time_moves = ce_store_get(store, "ShowTimeMoves", 0) != 0;
    x->standard_supermove = ce_store_get(store, "StandardSupermove", 0) != 0;
    x->full_range = ce_store_get(store, "FullRangeDeals", 0) != 0;
    x->full_screen = ce_store_get(store, "FullScreen", 0) != 0;
    x->warn_unwinnable = ce_store_get(store, "WarnUnwinnable", 0) != 0;
    x->auto_finish = ce_store_get(store, "AutoFinish", 0) != 0;
    x->single_click = ce_store_get(store, "SingleClick", 0) != 0;
    x->drag_drop = ce_store_get(store, "DragDrop", 0) != 0;
    x->enhanced_anim = ce_store_get(store, "EnhancedAnimations", 0) != 0;
    x->large_print = ce_store_get(store, "LargePrint", 0) != 0;
}

void fc_extras_save(const FcExtras *x, const CeStore *store)
{
    if (!store) return;
    ce_store_set(store, "ShowTimeMoves", x->show_time_moves != 0);
    ce_store_set(store, "StandardSupermove", x->standard_supermove != 0);
    ce_store_set(store, "FullRangeDeals", x->full_range != 0);
    ce_store_set(store, "FullScreen", x->full_screen != 0);
    ce_store_set(store, "WarnUnwinnable", x->warn_unwinnable != 0);
    ce_store_set(store, "AutoFinish", x->auto_finish != 0);
    ce_store_set(store, "SingleClick", x->single_click != 0);
    ce_store_set(store, "DragDrop", x->drag_drop != 0);
    ce_store_set(store, "EnhancedAnimations", x->enhanced_anim != 0);
    ce_store_set(store, "LargePrint", x->large_print != 0);
    if (store->flush) store->flush(store->ctx);
}
