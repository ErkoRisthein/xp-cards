/*
 * Solitaire HD — the saved game (see savegame.h).
 */
#include "savegame.h"

#include <stdlib.h>
#include <string.h>

#define HEADER 12u                       /* magic, version, payload length */
#define FIXED  (4u + 6u + SOL_PACKED_SIZE + 5u * 4u + 2u * 4u + 2u * 4u)   /* version 1; 2 adds the group */
#define ACT_MIN (7u + SOL_PACKED_SIZE + 3u * 4u)                           /* version 1; 2 adds nauto */
#define VERSION 2u
/* Plausible ranges, so that no saved number can overflow the score arithmetic: a score (Vegas
 * Cumulative carries one across games) and the clock's penalties (Standard -2 per 10 s of the clock,
 * which stops at SOL_TICKS_MAX). */
#define SCORE_LIMIT 100000000
#define PEN_LIMIT   (2 * (SOL_TICKS_MAX / 40 + 1))
static int score_ok(int v) { return v >= -SCORE_LIMIT && v <= SCORE_LIMIT; }
static int pen_ok(int v) { return v >= 0 && v <= PEN_LIMIT; }

/* ---- writing ------------------------------------------------------------------------------------- */

typedef struct Out { uint8_t *p; size_t n; } Out;

static void put8(Out *o, unsigned v) { o->p[o->n++] = (uint8_t)v; }
static void put32(Out *o, uint32_t v)
{
    put8(o, v & 255);
    put8(o, (v >> 8) & 255);
    put8(o, (v >> 16) & 255);
    put8(o, v >> 24);
}
static void put_bytes(Out *o, const void *d, size_t n)
{
    memcpy(o->p + o->n, d, n);
    o->n += n;
}

static size_t act_size(const SolAction *a) { return ACT_MIN + 1u + 2u * a->nsteps + a->nauto; }

static void put_action(Out *o, const SolAction *a)
{
    put8(o, a->type);
    put8(o, a->src);
    put8(o, a->dst);
    put8(o, a->n);
    put8(o, a->nsteps);
    put8(o, a->autoturn);
    put8(o, a->waste_fan);
    put8(o, a->nauto);
    for (int i = 0; i < a->nsteps && i < 52; i++) {
        put8(o, a->steps[i][0]);
        put8(o, a->steps[i][1]);
    }
    for (int i = 0; i < a->nauto && i < SOL_MAX_AUTO; i++)
        put8(o, a->autos[i]);
    put_bytes(o, a->board, SOL_PACKED_SIZE);
    put32(o, (uint32_t)a->score);
    put32(o, (uint32_t)a->recycles);
    put32(o, (uint32_t)a->clock_pen);
}

int sol_game_serialize(const SolSession *s, uint8_t **out, size_t *len)
{
    size_t total = HEADER + FIXED + 4u + 4u;
    int first = 0, nredo = s->nredo, i;
    uint8_t board[SOL_PACKED_SIZE];
    Out o;
    *out = NULL;
    *len = 0;
    if (!s->dealt || s->won)
        return 0;
    for (i = 0; i < nredo; i++)
        total += act_size(&s->redo[i]);
    /* the newest history first: what does not fit leaves out the oldest */
    for (i = s->nhist - 1; i >= 0; i--) {
        if (total + act_size(&s->hist[i]) > SOL_SAVE_MAX_FILE)
            break;
        total += act_size(&s->hist[i]);
    }
    first = i + 1;
    o.p = malloc(total);
    o.n = 0;
    if (!o.p)
        return 0;
    put_bytes(&o, "SOLG", 4);
    put32(&o, VERSION);
    put32(&o, (uint32_t)(total - HEADER - 4u));
    put32(&o, sol_options_pack(&s->opts));
    put8(&o, (unsigned)s->draw);
    put8(&o, (unsigned)s->game_scoring);
    put8(&o, (unsigned)s->waste_fan);
    put8(&o, s->undo_fresh != 0);
    put8(&o, s->counted != 0);
    put8(&o, 0);
    sol_board_pack(&s->board, board);
    put_bytes(&o, board, SOL_PACKED_SIZE);
    put32(&o, (uint32_t)s->score);
    put32(&o, (uint32_t)s->ticks);
    put32(&o, (uint32_t)s->recycles);
    put32(&o, (uint32_t)s->clock_pen);
    put32(&o, (uint32_t)s->carry);
    put32(&o, s->seed);
    put32(&o, s->rng);
    put32(&o, (uint32_t)(s->nhist - first));
    put32(&o, (uint32_t)nredo);
    put32(&o, (uint32_t)(s->redo_group > 0 && s->redo_group <= nredo ? s->redo_group : 0));
    for (i = first; i < s->nhist; i++)
        put_action(&o, &s->hist[i]);
    for (i = 0; i < nredo; i++)
        put_action(&o, &s->redo[i]);
    put32(&o, ce_crc32(o.p + HEADER, o.n - HEADER));
    *out = o.p;
    *len = o.n;
    return 1;
}

