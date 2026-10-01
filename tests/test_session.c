/*
 * FreeCell HD — native tests of the XP controller (src/core/session.c) and the statistics/options
 * model (src/core/stats.c), driven through a scripted fake UI. Plus a randomized playout with
 * invariants and full undo back to the deal.
 */
#include "core/session.h"
#include "core/stats.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (a), _b = (b); checks++; if (_a != _b) { fails++; \
    printf("FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); checks++; if (strcmp(_a, _b)) { fails++; \
    printf("FAIL %s:%d: %s == \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

/* ---- card helpers ---------------------------------------------------------------------------------- */

static Card C(const char *s)
{
    const char *ranks = "A23456789TJQK", *suits = "CDHS";
    return (int)(strchr(ranks, s[0]) - ranks) * 4 + (int)(strchr(suits, s[1]) - suits);
}

static void set_col(FcBoard *b, int col, const char *cards)
{
    int i = 0;
    for (int k = 0; k < FC_COLLEN; k++) b->board[col][k] = FC_EMPTY;
    while (*cards) {
        while (*cards == ' ') cards++;
        if (!*cards) break;
        b->board[col][i++] = C(cards);
        cards += 2;
    }
}

static void set_home(FcBoard *b, int slot, const char *card)
{
    Card c = C(card);
    b->board[0][slot] = c;
    b->home_rank[fc_suit(c)] = (int8_t)fc_rank(c);
    b->suit_home_slot[fc_suit(c)] = (int8_t)slot;
}

static void fix_cards_left(FcBoard *b)
{
    int n = 0;
    for (int i = 0; i < 4; i++) n += b->board[0][i] != FC_EMPTY;
    for (int c = 1; c <= 8; c++) n += fc_last_index(b, c) + 1;
    b->cards_left = n;
}

/* ---- fake registry ------------------------------------------------------------------------------- */

typedef struct Reg {
    struct { char name[32]; uint32_t v; } val[32];
    int n, nflush;
    int legacy_ok;
    uint32_t legacy[6];      /* lost won losses wins streak stype */
} Reg;

static int reg_find(Reg *r, const char *name)
{
    for (int i = 0; i < r->n; i++) if (!strcmp(r->val[i].name, name)) return i;
    return -1;
}

static int reg_get(void *ctx, const char *name, uint32_t *v)
{
    Reg *r = ctx;
    int i = reg_find(r, name);
    if (i < 0) return 0;
    *v = r->val[i].v;
    return 1;
}

static void reg_set(void *ctx, const char *name, uint32_t v)
{
    Reg *r = ctx;
    int i = reg_find(r, name);
    if (i < 0) { i = r->n++; snprintf(r->val[i].name, sizeof r->val[i].name, "%s", name); }
    r->val[i].v = v;
}

static void reg_del(void *ctx, const char *name)
{
    Reg *r = ctx;
    int i = reg_find(r, name);
    if (i >= 0) r->val[i] = r->val[--r->n];
}

static void reg_flush(void *ctx) { ((Reg *)ctx)->nflush++; }

static int reg_legacy(void *ctx, const char *key, uint32_t *v)
{
    static const char *const keys[6] = { "lost", "won", "losses", "wins", "streak", "stype" };
    Reg *r = ctx;
    if (!r->legacy_ok) return 0;
    for (int i = 0; i < 6; i++) if (!strcmp(keys[i], key)) { *v = r->legacy[i]; return 1; }
    return 0;
}

static long rv(Reg *r, const char *name)   /* value or -1 when missing */
{
    int i = reg_find(r, name);
    return i < 0 ? -1 : (long)r->val[i].v;
}

static FcStore make_store(Reg *r)
{
    FcStore s = { r, reg_get, reg_set, reg_del, reg_flush, reg_legacy };
    return s;
}

/* ---- fake UI ----------------------------------------------------------------------------------------- */

#define GN_CANCEL 0x7fffffff

typedef struct Fake {
    FcSession *s;
    char log[16384];
    /* scripted answers */
    int resign;                  /* confirm_resign */
    int movecol;
    int gn[8], ngn, gni;         /* ask_game_number answers (value or GN_CANCEL) */
    int gn_init[8];              /* initial values the session showed */
    int win_yes, win_sel;        /* -1: leave the checkbox as is */
    int lose_yes, lose_same;
    int cheat;
    /* observations */
    int posted[8], nposted;
    uint32_t now;
    int cards_left, undo_en, restart_en, redo_en, nmenu;
    char title[64];
    int timer[4];
    int nflash_on, nflash_off;
    int nfwd, nback, ninval;
    int nmsg, last_msg_id;
    char last_msg[200];
    int nresign, nmovecol, nwin, nlose, ncheat, win_sel_in, lose_same_in;
    int cl_seq[64], ncl;         /* cards_left_changed values */
    int check_replay;            /* verify board consistency on every animated step */
    int ntitle;                  /* set_title calls = deals */
} Fake;

static void logf_(Fake *f, const char *fmt, ...)
{
    size_t n = strlen(f->log);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(f->log + n, sizeof f->log - n, fmt, ap);
    va_end(ap);
}

static void ui_message(void *c, int id, const char *t)
{
    Fake *f = c;
    f->nmsg++; f->last_msg_id = id;
    snprintf(f->last_msg, sizeof f->last_msg, "%s", t);
    logf_(f, "msg%d;", id);
}
static int ui_resign(void *c) { Fake *f = c; f->nresign++; logf_(f, "resign?;"); return f->resign; }
static int ui_movecol(void *c) { Fake *f = c; f->nmovecol++; logf_(f, "movecol?;"); return f->movecol; }
static int ui_gamenum(void *c, int init, int *v)
{
    Fake *f = c;
    logf_(f, "gamenum(%d);", init);
    if (f->gni < 8) f->gn_init[f->gni] = init;
    if (f->gni >= f->ngn) return 0;
    int a = f->gn[f->gni++];
    if (a == GN_CANCEL) return 0;
    *v = a;
    return 1;
}
static int ui_win(void *c, int *sel)
{
    Fake *f = c;
    f->nwin++; f->win_sel_in = *sel;
    logf_(f, "youwin(%d);", *sel);
    if (f->win_sel >= 0) *sel = f->win_sel;
    return f->win_yes;
}
static int ui_lose(void *c, int *same)
{
    Fake *f = c;
    f->nlose++; f->lose_same_in = *same;
    logf_(f, "youlose(%d);", *same);
    if (f->lose_same >= 0) *same = f->lose_same;
    return f->lose_yes;
}
static int ui_cheat(void *c) { Fake *f = c; f->ncheat++; return f->cheat; }
static int board_ok(const FcBoard *b);
static void ui_anim(void *c, const FcStep *st, int fwd)
{
    Fake *f = c;
    if (fwd) f->nfwd++; else f->nback++;
    if (f->check_replay) {
        /* the board shows the state before the step: the card is where the step says */
        const FcBoard *b = &f->s->board;
        Card at = fwd ? b->board[st->src_col][st->src_pos] : b->board[st->dst_col][st->dst_pos];
        if (at != st->card || !board_ok(b)) { fails++; printf("FAIL replay step card mismatch\n"); }
    }
}
static void ui_inval(void *c) { ((Fake *)c)->ninval++; }
static void ui_title(void *c, const char *t) { Fake *f = c; f->ntitle++; snprintf(f->title, sizeof f->title, "%s", t); }
static void ui_cards_left(void *c, int n)
{
    Fake *f = c;
    f->cards_left = n;
    if (f->ncl < 64) f->cl_seq[f->ncl++] = n;
}
static void ui_menu(void *c, int u, int r, int rd)
{
    Fake *f = c;
    f->undo_en = u; f->restart_en = r; f->redo_en = rd; f->nmenu++;
}
static void ui_timer(void *c, int id, int ms) { Fake *f = c; f->timer[id & 3] = ms; logf_(f, "timer%d=%d;", id, ms); }
static void ui_flash(void *c, int on) { Fake *f = c; if (on) f->nflash_on++; else f->nflash_off++; }
static void ui_post(void *c, int cmd) { Fake *f = c; if (f->nposted < 8) f->posted[f->nposted++] = cmd; logf_(f, "post%d;", cmd); }
static uint32_t ui_now(void *c) { return ((Fake *)c)->now; }

static FcSessionUI fake_ui(Fake *f)
{
    FcSessionUI u = { f, ui_message, ui_resign, ui_movecol, ui_gamenum, ui_win, ui_lose, ui_cheat,
                      ui_anim, ui_inval, ui_title, ui_cards_left, ui_menu, ui_timer, ui_flash, ui_post,
                      ui_now };
    return u;
}

static void fake_reset(Fake *f, FcSession *s)
{
    memset(f, 0, sizeof *f);
    f->s = s;
    f->resign = 1;
    f->movecol = FCS_MOVECOL_COLUMN;
    f->win_sel = -1;
    f->lose_same = -1;
    f->now = 1700000000u;
}

static void clear_log(Fake *f) { f->log[0] = 0; }

/* Start game n via Select Game (fake answers n). */
static void start_game(FcSession *s, Fake *f, int n)
{
    f->gn[0] = n; f->ngn = 1; f->gni = 0;
    fcs_command(s, FCS_CMD_SELECT);
    f->ngn = f->gni = 0;
}

/* Run the follow-up commands the session posted. */
static void run_posted(FcSession *s, Fake *f)
{
    int n = f->nposted, cmds[8];
    memcpy(cmds, f->posted, sizeof cmds);
    f->nposted = 0;
    for (int i = 0; i < n; i++) fcs_command(s, cmds[i]);
}

/* Board consistency: 52 distinct cards (homes count as their whole pile), home piles consistent,
 * cards_left consistent, columns contiguous. Returns 1 if fine. */
static int board_ok(const FcBoard *b)
{
    int seen[52] = { 0 }, n = 0;
    for (int s = 0; s < 4; s++) {
        int r = b->home_rank[s], slot = b->suit_home_slot[s];
        if (r < -1 || r > 12) return 0;
        if ((r == -1) != (slot == -1)) return 0;
        if (r >= 0) {
            if (slot < 4 || slot > 7 || b->board[0][slot] != r * 4 + s) return 0;
            for (int k = 0; k <= r; k++) seen[k * 4 + s]++;
        }
        n += r + 1;
    }
    for (int i = 4; i < 8; i++) {
        Card c = b->board[0][i];
        if (c != FC_EMPTY && b->suit_home_slot[fc_suit(c)] != i) return 0;
    }
    for (int i = 0; i < 4; i++) if (b->board[0][i] != FC_EMPTY) seen[b->board[0][i]]++;
    for (int c = 1; c <= 8; c++) {
        int ended = 0;
        for (int i = 0; i < FC_COLLEN; i++) {
            Card x = b->board[c][i];
            if (x == FC_EMPTY) { ended = 1; continue; }
            if (ended || x < 0 || x > 51) return 0;
            seen[x]++;
        }
    }
    for (int i = 0; i < 52; i++) if (seen[i] != 1) return 0;
    return b->cards_left == 52 - n;
}

static int board_eq(const FcBoard *x, const FcBoard *y) { return memcmp(x, y, sizeof *x) == 0; }

/* ---- scenarios ---------------------------------------------------------------------------------- */

/* A small custom position (not all 52 cards) on top of an active game. */
static void custom(FcSession *s, const char *const cols[8], const char *const fcs[4])
{
    fc_board_clear(&s->board);
    for (int c = 1; c <= 8; c++) set_col(&s->board, c, cols[c - 1] ? cols[c - 1] : "");
    for (int i = 0; i < 4; i++) s->board.board[0][i] = fcs && fcs[i] ? C(fcs[i]) : FC_EMPTY;
    fix_cards_left(&s->board);
}

static void test_startup_and_deal(void)
{
    FcSession s; Fake f; Reg r = { 0 };
    FcStore st = make_store(&r);
    fake_reset(&f, &s);
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, &st);
    CHECK_EQ(s.game_number, 0);
    CHECK_EQ(s.board.cards_left, 0);
    CHECK(!fcs_undo_enabled(&s));
    CHECK(!fcs_restart_enabled(&s));
    FcsViewState v;
    fcs_view_state(&s, &v);
    CHECK_EQ(v.no_game, 1);
    CHECK_EQ(v.king, FCS_KING_RIGHT);
    fcs_click(&s, 1, 0);                        /* no game: ignored */
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(rv(&r, "AlreadyPlayed"), 1);       /* migration ran (no legacy values) */
    CHECK_EQ(rv(&r, "won"), -1);

    start_game(&s, &f, 617);
    CHECK_STR(f.title, "FreeCell Game #617");
    CHECK_EQ(f.cards_left, 52);
    CHECK_EQ(f.restart_en, 1);
    CHECK_EQ(f.undo_en, 0);
    CHECK_EQ(s.in_progress, 1);
    CHECK_EQ(s.select_flag, 1);
    CHECK_EQ(f.gn_init[0], fc_random_game_number(1700000000u));   /* default = RandomGameNumber */
    FcBoard d;
    fc_deal(&d, 617);
    CHECK(board_eq(&s.board, &d));              /* no autoplay after the deal */
    fcs_free(&s);
}