/* ---- reading ------------------------------------------------------------------------------------- */

typedef struct In { const uint8_t *p; size_t n, k; int bad; } In;

static unsigned get8(In *in)
{
    if (in->k + 1 > in->n) {
        in->bad = 1;
        return 0;
    }
    return in->p[in->k++];
}
static uint32_t get32(In *in)
{
    uint32_t v = get8(in);
    v |= (uint32_t)get8(in) << 8;
    v |= (uint32_t)get8(in) << 16;
    v |= (uint32_t)get8(in) << 24;
    return v;
}
static void get_bytes(In *in, void *d, size_t n)
{
    if (in->k + n > in->n) {
        in->bad = 1;
        memset(d, 0, n);
        return;
    }
    memcpy(d, in->p + in->k, n);
    in->k += n;
}

static int board_ok(const uint8_t packed[SOL_PACKED_SIZE])
{
    SolBoard b;
    int total = 0;
    for (int p = 0; p < SOL_NPILES; p++)
        total += packed[p];
    if (total != 52)
        return 0;
    sol_board_unpack(&b, packed);
    return sol_board_valid(&b, NULL, 0);
}

/* A valid auto-home step: a turn of a tableau column, or a card from the waste or a column home. */
static int auto_ok(unsigned op)
{
    if (op & SOL_AUTO_TURN)
        return sol_is_tab((int)(op & 0x7F));
    return (int)(op >> 2) == SOL_WASTE || sol_is_tab((int)(op >> 2));
}

static int get_action(In *in, SolAction *a, unsigned version)
{
    memset(a, 0, sizeof *a);
    a->type = (uint8_t)get8(in);
    a->src = (uint8_t)get8(in);
    a->dst = (uint8_t)get8(in);
    a->n = (uint8_t)get8(in);
    a->nsteps = (uint8_t)get8(in);
    a->autoturn = (uint8_t)get8(in);
    a->waste_fan = (uint8_t)get8(in);
    a->nauto = version >= 2 ? (uint8_t)get8(in) : 0;
    if (a->type < SOL_ACT_MOVE || a->type > SOL_ACT_FINISH || a->src >= SOL_NPILES || a->dst >= SOL_NPILES ||
        a->n > 52 || a->nsteps > 52 || a->autoturn >= 128 || a->waste_fan > 3 || a->nauto > SOL_MAX_AUTO)
        return 0;
    for (int i = 0; i < a->nsteps; i++) {
        a->steps[i][0] = (uint8_t)get8(in);
        a->steps[i][1] = (uint8_t)get8(in);
        if (a->steps[i][0] >= SOL_NPILES || a->steps[i][1] >= SOL_NPILES)
            return 0;
    }
    for (int i = 0; i < a->nauto; i++) {
        a->autos[i] = (uint8_t)get8(in);
        if (!auto_ok(a->autos[i]))
            return 0;
    }
    get_bytes(in, a->board, SOL_PACKED_SIZE);
    a->score = (int32_t)get32(in);
    a->recycles = (int32_t)get32(in);
    a->clock_pen = (int32_t)get32(in);
    return !in->bad && board_ok(a->board) && a->recycles >= 0 && a->recycles <= 100000 && score_ok(a->score) &&
           pen_ok(a->clock_pen);
}

int sol_game_restore(SolSession *s, const uint8_t *data, size_t len)
{
    In in;
    SolOptions o, cur;
    uint8_t board[SOL_PACKED_SIZE];
    uint32_t plen, nh, nr, group = 0, i, version;
    int draw, scoring, fan, fresh, counted, score, ticks, recycles, clock_pen, carry, other;
    uint32_t seed, rng;
    SolAction *hist = NULL, *redo = NULL;
    if (!data || len < HEADER + FIXED + 4u || len > SOL_SAVE_MAX_FILE || memcmp(data, "SOLG", 4))
        return SOL_LOAD_DAMAGED;
    in.p = data;
    in.n = len;
    in.k = 4;
    in.bad = 0;
    version = get32(&in);
    if (version != 1 && version != VERSION)
        return SOL_LOAD_DAMAGED;
    plen = get32(&in);
    if ((size_t)plen + HEADER + 4u != len)
        return SOL_LOAD_DAMAGED;
    {
        const uint8_t *c = data + len - 4;
        uint32_t crc = (uint32_t)c[0] | (uint32_t)c[1] << 8 | (uint32_t)c[2] << 16 | (uint32_t)c[3] << 24;
        if (ce_crc32(data + HEADER, plen) != crc)
            return SOL_LOAD_DAMAGED;
    }
    in.n = len - 4;                         /* the payload */
    sol_options_unpack(&o, get32(&in));
    draw = (int)get8(&in);
    scoring = (int)get8(&in);
    fan = (int)get8(&in);
    fresh = (int)get8(&in);
    counted = (int)get8(&in);
    get8(&in);
    get_bytes(&in, board, SOL_PACKED_SIZE);
    score = (int32_t)get32(&in);
    ticks = (int32_t)get32(&in);
    recycles = (int32_t)get32(&in);
    clock_pen = (int32_t)get32(&in);
    carry = (int32_t)get32(&in);
    seed = get32(&in);
    rng = get32(&in);
    nh = get32(&in);
    nr = get32(&in);
    if (version >= 2)
        group = get32(&in);
    if (in.bad || group > nr || (draw != 1 && draw != 3) || scoring < SOL_SCORING_STANDARD || scoring > SOL_SCORING_NONE ||
        fan > 3 || fresh > 1 || counted > 1 || ticks < 0 || ticks > SOL_TICKS_MAX || recycles < 0 ||
        recycles > 100000 || (scoring == SOL_SCORING_VEGAS && recycles > draw - 1) ||
        (scoring == SOL_SCORING_STANDARD && score < 0) || (scoring == SOL_SCORING_NONE && score != 0) ||
        !score_ok(score) || !score_ok(carry) || !pen_ok(clock_pen) ||
        seed > 0x7FFF || o.draw != draw || o.scoring != scoring || !board_ok(board) ||
        nh > len / ACT_MIN || nr > len / ACT_MIN)
        return SOL_LOAD_DAMAGED;
    /* the Options it was played with must still be the current ones; with "Apply option changes to the
     * next game" (2c) it goes on with its own, and the current ones wait for the next deal */
    cur = s->opts;
    other = cur.draw != draw || cur.scoring != scoring || (cur.timed != 0) != (o.timed != 0);
    if (other && !s->extras.next_game_options)
        return SOL_LOAD_OTHER_OPTIONS;
    if (nh) {
        hist = malloc(nh * sizeof *hist);
        if (!hist)
            return SOL_LOAD_DAMAGED;
    }
    if (nr) {
        redo = malloc(nr * sizeof *redo);
        if (!redo) {
            free(hist);
            return SOL_LOAD_DAMAGED;
        }
    }
    for (i = 0; i < nh; i++)
        if (!get_action(&in, &hist[i], version))
            goto bad;
    for (i = 0; i < nr; i++)
        if (!get_action(&in, &redo[i], version))
            goto bad;
    if (in.bad || in.k != in.n)
        goto bad;

    /* install: the old game (if any) is replaced without a result */
    free(s->hist);
    free(s->redo);
    s->hist = hist;
    s->nhist = s->hist_cap = (int)nh;
    s->redo = redo;
    s->nredo = s->redo_cap = (int)nr;
    s->redo_group = (int)group;
    sol_board_unpack(&s->board, board);
    s->waste_fan = fan;
    if (other) {
        s->pend_opts = cur;
        s->pending = 1;
        s->opts.draw = draw;
        s->opts.scoring = scoring;
        s->opts.timed = o.timed != 0;
    } else {
        s->pending = 0;
    }
    s->draw = draw;
    s->game_scoring = scoring;
    s->game_timed = o.timed != 0;
    s->undo_fresh = fresh;
    s->counted = counted;
    s->score = score;
    s->ticks = ticks;
    s->recycles = recycles;
    s->clock_pen = clock_pen;
    s->carry = carry;
    s->seed = seed;
    s->rng = rng;
    sol_restored(s);
    return SOL_LOAD_OK;
bad:
    free(hist);
    free(redo);
    return SOL_LOAD_DAMAGED;
}

/* ---- through a CeBlobIO -------------------------------------------------------------------------- */

int sol_game_clear(const CeBlobIO *io)
{
    static const uint8_t none[1] = { 0 };
    return io && io->write ? io->write(io->ctx, none, 0) : 0;
}

int sol_game_save(const SolSession *s, const CeBlobIO *io)
{
    uint8_t *data;
    size_t len;
    int ok;
    if (!io || !io->write)
        return 0;
    if (!sol_game_serialize(s, &data, &len))
        return sol_game_clear(io);
    ok = io->write(io->ctx, data, len);
    free(data);
    return ok;
}

int sol_game_load(SolSession *s, const CeBlobIO *io)
{
    uint8_t *buf;
    long n;
    int r;
    if (!io || !io->read)
        return SOL_LOAD_NONE;
    buf = malloc(SOL_SAVE_MAX_FILE);
    if (!buf)
        return SOL_LOAD_NONE;
    n = io->read(io->ctx, buf, SOL_SAVE_MAX_FILE);
    if (n <= 0)
        r = SOL_LOAD_NONE;                  /* no file, or the empty file: no saved game */
    else if ((size_t)n > SOL_SAVE_MAX_FILE)
        r = SOL_LOAD_DAMAGED;
    else
        r = sol_game_restore(s, buf, (size_t)n);
    free(buf);
    return r;
}