static void test_select_move_undo(void)
{
    FcSession s; Fake f;
    fake_reset(&f, &s);
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, NULL);
    start_game(&s, &f, 5);
    static const char *const cols[8] = { "KH 4S", "QD 5H", "KS", "KC", "KD", "QS", "QC", "JH" };
    custom(&s, cols, NULL);
    FcBoard start = s.board;

    fcs_click(&s, 1, 0);                        /* any card of a column selects its bottom card */
    CHECK_EQ(s.sel, 1); CHECK_EQ(s.sel_col, 1); CHECK_EQ(s.sel_pos, 1);
    CHECK(fcs_has_selection(&s));
    fcs_click(&s, 2, 1);                        /* 4S onto 5H */
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(s.board.board[2][2], C("4S"));
    CHECK_EQ(s.nhist, 1);
    CHECK_EQ(f.nfwd, 1);
    CHECK_EQ(f.undo_en, 1);

    fcs_click(&s, 1, 0);                        /* select KH, click it again: deselect */
    CHECK_EQ(s.sel, 1);
    CHECK_EQ(f.undo_en, 1);                     /* a selecting click keeps the history */
    fcs_click(&s, 1, 0);
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(s.nhist, 1);                       /* a pure deselect adds nothing */
    fcs_click(&s, FCS_MISS, 0);                 /* miss with nothing selected: nothing */
    CHECK_EQ(s.sel, 0);
    fcs_click(&s, 3, 0);
    fcs_click(&s, FCS_MISS, 0);                 /* miss with a selection: deselect */
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(s.nhist, 1);
    fcs_click(&s, 0, 2);                        /* empty free cell: not selectable */
    CHECK_EQ(s.sel, 0);

    /* free cell -> free cell and free cell -> column are undoable */
    fcs_click(&s, 8, 0);                        /* JH */
    fcs_click(&s, 0, 0);
    CHECK_EQ(s.board.board[0][0], C("JH"));
    CHECK_EQ(s.king, FCS_KING_LEFT);            /* a move into a free cell turns the king left */
    fcs_click(&s, 0, 0);
    CHECK_EQ(s.sel_col, 0);
    fcs_click(&s, 0, 3);
    CHECK_EQ(s.board.board[0][3], C("JH"));
    CHECK_EQ(s.nhist, 3);
    fcs_click(&s, 0, 3);
    fcs_click(&s, 4, 0);                        /* JH onto KC: illegal (messages on) */
    CHECK_EQ(f.nmsg, 1);
    CHECK_EQ(f.last_msg_id, 306);
    CHECK_STR(f.last_msg, "That move is not allowed.");
    CHECK_EQ(s.sel, 0);
    fcs_click(&s, 0, 3);
    fcs_click(&s, 6, 0);                        /* JH onto QS */
    CHECK_EQ(s.board.board[6][1], C("JH"));
    CHECK_EQ(s.nhist, 4);

    /* free cell -> home is undoable too; the king turns right on a move home */
    s.board.board[0][1] = C("AH");
    fix_cards_left(&s.board);
    FcBoard pre = s.board;
    fcs_click(&s, 0, 1);
    fcs_click(&s, 0, 6);
    CHECK_EQ(s.board.board[0][6], C("AH"));
    CHECK_EQ(s.board.suit_home_slot[2], 6);
    CHECK_EQ(s.king, FCS_KING_RIGHT);
    CHECK_EQ(s.nhist, 5);
    fcs_command(&s, FCS_CMD_UNDO);
    CHECK(board_eq(&s.board, &pre));
    CHECK_EQ(s.board.suit_home_slot[2], -1);
    s.board.board[0][1] = FC_EMPTY;
    fix_cards_left(&s.board);

    /* unlimited undo back to the start, selection dropped first */
    fcs_click(&s, 2, 0);
    CHECK_EQ(s.sel, 1);
    for (int i = 0; i < 4; i++) fcs_command(&s, FCS_CMD_UNDO);
    CHECK_EQ(s.sel, 0);
    CHECK(board_eq(&s.board, &start));
    CHECK_EQ(s.nhist, 0);
    CHECK_EQ(f.undo_en, 0);
    CHECK_EQ(f.nback, 5);
    fcs_command(&s, FCS_CMD_UNDO);              /* nothing left */
    CHECK(board_eq(&s.board, &start));
    fcs_free(&s);
}

static void test_messages_on_off(void)
{
    static const char *const cols[8] = { "KH 4S", "QD 5H", "KD AC", "KS", "QH", "QS", "QC", "JH" };
    for (int msgs = 0; msgs <= 1; msgs++) {
        FcSession s; Fake f;
        fake_reset(&f, &s);
        FcSessionUI ui = fake_ui(&f);
        fcs_init(&s, &ui, NULL);
        s.opts.messages = msgs;
        start_game(&s, &f, 5);
        custom(&s, cols, NULL);
        fcs_click(&s, 1, 1);                    /* 4S */
        fcs_click(&s, 4, 0);                    /* onto KS: CardsToMove = 0 */
        if (!msgs) {
            CHECK_EQ(f.nmsg, 0);
            CHECK_EQ(s.sel, 1);                 /* silent, selection kept */
            CHECK_EQ(s.board.board[3][1], C("AC"));   /* no autoplay */
            CHECK_EQ(s.nhist, 0);
        } else {
            CHECK_EQ(f.nmsg, 1);
            CHECK_EQ(s.sel, 0);
            CHECK_EQ(s.board.board[0][4], C("AC"));   /* deselect commits: autoplay */
            CHECK_EQ(s.nhist, 1);
            CHECK_EQ(f.cl_seq[f.ncl - 1], s.board.cards_left);
        }
        /* free cell target occupied */
        s.board.board[0][0] = C("KS");
        s.board.board[4][0] = FC_EMPTY;
        fix_cards_left(&s.board);
        if (s.sel) fcs_click(&s, FCS_MISS, 0);
        fcs_click(&s, 8, 0);
        fcs_click(&s, 0, 0);
        CHECK_EQ(s.sel, !msgs);
        CHECK_EQ(f.nmsg, msgs ? 2 : 0);
        fcs_free(&s);
    }
}

/* §2.2 capacity table with the Wine-verified texts (missing space fixed). */
static void test_capacity_messages(void)
{
    static const struct { int f, e; const char *msg; } t[] = {
        { 1, 2, "That move requires moving 7 cards. You only have enough free space to move 6." },
        { 1, 1, "That move requires moving 7 cards. You only have enough free space to move 4." },
        { 4, 0, "That move requires moving 7 cards. You only have enough free space to move 5." },
        { 3, 1, NULL } };
    static const char *const fill[4] = { "KS", "KH", "KD", "KC" };
    static const char *const fillc[6] = { "QS", "QH", "QD", "QC", "JS", "JH" };
    for (int i = 0; i < 4; i++) {
        FcSession s; Fake f;
        fake_reset(&f, &s);
        FcSessionUI ui = fake_ui(&f);
        fcs_init(&s, &ui, NULL);
        start_game(&s, &f, 5);
        const char *cols[8] = { "9H 8S 7H 6S 5H 4S 3H", "TC" }, *fcs[4] = { 0 };
        for (int k = 0; k < 4 - t[i].f; k++) fcs[k] = fill[k];
        for (int c = 3; c <= 8 - t[i].e; c++) cols[c - 1] = fillc[c - 3];
        custom(&s, cols, fcs);
        fcs_click(&s, 1, 3);
        fcs_click(&s, 2, 0);
        if (t[i].msg) {
            CHECK_EQ(f.nmsg, 1);
            CHECK_EQ(f.last_msg_id, 307);
            CHECK_STR(f.last_msg, t[i].msg);
            CHECK_EQ(s.sel, 0);
        } else {
            CHECK_EQ(f.nmsg, 0);
            CHECK_EQ(f.nfwd, 19);
            CHECK_EQ(fc_last_index(&s.board, 2), 7);
            CHECK_EQ(s.nhist, 1);
            fcs_command(&s, FCS_CMD_UNDO);
            CHECK_EQ(f.nback, 19);
            CHECK_EQ(fc_last_index(&s.board, 1), 6);
        }
        fcs_free(&s);
    }
}

/* §2.5 MoveCol table, through the dialog. */
static void test_movecol(void)
{
    static const struct { int f, e, answer, moved, dialog; } t[] = {
        { 3, 1, FCS_MOVECOL_COLUMN, 4, 1 },
        { 1, 2, FCS_MOVECOL_COLUMN, 2, 1 },
        { 3, 1, FCS_MOVECOL_SINGLE, 1, 1 },
        { 3, 1, FCS_MOVECOL_CANCEL, 0, 1 },
        { 0, 1, FCS_MOVECOL_COLUMN, 1, 0 } };
    static const char *const fill[4] = { "KS", "KH", "KD", "KC" };
    static const char *const fillc[6] = { "QS", "QH", "QD", "QC", "JS", "JH" };
    for (int i = 0; i < 5; i++) {
        FcSession s; Fake f;
        fake_reset(&f, &s);
        FcSessionUI ui = fake_ui(&f);
        fcs_init(&s, &ui, NULL);
        start_game(&s, &f, 5);
        f.movecol = t[i].answer;
        const char *cols[8] = { "9H 8S 7H 6S 5H 4S 3H", "TC" }, *fcs[4] = { 0 };
        for (int k = 0; k < 4 - t[i].f; k++) fcs[k] = fill[k];
        for (int c = 3; c <= 8 - t[i].e; c++) cols[c - 1] = fillc[c - 3];
        custom(&s, cols, fcs);
        fcs_click(&s, 1, 6);
        fcs_click(&s, 8, 0);                    /* the empty column */
        CHECK_EQ(f.nmovecol, t[i].dialog);
        CHECK_EQ(fc_last_index(&s.board, 8) + 1, t[i].moved);
        CHECK_EQ(s.sel, 0);
        CHECK_EQ(f.nmsg, 0);
        CHECK_EQ(s.nhist, t[i].moved > 0);
        if (i == 0) {
            CHECK_EQ(s.board.board[8][0], C("6S"));
            CHECK_EQ(s.board.board[8][3], C("3H"));
        }
        fcs_free(&s);
    }
}

static void test_dblclick(void)
{
    static const char *const cols[8] = { "KH 4S", "QD 5H", "KS", "KC", "KD", "QS", "QC", "JH" };
    FcSession s; Fake f;
    fake_reset(&f, &s);
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, NULL);
    start_game(&s, &f, 5);
    custom(&s, cols, NULL);
    s.board.board[0][0] = C("JS");
    fix_cards_left(&s.board);
    /* option on: selected column card -> leftmost empty free cell, undoable */
    fcs_click(&s, 1, 1);
    fcs_dblclick(&s, 1, 1);
    CHECK_EQ(s.board.board[0][1], C("4S"));
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(s.nhist, 1);
    fcs_command(&s, FCS_CMD_UNDO);
    CHECK_EQ(s.board.board[1][1], C("4S"));
    /* no empty free cell: plain click (deselect), no message */
    s.board.board[0][1] = C("JD"); s.board.board[0][2] = C("JC"); s.board.board[0][3] = C("TS");
    s.board.board[3][0] = FC_EMPTY;               /* keep an empty column so no game-over check bites */
    fix_cards_left(&s.board);
    fcs_click(&s, 1, 1);
    fcs_dblclick(&s, 1, 1);
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(s.board.board[1][1], C("4S"));
    CHECK_EQ(f.nmsg, 0);
    s.board.board[0][3] = FC_EMPTY;
    fix_cards_left(&s.board);
    /* double-click on a free-cell card: select + deselect */
    fcs_click(&s, 0, 0);
    fcs_dblclick(&s, 0, 0);
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(s.board.board[0][0], C("JS"));
    /* without a selection a double-click is one click (no XP repost): selects */
    fcs_dblclick(&s, 2, 0);
    CHECK_EQ(s.sel, 1);
    CHECK_EQ(s.board.board[2][1], C("5H"));
    fcs_click(&s, 2, 0);
    /* option off: select, then the dblclick deselects */
    s.opts.dblclick = 0;
    fcs_click(&s, 1, 1);
    fcs_dblclick(&s, 1, 1);
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(s.board.board[1][1], C("4S"));
    CHECK_EQ(s.board.board[0][3], FC_EMPTY);
    /* swallowed activation click */
    fcs_mouse_activate(&s);
    fcs_click(&s, 1, 1);
    CHECK_EQ(s.sel, 0);
    fcs_click(&s, 1, 1);
    CHECK_EQ(s.sel, 1);
    fcs_free(&s);
}

static void test_keyboard(void)
{
    static const char *const cols[8] = { "KH 4S", "QD 5H", "KS 3C", "KC", "KD", "QS", "QC", "JH" };
    static const char *const fcs[4] = { NULL, "9D", NULL, "9C" };
    FcSession s; Fake f;
    fake_reset(&f, &s);
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, NULL);
    start_game(&s, &f, 5);
    custom(&s, cols, fcs);
    set_home(&s.board, 6, "2C");
    fix_cards_left(&s.board);

    fcs_char(&s, 'x');
    CHECK_EQ(s.sel, 0);
    fcs_char(&s, '9');                          /* nothing selected: nothing */
    CHECK_EQ(s.sel, 0);
    fcs_char(&s, '0');                          /* first occupied free cell */
    CHECK_EQ(s.sel, 1); CHECK_EQ(s.sel_col, 0); CHECK_EQ(s.sel_pos, 1);
    CHECK_EQ(s.king, FCS_KING_LEFT);
    fcs_char(&s, '0');                          /* next occupied free cell to the right */
    CHECK_EQ(s.sel, 1); CHECK_EQ(s.sel_col, 0); CHECK_EQ(s.sel_pos, 3);
    fcs_char(&s, '0');                          /* none further right: just deselected */
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(f.nmsg, 0);
    fcs_char(&s, '1');                          /* select column 1 */
    CHECK_EQ(s.sel_col, 1);
    fcs_char(&s, '2');                          /* move 4S onto 5H */
    CHECK_EQ(s.board.board[2][2], C("4S"));
    fcs_char(&s, '3');                          /* 3C */
    fcs_char(&s, '9');                          /* to its home slot (6) */
    CHECK_EQ(s.board.board[0][6], C("3C"));
    CHECK_EQ(s.board.home_rank[0], 2);
    fcs_char(&s, '8');                          /* JH */
    fcs_char(&s, '0');                          /* tableau card -> first empty free cell */
    CHECK_EQ(s.board.board[0][0], C("JH"));
    fcs_char(&s, '1');                          /* KH, no empty... free cell 2 empty */
    fcs_char(&s, '9');                          /* KH has no home slot: targets slot 4 -> illegal */
    CHECK_EQ(f.nmsg, 1);
    CHECK_EQ(s.sel, 0);

    /* peek: pressing the selected column's digit again (>= 2 cards) */
    fcs_char(&s, '2');                          /* column 2: QD 5H 4S */
    CHECK_EQ(s.sel_col, 2);
    clear_log(&f);
    fcs_char(&s, '2');
    CHECK_EQ(s.kbd_peek, 1);
    CHECK_EQ(s.peek_col, 2); CHECK_EQ(s.peek_pos, 0);
    CHECK_EQ(f.timer[FCS_TIMER_PEEK], 300);
    CHECK_EQ(fcs_mouse_move(&s, 0, 0, 1), FCS_CURSOR_WAIT);
    fcs_click(&s, 5, 0);                        /* input blocked */
    fcs_char(&s, '5');
    CHECK_EQ(s.sel_col, 2);
    fcs_timer(&s, FCS_TIMER_PEEK);
    CHECK_EQ(s.peek_pos, 1);
    CHECK_EQ(s.kbd_peek, 1);
    fcs_timer(&s, FCS_TIMER_PEEK);              /* bottom card reached: done, deselect */
    CHECK_EQ(s.kbd_peek, 0);
    CHECK_EQ(s.peek_col, -1);
    CHECK_EQ(f.timer[FCS_TIMER_PEEK], 0);
    CHECK_EQ(s.sel, 0);
    /* a single-card column selected: its digit deselects */
    fcs_char(&s, '4');
    fcs_char(&s, '4');
    CHECK_EQ(s.kbd_peek, 0);
    CHECK_EQ(s.sel, 0);

    /* right button peek on a covered card only */
    fcs_rbutton_down(&s, 2, 0);
    CHECK_EQ(s.peek_col, 2); CHECK_EQ(s.peek_pos, 0);
    fcs_rbutton_up(&s);
    CHECK_EQ(s.peek_col, -1);
    fcs_rbutton_down(&s, 2, 2);                 /* the bottom card: nothing */
    CHECK_EQ(s.peek_col, -1);
    fcs_rbutton_down(&s, 0, 1);                 /* top row: nothing */
    CHECK_EQ(s.peek_col, -1);
    fcs_free(&s);
}

static void test_cursor_king(void)
{
    static const char *const cols[8] = { "KH 4S", "QD 5H", "KS", "9H 8S 7H 6S 5D", "TC", "QS", "QC", "JH" };
    static const char *const fcs[4] = { "3D", NULL, NULL, NULL };
    FcSession s; Fake f;
    fake_reset(&f, &s);
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, NULL);
    start_game(&s, &f, 5);
    custom(&s, cols, fcs);
    set_home(&s.board, 5, "AS");
    set_home(&s.board, 6, "4D");
    fix_cards_left(&s.board);
    CHECK_EQ(fcs_mouse_move(&s, 0, 1, 1), FCS_CURSOR_ARROW);     /* nothing selected */
    CHECK_EQ(s.king, FCS_KING_LEFT);
    fcs_mouse_move(&s, 0, 6, 1);
    CHECK_EQ(s.king, FCS_KING_RIGHT);
    fcs_mouse_move(&s, FCS_MISS, 0, 0);                           /* elsewhere: unchanged */
    CHECK_EQ(s.king, FCS_KING_RIGHT);
    fcs_click(&s, 1, 0);                                         /* 4S */
    CHECK_EQ(fcs_cursor(&s, 0, 1, 1), FCS_CURSOR_UPARROW);       /* empty free cell */
    CHECK_EQ(fcs_cursor(&s, 0, 0, 1), FCS_CURSOR_ARROW);         /* occupied */
    CHECK_EQ(fcs_cursor(&s, 0, 5, 1), FCS_CURSOR_ARROW);         /* 4S onto AS home: no */
    CHECK_EQ(fcs_cursor(&s, 2, 1, 1), FCS_CURSOR_DOWNARROW);     /* onto 5H */
    CHECK_EQ(fcs_cursor(&s, 2, 1, 0), FCS_CURSOR_ARROW);         /* below the cards */
    CHECK_EQ(fcs_cursor(&s, 3, 0, 1), FCS_CURSOR_ARROW);         /* onto KS: illegal */
    CHECK_EQ(fcs_cursor(&s, 1, 1, 1), FCS_CURSOR_ARROW);         /* itself */
    s.board.board[8][0] = FC_EMPTY;
    CHECK_EQ(fcs_cursor(&s, 8, -1, 0), FCS_CURSOR_UPARROW);      /* empty column, no check */
    CHECK_EQ(fcs_cursor(&s, FCS_MISS, 0, 0), FCS_CURSOR_ARROW);
    fcs_click(&s, 1, 0);
    fcs_click(&s, 4, 0);                                         /* run 6S 5D, onto TC? no */
    CHECK_EQ(fcs_cursor(&s, 5, 0, 1), FCS_CURSOR_DOWNARROW);     /* 9H..5D onto TC: 5 <= cap */
    s.board.board[0][1] = C("2C"); s.board.board[0][2] = C("2H"); s.board.board[0][3] = C("JD");
    s.board.board[8][0] = C("JH");
    CHECK_EQ(fcs_cursor(&s, 5, 0, 1), FCS_CURSOR_ARROW);         /* capacity 1 < 5 */
    fcs_click(&s, 4, 0);                                         /* deselect */
    fcs_click(&s, 0, 0);                                         /* 3D in a free cell */
    CHECK_EQ(fcs_cursor(&s, 0, 6, 1), FCS_CURSOR_ARROW);         /* home is at 4D */
    s.board.board[0][6] = C("2D"); s.board.home_rank[1] = 1;
    CHECK_EQ(fcs_cursor(&s, 0, 6, 1), FCS_CURSOR_UPARROW);
    CHECK_EQ(fcs_cursor(&s, 0, 0, 1), FCS_CURSOR_ARROW);         /* itself */
    CHECK_EQ(fcs_cursor(&s, 1, 0, 1), FCS_CURSOR_DOWNARROW);     /* 3D onto 4S */
    fcs_free(&s);
}

/* Near-win position: everything home except KS at the bottom of column 1. */
static void near_win(FcSession *s)
{
    fc_board_clear(&s->board);
    set_home(&s->board, 4, "KC");
    set_home(&s->board, 5, "KD");
    set_home(&s->board, 6, "KH");
    set_home(&s->board, 7, "QS");
    set_col(&s->board, 1, "KS");
    fix_cards_left(&s->board);
}

static void test_win_flow(void)
{
    for (int variant = 0; variant < 3; variant++) {
        FcSession s; Fake f; Reg r = { 0 };
        FcStore st = make_store(&r);
        fake_reset(&f, &s);
        FcSessionUI ui = fake_ui(&f);
        fcs_init(&s, &ui, &st);
        start_game(&s, &f, 7);
        near_win(&s);
        f.win_yes = variant != 2;
        f.win_sel = variant == 1 ? 0 : -1;
        fcs_click(&s, 1, 0);
        fcs_click(&s, 1, 0);                    /* deselect -> autoplay KS -> win */
        CHECK_EQ(f.nwin, 1);
        CHECK_EQ(f.win_sel_in, 1);              /* last deal was Select Game */
        CHECK_EQ(rv(&r, "won"), 1);
        CHECK_EQ(rv(&r, "streak"), 1);
        CHECK_EQ(rv(&r, "stype"), 1);
        CHECK_EQ(rv(&r, "wins"), 1);
        CHECK_EQ(s.stats.session_won, 1);
        CHECK_EQ(s.stats.last_recorded, 7);
        CHECK_EQ(s.big_king, 1);
        CHECK_EQ(s.king, FCS_KING_BLANK);
        CHECK_EQ(s.game_number, 0);
        CHECK_EQ(s.in_progress, 0);
        CHECK_EQ(s.nhist, 0);
        CHECK_EQ(f.undo_en, 0);
        CHECK_EQ(f.cards_left, 0);
        fcs_click(&s, 1, 0);                    /* frozen */
        CHECK_EQ(s.sel, 0);
        if (variant == 0) {
            CHECK_EQ(f.nposted, 1);
            CHECK_EQ(f.posted[0], FCS_CMD_SELECT);
            f.gn[0] = 99; f.ngn = 1; f.gni = 0;
            run_posted(&s, &f);
            CHECK_EQ(f.nresign, 0);             /* not in progress: no prompt */
            CHECK_EQ(s.game_number, 99);
            CHECK_EQ(s.big_king, 0);
            CHECK_EQ(s.king, FCS_KING_RIGHT);
        } else if (variant == 1) {
            CHECK_EQ(f.posted[0], FCS_CMD_NEW);
            CHECK_EQ(s.select_flag, 0);
            run_posted(&s, &f);
            CHECK(s.game_number >= 1 && s.game_number <= 32767);
            CHECK_EQ(s.select_flag, 0);
        } else {
            CHECK_EQ(f.nposted, 0);             /* No: frozen; Restart re-deals the won game */
            fcs_command(&s, FCS_CMD_RESTART);
            CHECK_EQ(s.game_number, 7);
            CHECK_EQ(f.nresign, 0);
            /* winning the same number right after: not counted again */
            near_win(&s);
            fcs_click(&s, 1, 0); fcs_click(&s, 1, 0);
            CHECK_EQ(rv(&r, "won"), 1);
            CHECK_EQ(f.nwin, 2);
        }
        fcs_free(&s);
    }
    /* cards-left goes down card by card during an autoplay cascade */
    FcSession s; Fake f;
    fake_reset(&f, &s);
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, NULL);
    start_game(&s, &f, 3);
    fc_board_clear(&s.board);
    set_home(&s.board, 4, "QC"); set_home(&s.board, 5, "QD"); set_home(&s.board, 6, "QH"); set_home(&s.board, 7, "QS");
    set_col(&s.board, 1, "KS KH");
    set_col(&s.board, 2, "KC KD");
    fix_cards_left(&s.board);
    f.ncl = 0;
    fcs_click(&s, 1, 0); fcs_click(&s, 1, 0);
    CHECK_EQ(f.ncl, 4);
    CHECK_EQ(f.cl_seq[0], 3); CHECK_EQ(f.cl_seq[1], 2); CHECK_EQ(f.cl_seq[2], 1); CHECK_EQ(f.cl_seq[3], 0);
    CHECK_EQ(f.nwin, 1);
    fcs_free(&s);
}

/* Zero-move and one-move positions (all free cells full, no empty column). */
static void stuck(FcSession *s, const char *last_bottom)
{
    static const char *const tops[8] = { "4H", "6H", "8H", "TH", "4D", "6D", "8D", "TD" };
    static const char *const bots[7] = { "3S", "5S", "7S", "9S", "JS", "3C", "5C" };
    fc_board_clear(&s->board);
    s->board.board[0][0] = C("KS"); s->board.board[0][1] = C("KC");
    s->board.board[0][2] = C("KH"); s->board.board[0][3] = C("KD");
    char buf[16];
    for (int c = 1; c <= 8; c++) {
        snprintf(buf, sizeof buf, "%s %s", tops[c - 1], c < 8 ? bots[c - 1] : last_bottom);
        set_col(&s->board, c, buf);
    }
    fix_cards_left(&s->board);
}

static void test_lose_flow(void)
{
    /* one move left: flash 4 x 400 ms, no dialog */
    {
        FcSession s; Fake f;
        fake_reset(&f, &s);
        FcSessionUI ui = fake_ui(&f);
        fcs_init(&s, &ui, NULL);
        start_game(&s, &f, 9);
        stuck(&s, "QD");
        fcs_click(&s, 1, 0); fcs_click(&s, 1, 0);
        CHECK_EQ(f.nlose, 0);
        CHECK_EQ(f.timer[FCS_TIMER_FLASH], 400);
        for (int i = 0; i < 4; i++) fcs_timer(&s, FCS_TIMER_FLASH);
        CHECK_EQ(f.nflash_on, 4);
        CHECK_EQ(f.nflash_off, 1);
        CHECK_EQ(f.timer[FCS_TIMER_FLASH], 0);
        CHECK_EQ(s.in_progress, 1);
        fcs_free(&s);
    }
    for (int variant = 0; variant < 4; variant++) {
        FcSession s; Fake f; Reg r = { 0 };
        FcStore st = make_store(&r);
        fake_reset(&f, &s);
        FcSessionUI ui = fake_ui(&f);
        fcs_init(&s, &ui, &st);
        if (variant == 3) { f.now = 1234; fcs_command(&s, FCS_CMD_NEW); }
        else start_game(&s, &f, 9);
        int g = s.game_number;
        stuck(&s, "7C");
        f.lose_yes = variant != 2;
        f.lose_same = variant == 1 || variant == 3 ? 0 : -1;
        fcs_click(&s, 1, 0);
        fcs_click(&s, 2, 0);                    /* illegal (messages on): deselect -> no moves */
        CHECK_EQ(f.nlose, 1);
        CHECK_EQ(f.lose_same_in, 1);
        CHECK_EQ(rv(&r, "lost"), 1);
        CHECK_EQ(rv(&r, "stype"), 0);
        CHECK_EQ(rv(&r, "streak"), 1);
        CHECK_EQ(rv(&r, "losses"), 1);
        CHECK_EQ(s.stats.session_lost, 1);
        CHECK_EQ(s.game_number, 0);
        CHECK_EQ(s.in_progress, 0);
        CHECK_EQ(f.undo_en, 0);
        if (variant == 0) {
            CHECK_EQ(f.posted[0], FCS_CMD_RESTART);
            run_posted(&s, &f);
            CHECK_EQ(f.nresign, 0);
            CHECK_EQ(s.game_number, 9);
            /* losing #9 again right away: not counted twice */
            stuck(&s, "7C");
            fcs_click(&s, 1, 0); fcs_click(&s, 1, 0);
            CHECK_EQ(rv(&r, "lost"), 1);
            CHECK_EQ(f.nlose, 2);
        } else if (variant == 1) {
            CHECK_EQ(f.posted[0], FCS_CMD_SELECT);  /* select flag set by Select Game */
        } else if (variant == 3) {
            CHECK_EQ(f.posted[0], FCS_CMD_NEW);
            run_posted(&s, &f);
            CHECK(s.game_number != g);
        } else {
            CHECK_EQ(f.nposted, 0);
            fcs_click(&s, 1, 0);                /* frozen */
            CHECK_EQ(s.sel, 0);
            fcs_command(&s, FCS_CMD_RESTART);   /* re-deals the lost game */
            CHECK_EQ(s.game_number, 9);
        }
        fcs_free(&s);
    }
}

static void test_resign_flows(void)
{
    FcSession s; Fake f; Reg r = { 0 };
    FcStore st = make_store(&r);
    fake_reset(&f, &s);
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, &st);
    start_game(&s, &f, 11);
    /* New, answer No: nothing */
    f.resign = 0;
    fcs_command(&s, FCS_CMD_NEW);
    CHECK_EQ(f.nresign, 1);
    CHECK_EQ(s.game_number, 11);
    CHECK_EQ(rv(&r, "lost"), -1);
    /* Select Game: invalid entries re-show the dialog with 0; cancel restores everything */
    fcs_click(&s, 1, 0);
    fcs_click(&s, 0, 0);                        /* a move, so there is history */
    CHECK_EQ(s.nhist, 1);
    FcBoard before = s.board;
    f.resign = 1;
    f.gn[0] = 0; f.gn[1] = -3; f.gn[2] = 1000001; f.gn[3] = GN_CANCEL; f.ngn = 4; f.gni = 0;
    clear_log(&f);
    fcs_command(&s, FCS_CMD_SELECT);
    CHECK_EQ(f.gni, 4);
    CHECK_EQ(f.gn_init[1], 0);
    CHECK_EQ(f.gn_init[3], 0);
    CHECK(strstr(f.log, "resign?;gamenum(") == f.log);   /* resign prompt first, as XP */
    CHECK(board_eq(&s.board, &before));
    CHECK_EQ(s.game_number, 11);
    CHECK_EQ(s.in_progress, 1);
    CHECK_EQ(s.nhist, 1);
    CHECK_EQ(rv(&r, "lost"), -1);                /* no loss recorded on cancel */
    CHECK_EQ(s.select_flag, 1);
    fcs_command(&s, FCS_CMD_UNDO);              /* the old game is fully playable */
    CHECK_EQ(s.nhist, 0);
    /* Restart, Yes: a loss, same game; again: not counted again (same number) */
    fcs_command(&s, FCS_CMD_RESTART);
    CHECK_EQ(rv(&r, "lost"), 1);
    CHECK_EQ(s.game_number, 11);
    CHECK_EQ(s.in_progress, 1);
    fcs_command(&s, FCS_CMD_RESTART);
    CHECK_EQ(f.nresign, 4);
    CHECK_EQ(rv(&r, "lost"), 1);
    /* Select a valid number (-1 works) after resigning: loss for #11 already counted */
    f.gn[0] = -1; f.ngn = 1; f.gni = 0;
    fcs_command(&s, FCS_CMD_SELECT);
    CHECK_EQ(s.game_number, -1);
    CHECK_STR(f.title, "FreeCell Game #-1");
    CHECK_EQ(rv(&r, "lost"), 1);
    /* resigning -1: a loss is never recorded for game numbers <= 0 */
    f.gn[0] = 12; f.ngn = 1; f.gni = 0;
    fcs_command(&s, FCS_CMD_SELECT);
    CHECK_EQ(rv(&r, "lost"), 1);
    CHECK_EQ(s.game_number, 12);
    /* New: loss for 12, streak 2 */
    fcs_command(&s, FCS_CMD_NEW);
    CHECK_EQ(rv(&r, "lost"), 2);
    CHECK_EQ(rv(&r, "streak"), 2);
    CHECK_EQ(rv(&r, "losses"), 2);
    CHECK_EQ(s.select_flag, 0);
    /* two New Games in the same second give different numbers */
    int g1 = s.game_number;
    fcs_command(&s, FCS_CMD_NEW);
    CHECK(s.game_number != g1);
    CHECK(s.game_number >= 1 && s.game_number <= 32767);
    /* Exit: No keeps the window; Yes records the loss and saves the options */
    f.resign = 0;
    CHECK_EQ(fcs_close(&s), 0);
    f.resign = 1;
    s.opts.quick = 1;
    int lost = (int)rv(&r, "lost");
    CHECK_EQ(fcs_close(&s), 1);
    CHECK_EQ(rv(&r, "lost"), lost + 1);
    CHECK_EQ(rv(&r, "quick"), 1);
    CHECK_EQ(r.nflush, 1);
    fcs_free(&s);
}

static void test_cheat(void)
{
    for (int win = 0; win <= 1; win++) {
        FcSession s; Fake f; Reg r = { 0 };
        FcStore st = make_store(&r);
        fake_reset(&f, &s);
        FcSessionUI ui = fake_ui(&f);
        fcs_init(&s, &ui, &st);
        start_game(&s, &f, 21);
        f.check_replay = 1;
        f.cheat = win ? FCS_CHEAT_WIN : FCS_CHEAT_LOSE;
        fcs_command(&s, FCS_CMD_CHEAT);
        CHECK_EQ(s.cheat, f.cheat);
        CHECK_EQ(f.nwin + f.nlose, 0);         /* nothing until the next committed click */
        fcs_click(&s, 1, 0);
        CHECK_EQ(f.nwin + f.nlose, 0);
        fcs_click(&s, 1, 0);                    /* deselect commits */
        if (win) {
            CHECK_EQ(f.nwin, 1);
            CHECK_EQ(f.nfwd, 52);
            CHECK_EQ(rv(&r, "won"), 1);
            for (int k = 0; k < 4; k++) CHECK_EQ(s.board.home_rank[k], 12);
            CHECK(board_ok(&s.board));
            for (int i = 4; i < 8; i++) CHECK_EQ(fc_rank(s.board.board[0][i]), 12);
        } else {
            CHECK_EQ(f.nlose, 1);
            CHECK_EQ(rv(&r, "lost"), 1);
        }
        CHECK_EQ(s.cheat, 0);
        fcs_free(&s);
    }
}

static void test_stats(void)
{
    CHECK_EQ(fc_stats_percent(1, 3), 25);
    CHECK_EQ(fc_stats_percent(199, 1), 99);
    CHECK_EQ(fc_stats_percent(2, 1), 67);
    CHECK_EQ(fc_stats_percent(1, 7), 13);
    CHECK_EQ(fc_stats_percent(0, 0), 0);
    CHECK_EQ(fc_stats_percent(5, 0), 100);
    CHECK_EQ(fc_stats_percent(0, 4), 0);
    CHECK_EQ(fc_stats_percent(1, 1), 50);

    Reg r = { 0 };
    FcStore st = make_store(&r);
    FcStats s;
    fc_stats_init(&s, &st);
    fc_stats_record_loss(&s, 5);
    CHECK_EQ(rv(&r, "lost"), 1); CHECK_EQ(rv(&r, "stype"), 0); CHECK_EQ(rv(&r, "streak"), 1);
    fc_stats_record_win(&s, 5);                 /* same number right after: not counted */
    CHECK_EQ(rv(&r, "won"), -1);
    fc_stats_record_win(&s, 6);
    CHECK_EQ(rv(&r, "won"), 1); CHECK_EQ(rv(&r, "stype"), 1); CHECK_EQ(rv(&r, "streak"), 1);
    fc_stats_record_win(&s, 7);
    fc_stats_record_win(&s, -1);                /* wins on -1 count */
    CHECK_EQ(rv(&r, "won"), 3); CHECK_EQ(rv(&r, "streak"), 3); CHECK_EQ(rv(&r, "wins"), 3);
    fc_stats_record_loss(&s, -2);               /* losses on -2 do not */
    CHECK_EQ(rv(&r, "lost"), 1);
    CHECK_EQ(s.last_recorded, -2);
    fc_stats_record_loss(&s, 8);
    fc_stats_record_loss(&s, 9);
    CHECK_EQ(rv(&r, "lost"), 3); CHECK_EQ(rv(&r, "streak"), 2); CHECK_EQ(rv(&r, "losses"), 2);
    CHECK_EQ(rv(&r, "wins"), 3);
    CHECK_EQ(s.session_won, 3); CHECK_EQ(s.session_lost, 3);

    FcStatsView v;
    char buf[200];
    fc_stats_view(&s, &v);
    fc_stats_format_session(&v, buf, sizeof buf);
    CHECK_STR(buf, "This session\t\t\t50%\n\twon:\t\t3 \n\tlost:\t\t3\n\n");
    fc_stats_format_total(&v, buf, sizeof buf);
    CHECK_STR(buf, "Total\t\t\t\t50%\n\twon:\t\t3 \n\tlost:\t\t3\n\n");
    fc_stats_format_streaks(&v, buf, sizeof buf);
    CHECK_STR(buf, "Streaks\n\twins:\t\t3 \n\tlosses:\t\t2 \n\tcurrent:\t\t2 losses");
    fc_stats_format_current(0, 1, buf, sizeof buf); CHECK_STR(buf, "0");
    fc_stats_format_current(1, 1, buf, sizeof buf); CHECK_STR(buf, "1 win");
    fc_stats_format_current(1, 0, buf, sizeof buf); CHECK_STR(buf, "1 loss");
    fc_stats_format_current(5, 0, buf, sizeof buf); CHECK_STR(buf, "5 losses");
    fc_stats_format_current(7, 1, buf, sizeof buf); CHECK_STR(buf, "7 wins");
    /* Wine-verified rendering: "This session 25% / won: 1 / lost: 3" */
    v.session_won = 1; v.session_lost = 3;
    fc_stats_format_session(&v, buf, sizeof buf);
    CHECK_STR(buf, "This session\t\t\t25%\n\twon:\t\t1 \n\tlost:\t\t3\n\n");

    reg_set(&r, "AlreadyPlayed", 1);
    reg_set(&r, "messages", 0);
    fc_stats_clear(&s);
    CHECK_EQ(r.n, 2);                           /* only AlreadyPlayed and the option remain */
    CHECK_EQ(s.session_won, 0); CHECK_EQ(s.session_lost, 0);
    fc_stats_view(&s, &v);
    fc_stats_format_streaks(&v, buf, sizeof buf);
    CHECK_STR(buf, "Streaks\n\twins:\t\t0 \n\tlosses:\t\t0 \n\tcurrent:\t\t0");

    /* entpack.ini migration: once, only when AlreadyPlayed is missing */
    Reg m = { 0 };
    m.legacy_ok = 1;
    uint32_t leg[6] = { 4, 9, 2, 5, 1, 1 };
    memcpy(m.legacy, leg, sizeof leg);
    FcStore ms = make_store(&m);
    fc_stats_init(&s, &ms);
    CHECK_EQ(fc_stats_migrate(&s), 1);
    CHECK_EQ(rv(&m, "lost"), 4); CHECK_EQ(rv(&m, "won"), 9); CHECK_EQ(rv(&m, "losses"), 2);
    CHECK_EQ(rv(&m, "wins"), 5); CHECK_EQ(rv(&m, "streak"), 1); CHECK_EQ(rv(&m, "stype"), 1);
    CHECK_EQ(rv(&m, "AlreadyPlayed"), 1);
    reg_set(&m, "won", 10);
    CHECK_EQ(fc_stats_migrate(&s), 0);
    CHECK_EQ(rv(&m, "won"), 10);
    /* legacy source not available: existing values untouched, flag set */
    Reg k = { 0 };
    reg_set(&k, "won", 3);
    FcStore ks = make_store(&k);
    fc_stats_init(&s, &ks);
    CHECK_EQ(fc_stats_migrate(&s), 1);
    CHECK_EQ(rv(&k, "won"), 3);
    CHECK_EQ(rv(&k, "AlreadyPlayed"), 1);
    /* a store without callbacks: defaults, nothing crashes */
    FcStore none = { 0 };
    fc_stats_init(&s, &none);
    fc_stats_record_win(&s, 3);
    CHECK_EQ(fc_stats_get(&s, "won", 0), 0);
    CHECK_EQ(s.session_won, 1);
}

static void test_options(void)
{
    Reg r = { 0 };
    FcStore st = make_store(&r);
    FcOptions o;
    fc_options_load(&o, &st);
    CHECK_EQ(o.messages, 1); CHECK_EQ(o.quick, 0); CHECK_EQ(o.dblclick, 1);
    o.messages = 0; o.quick = 1; o.dblclick = 0;
    fc_options_save(&o, &st);
    CHECK_EQ(rv(&r, "messages"), 0); CHECK_EQ(rv(&r, "quick"), 1); CHECK_EQ(rv(&r, "dblclick"), 0);
    CHECK_EQ(r.nflush, 1);
    FcOptions p;
    fc_options_load(&p, &st);
    CHECK_EQ(p.messages, 0); CHECK_EQ(p.quick, 1); CHECK_EQ(p.dblclick, 0);
    fc_options_default(&o);
    fc_options_save(&o, &st);                   /* defaults are deleted */
    CHECK_EQ(rv(&r, "messages"), -1); CHECK_EQ(rv(&r, "quick"), -1); CHECK_EQ(rv(&r, "dblclick"), -1);
    CHECK_EQ(r.n, 0);
    /* session loads them at init and saves on close only */
    reg_set(&r, "dblclick", 0);
    FcSession s;
    fcs_init(&s, NULL, &st);
    CHECK_EQ(s.opts.dblclick, 0);
    s.opts.messages = 0;
    CHECK_EQ(rv(&r, "messages"), -1);
    CHECK_EQ(fcs_close(&s), 1);                 /* no game in progress, no prompt */
    CHECK_EQ(rv(&r, "messages"), 0);
    CHECK_EQ(rv(&r, "dblclick"), 0);
    fcs_free(&s);
}

/* ---- randomized playout ---------------------------------------------------------------------- */

static int session_ok(const FcSession *s)
{
    if (!board_ok(&s->board)) return 0;
    if (s->sel) {
        if (s->sel_col == 0) {
            if (s->sel_pos < 0 || s->sel_pos > 3 || s->board.board[0][s->sel_pos] == FC_EMPTY) return 0;
        } else if (s->sel_col < 1 || s->sel_col > 8 || fc_last_index(&s->board, s->sel_col) != s->sel_pos ||
                   s->sel_pos < 0) {
            return 0;
        }
    }
    return 1;
}

static void test_random_playout(void)
{
    srand(4242);
    long actions = 0, undone = 0, wins = 0, losses = 0, deals = 0;
    for (int game = 0; game < 150; game++) {
        FcSession s; Fake f; Reg r = { 0 };
        FcStore st = make_store(&r);
        fake_reset(&f, &s);
        f.check_replay = 1;
        FcSessionUI ui = fake_ui(&f);
        if (game & 1) ui.post_command = NULL;   /* follow-ups run directly */
        fcs_init(&s, &ui, &st);
        start_game(&s, &f, 1 + rand() % 1000000);
        FcBoard dealt = s.board;
        int ntitle = f.ntitle, frozen = 0;
        s.opts.messages = rand() % 2;
        s.opts.dblclick = rand() % 2;
        for (int k = 0; k < 500; k++) {
            f.movecol = rand() % 3 - 1;
            f.lose_yes = f.win_yes = rand() % 2;
            f.lose_same = f.win_sel = rand() % 2;
            f.resign = rand() % 2;
            f.gn[0] = rand() % 4 ? 1 + rand() % 1000000 : GN_CANCEL; f.ngn = 1; f.gni = 0;
            int lost0 = f.nlose, won0 = f.nwin;
            int what = rand() % 1000;
            int col = rand() % 10 - 1, pos = rand() % 8;
            if (col < 0) col = FCS_MISS;
            if (col >= 1 && col <= 8) pos = rand() % 3 ? fc_last_index(&s.board, col) : rand() % 8;
            if (what < 700) fcs_click(&s, col, pos);
            else if (what < 780) fcs_dblclick(&s, col, pos);
            else if (what < 880) fcs_char(&s, '0' + rand() % 10);
            else if (what < 910) { fcs_rbutton_down(&s, col, pos); fcs_rbutton_up(&s); }
            else if (what < 945) fcs_command(&s, FCS_CMD_UNDO);
            else if (what < 960) fcs_command(&s, FCS_CMD_REDO);
            else if (what < 990) fcs_mouse_move(&s, col, pos, rand() % 2);
            else {
                static const int cmds[4] = { FCS_CMD_NEW, FCS_CMD_SELECT, FCS_CMD_RESTART, FCS_CMD_CHEAT };
                f.cheat = rand() % 5 == 0 ? FCS_CHEAT_WIN : FCS_CHEAT_NONE;
                fcs_command(&s, cmds[rand() % 4]);
            }
            while (s.kbd_peek) fcs_timer(&s, FCS_TIMER_PEEK);
            if (f.nposted) run_posted(&s, &f);
            actions++;
            wins += f.nwin - won0;
            losses += f.nlose - lost0;
            if (!session_ok(&s)) {
                fails++;
                printf("FAIL invariants: game %d action %d\n", s.game_number, k);
                break;
            }
            CHECK_EQ(f.undo_en, fcs_undo_enabled(&s));
            CHECK_EQ(f.redo_en, fcs_redo_enabled(&s));
            if (f.ntitle != ntitle) {           /* a new deal: new baseline */
                ntitle = f.ntitle;
                dealt = s.board;
                deals++;
                frozen = 0;
                FcBoard d;
                fc_deal(&d, s.game_number);
                CHECK(board_eq(&d, &s.board));
            }
            if (s.game_number == 0) {
                frozen = 1;
                CHECK_EQ(s.nhist, 0);
                CHECK_EQ(s.nredo, 0);
            }
        }
        if (!frozen) {
            while (fcs_undo_enabled(&s)) fcs_command(&s, FCS_CMD_UNDO);
            CHECK(board_eq(&s.board, &dealt));
            CHECK_EQ(s.nhist, 0);
            CHECK_EQ(f.cards_left, 52);
            undone++;
        }
        fcs_free(&s);
    }
    printf("random playout: %ld actions, %ld deals, %ld lost, %ld won, %ld games undone to the deal\n",
           actions, deals, losses, wins, undone);
    CHECK(undone > 50);
    CHECK(wins > 0 && losses > 0);
}

/* Greedy legal play with undo stress: many moves, many undos interleaved. */
static void test_undo_stress(void)
{
    srand(99);
    for (int game = 0; game < 30; game++) {
        FcSession s; Fake f;
        fake_reset(&f, &s);
        FcSessionUI ui = fake_ui(&f);
        fcs_init(&s, &ui, NULL);
        start_game(&s, &f, 1 + game * 31337 % 32000);
        FcBoard dealt = s.board;
        FcBoard *snap = malloc(sizeof(FcBoard) * 2048);
        int nsnap = 0;
        snap[nsnap++] = s.board;
        for (int k = 0; k < 600 && s.game_number; k++) {
            if (rand() % 5 == 0 && fcs_undo_enabled(&s)) {
                fcs_command(&s, FCS_CMD_UNDO);
                nsnap--;
                CHECK(board_eq(&s.board, &snap[nsnap - 1]));
                continue;
            }
            int h = s.nhist;
            fcs_click(&s, 1 + rand() % 8, 0);
            fcs_click(&s, rand() % 9, 4 * (rand() % 2) + rand() % 4);
            if (s.sel) fcs_click(&s, FCS_MISS, 0);
            if (s.nhist > h && nsnap < 2048) snap[nsnap++] = s.board;
            else if (s.nhist < h) { nsnap = 1; snap[0] = s.board; dealt = s.board; }
        }
        if (s.game_number) {
            while (fcs_undo_enabled(&s)) fcs_command(&s, FCS_CMD_UNDO);
            CHECK(board_eq(&s.board, &dealt));
        }
        free(snap);
        fcs_free(&s);
    }
}

/* Redo (extra): undo N then redo N gives back the identical boards, with the same animated steps;
 * a selecting click keeps the redo stack, a new committed action / deal clears it; random undo/redo
 * interleaving against snapshots. */
static int legal_action(FcSession *s)           /* try random click pairs until one commits */
{
    for (int tries = 0; tries < 400 && s->game_number; tries++) {
        int h = s->nhist;
        fcs_click(s, 1 + rand() % 8, 0);
        fcs_click(s, rand() % 9, 4 * (rand() % 2) + rand() % 4);
        if (s->sel) fcs_click(s, FCS_MISS, 0);
        if (s->nhist > h) return 1;
        if (s->nhist < h) return -1;            /* history dropped (win / lose) */
    }
    return 0;
}

static void test_redo(void)
{
    FcSession s; Fake f;
    fake_reset(&f, &s);
    f.check_replay = 1;
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, NULL);
    s.opts.messages = 0;
    srand(7);
    start_game(&s, &f, 1);
    CHECK_EQ(f.redo_en, 0);
    FcBoard snaps[40];
    int n = 0;
    snaps[0] = s.board;
    while (n < 24 && legal_action(&s) == 1) snaps[++n] = s.board;
    CHECK(n >= 8);
    CHECK(!fcs_redo_enabled(&s));
    fcs_command(&s, FCS_CMD_REDO);              /* nothing to redo */
    CHECK(board_eq(&s.board, &snaps[n]));

    int back0 = f.nback;
    for (int i = n; i > 0; i--) {
        fcs_command(&s, FCS_CMD_UNDO);
        CHECK(board_eq(&s.board, &snaps[i - 1]));
        CHECK_EQ(f.redo_en, 1);
        CHECK_EQ(s.nredo, n - i + 1);
    }
    CHECK_EQ(f.undo_en, 0);
    int fwd0 = f.nfwd, cl0 = f.ncl;
    for (int i = 1; i <= n; i++) {
        fcs_command(&s, FCS_CMD_REDO);
        CHECK(board_eq(&s.board, &snaps[i]));
        CHECK_EQ(f.undo_en, 1);
        CHECK_EQ(f.redo_en, i < n);
    }
    CHECK_EQ(f.nfwd - fwd0, f.nback - back0);   /* every undone step animated forward again */
    CHECK_EQ(f.cards_left, s.board.cards_left);
    CHECK(f.ncl >= cl0);
    CHECK_EQ(s.nredo, 0);
    CHECK_EQ(s.nhist, n);

    /* partial: undo 3, redo 1; a selecting click and a pure deselect keep the redo stack */
    for (int i = 0; i < 3; i++) fcs_command(&s, FCS_CMD_UNDO);
    fcs_command(&s, FCS_CMD_REDO);
    CHECK(board_eq(&s.board, &snaps[n - 2]));
    CHECK_EQ(s.nredo, 2);
    int col = 1;
    while (col <= 8 && fc_last_index(&s.board, col) < 0) col++;
    fcs_click(&s, col, 0);
    CHECK_EQ(s.sel, 1);
    CHECK(fcs_redo_enabled(&s));
    FcBoard pre = s.board;
    fcs_click(&s, col, 0);                      /* deselect (autoplay would be a new action) */
    if (board_eq(&s.board, &pre)) CHECK_EQ(s.nredo, 2);
    /* a redo after the selection works and drops the selection first */
    fcs_click(&s, col, 0);
    fcs_command(&s, FCS_CMD_REDO);
    CHECK_EQ(s.sel, 0);
    CHECK(board_eq(&s.board, &snaps[n - 1]));
    CHECK_EQ(s.nredo, 1);

    /* a newly committed action clears it */
    fcs_command(&s, FCS_CMD_UNDO);
    CHECK_EQ(s.nredo, 2);
    int r = legal_action(&s);
    CHECK(r != 0);
    CHECK_EQ(s.nredo, 0);
    CHECK_EQ(f.redo_en, 0);
    FcBoard now = s.board;
    fcs_command(&s, FCS_CMD_REDO);
    CHECK(board_eq(&s.board, &now));

    /* a deal clears it */
    if (s.game_number) {
        fcs_command(&s, FCS_CMD_UNDO);
        CHECK_EQ(f.redo_en, 1);
    }
    start_game(&s, &f, 2);
    CHECK_EQ(s.nredo, 0);
    CHECK_EQ(f.redo_en, 0);
    fcs_free(&s);

    /* random undo/redo interleaving against snapshots */
    for (int game = 0; game < 20; game++) {
        fake_reset(&f, &s);
        f.check_replay = 1;
        ui = fake_ui(&f);
        fcs_init(&s, &ui, NULL);
        s.opts.messages = 0;
        start_game(&s, &f, 100 + game * 7919 % 30000);
        static FcBoard snap[1024], rsnap[1024];
        int ns = 0, nr = 0;
        snap[ns++] = s.board;
        for (int k = 0; k < 400 && s.game_number; k++) {
            int what = rand() % 10;
            if (what < 3 && fcs_undo_enabled(&s)) {
                fcs_command(&s, FCS_CMD_UNDO);
                rsnap[nr++] = snap[--ns];
                CHECK(board_eq(&s.board, &snap[ns - 1]));
            } else if (what < 6 && fcs_redo_enabled(&s)) {
                fcs_command(&s, FCS_CMD_REDO);
                if (!s.game_number) break;      /* cannot happen: a redone action never ends the game */
                snap[ns++] = rsnap[--nr];
                CHECK(board_eq(&s.board, &snap[ns - 1]));
            } else {
                int res = legal_action(&s);
                if (res == 1 && ns < 1024) { snap[ns++] = s.board; nr = 0; CHECK_EQ(s.nredo, 0); }
                else if (res != 0) break;
            }
            CHECK_EQ(s.nredo, nr);
            CHECK_EQ(f.redo_en, fcs_redo_enabled(&s));
            CHECK_EQ(f.undo_en, fcs_undo_enabled(&s));
        }
        fcs_free(&s);
    }
}

int main(void)
{
    test_startup_and_deal();
    test_select_move_undo();
    test_messages_on_off();
    test_capacity_messages();
    test_movecol();
    test_dblclick();
    test_keyboard();
    test_cursor_king();
    test_win_flow();
    test_lose_flow();
    test_resign_flows();
    test_cheat();
    test_stats();
    test_options();
    test_random_playout();
    test_undo_stress();
    test_redo();
    printf("test_session: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
