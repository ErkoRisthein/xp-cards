/*
 * FreeCell HD — native tests of the XP controller (src/freecell/session.c) and the statistics/options
 * model (src/freecell/stats.c), driven through a scripted fake UI. Plus a randomized playout with
 * invariants and full undo back to the deal. v1.1 extras: move counter, game clock (fake clock),
 * standard supermove rule in the controller, New Game range, won-deals set and its file format.
 */
#include "freecell/session.h"
#include "freecell/stats.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;

/* The random tests' own generator: the C library's rand() gives different sequences on macOS and
 * glibc, and CI runs both (64-bit LCG, the high 31 bits). */
static uint64_t trng = 1;
static void tsrand(unsigned seed) { trng = seed; }
static int trand(void)
{
    trng = trng * 6364136223846793005u + 1442695040888963407u;
    return (int)(trng >> 33);
}
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

static CeStore make_store(Reg *r)
{
    CeStore s = { r, reg_get, reg_set, reg_del, reg_flush, reg_legacy };
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
    int check_replay;            /* verify on every animated step: 1 = the card and the board, 2 = the card */
    int ntitle;                  /* set_title calls = deals */
    uint32_t ms;                 /* fake GetTickCount for the game clock */
    int nstatus;                 /* status_changed calls */
    int win_msgs_opt;            /* s->opts.messages while the YouWin dialog was up */
    int confirm_answer, nconfirm, last_confirm_id;   /* ui.confirm (v1.4) */
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
    f->win_msgs_opt = f->s->opts.messages;
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
        if (at != st->card || (f->check_replay == 1 && !board_ok(b))) { fails++; printf("FAIL replay step card mismatch\n"); }
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
static uint32_t ui_now_ms(void *c) { return ((Fake *)c)->ms; }
static void ui_status(void *c) { ((Fake *)c)->nstatus++; }
static int ui_confirm(void *c, int id, const char *t)
{
    Fake *f = c;
    f->nconfirm++;
    f->last_confirm_id = id;
    logf_(f, "confirm%d;", id);
    if (strcmp(t, fcs_string(id))) { fails++; printf("FAIL confirm text\n"); }
    return f->confirm_answer;
}

static FcSessionUI fake_ui(Fake *f)
{
    FcSessionUI u = { f, ui_message, ui_resign, ui_movecol, ui_gamenum, ui_win, ui_lose, ui_cheat,
                      ui_anim, ui_inval, ui_title, ui_cards_left, ui_menu, ui_timer, ui_flash, ui_post,
                      ui_now, ui_now_ms, ui_status, NULL, NULL, NULL, ui_confirm };
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
    f->confirm_answer = 1;
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
    CeStore st = make_store(&r);
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

/* A right-button peek held while a left-click move lands on the peeked column ends with the move
 * (XP repaints the column; a lasting peek would cover the card that just landed). */
static void test_peek_ends_on_move(void)
{
    static const char *const cols[8] = { "KS QH JC", "5S TD", "2S", "3S", "4S", "6S", "7S", "8S" };
    FcSession s; Fake f;
    FcsViewState vs;
    fake_reset(&f, &s);
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, NULL);
    start_game(&s, &f, 1);
    custom(&s, cols, NULL);
    fcs_rbutton_down(&s, 1, 0);                 /* hold the right button on KS (covered) */
    CHECK_EQ(s.peek_col, 1); CHECK_EQ(s.peek_pos, 0);
    fcs_click(&s, 2, 1);                        /* select TD */
    CHECK_EQ(s.peek_col, 1);                    /* a selection alone keeps it */
    fcs_click(&s, 1, 2);                        /* TD onto JC, column 1 */
    CHECK_EQ(s.board.board[1][3], C("TD"));
    CHECK_EQ(s.peek_col, -1); CHECK_EQ(s.peek_pos, -1);
    fcs_view_state(&s, &vs);
    CHECK_EQ(vs.peek_col, -1);
    fcs_rbutton_up(&s);                         /* the late button-up is harmless */
    CHECK_EQ(s.peek_col, -1);
    /* the same when the peeked column is emptied by the move */
    fcs_rbutton_down(&s, 2, 0);                 /* 5S, under nothing now: bottom card, no peek */
    CHECK_EQ(s.peek_col, -1);
    fcs_rbutton_down(&s, 1, 1);                 /* QH */
    CHECK_EQ(s.peek_col, 1);
    fcs_click(&s, 3, 0);                        /* 2S ... */
    fcs_click(&s, 0, 0);                        /* ... to a free cell */
    CHECK_EQ(s.board.board[0][0], C("2S"));
    CHECK_EQ(s.peek_col, -1);
    fcs_free(&s);
}

/* Key '0' with a free-cell card selected silences messages for its deselect without touching the
 * user's option: the deselect can autoplay to a win, and a session end during YouWin saves s->opts. */
static void test_key0_keeps_messages_option(void)
{
    static const char *const fcs[4] = { "AH", NULL, NULL, NULL };
    FcSession s; Fake f;
    fake_reset(&f, &s);
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, NULL);
    start_game(&s, &f, 1);
    custom(&s, (const char *const[8]){ 0 }, fcs);
    s.opts.messages = 1;
    f.win_msgs_opt = -1;
    fcs_char(&s, '0');                          /* select AH in free cell 0 */
    CHECK_EQ(s.sel, 1); CHECK_EQ(s.sel_col, 0);
    fcs_char(&s, '0');                          /* deselect: autoplay takes it home, a win */
    CHECK_EQ(f.nwin, 1);
    CHECK_EQ(f.win_msgs_opt, 1);
    CHECK_EQ(s.opts.messages, 1);
    CHECK_EQ(s.quiet, 0);
    CHECK_EQ(f.nmsg, 0);
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
        CeStore st = make_store(&r);
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
        CeStore st = make_store(&r);
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
    CeStore st = make_store(&r);
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
        CeStore st = make_store(&r);
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
    CeStore st = make_store(&r);
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
    CeStore ms = make_store(&m);
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
    CeStore ks = make_store(&k);
    fc_stats_init(&s, &ks);
    CHECK_EQ(fc_stats_migrate(&s), 1);
    CHECK_EQ(rv(&k, "won"), 3);
    CHECK_EQ(rv(&k, "AlreadyPlayed"), 1);
    /* a store without callbacks: defaults, nothing crashes */
    CeStore none = { 0 };
    fc_stats_init(&s, &none);
    fc_stats_record_win(&s, 3);
    CHECK_EQ(fc_stats_get(&s, "won", 0), 0);
    CHECK_EQ(s.session_won, 1);
}

static void test_options(void)
{
    Reg r = { 0 };
    CeStore st = make_store(&r);
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

/* The move counter equals the counted actions on the undo history (it is never negative). */
static int moves_ok(const FcSession *s)
{
    int n = 0;
    for (int i = 0; i < s->nhist; i++) n += s->hist[i]->counted;
    return s->moves == n && s->moves >= 0;
}

/* extras = 1: the standard supermove rule and the full New Game range are on. */
static void random_playout(unsigned seed, int extras)
{
    tsrand(seed);
    long actions = 0, undone = 0, wins = 0, losses = 0, deals = 0;
    for (int game = 0; game < 150; game++) {
        FcSession s; Fake f; Reg r = { 0 };
        CeStore st = make_store(&r);
        fake_reset(&f, &s);
        f.check_replay = 1;
        FcSessionUI ui = fake_ui(&f);
        if (game & 1) ui.post_command = NULL;   /* follow-ups run directly */
        fcs_init(&s, &ui, &st);
        s.extras.standard_supermove = s.extras.full_range = extras == 1;
        if (extras == 2) {                      /* v1.4: single click, drag and drop, Undo All */
            s.extras.single_click = trand() % 2;
            s.extras.standard_supermove = trand() % 2;
        }
        start_game(&s, &f, 1 + trand() % 1000000);
        FcBoard dealt = s.board;
        int ntitle = f.ntitle, frozen = 0;
        s.opts.messages = trand() % 2;
        s.opts.dblclick = trand() % 2;
        for (int k = 0; k < 500; k++) {
            f.movecol = trand() % 3 - 1;
            f.lose_yes = f.win_yes = trand() % 2;
            f.lose_same = f.win_sel = trand() % 2;
            f.resign = trand() % 2;
            f.gn[0] = trand() % 4 ? 1 + trand() % 1000000 : GN_CANCEL; f.ngn = 1; f.gni = 0;
            int lost0 = f.nlose, won0 = f.nwin;
            int what = trand() % 1000;
            int col = trand() % 10 - 1, pos = trand() % 8;
            if (col < 0) col = FCS_MISS;
            if (col >= 1 && col <= 8) pos = trand() % 3 ? fc_last_index(&s.board, col) : trand() % 8;
            if (extras == 2 && what < 300) {
                /* a press, then a release (a click) or a drag and a drop on a random pile */
                int k2 = trand() % 4, dcol = trand() % 10 - 1, dpos = trand() % 8, first;
                if (dcol < 0) dcol = FCS_MISS;
                if (dcol >= 1) dpos = fc_last_index(&s.board, dcol);
                f.confirm_answer = trand() % 2;
                if (k2 == 3 && trand() % 4 == 0) fcs_command(&s, FCS_CMD_UNDO_ALL);
                else if (fcs_press(&s, col, pos)) {
                    if (k2 == 0) fcs_release(&s);
                    else if (fcs_drag_cards(&s, col, pos, &first) || k2 == 2) {
                        int ok = fcs_drop_ok(&s, dcol, dpos), h0 = s.nhist, moved = fcs_drop(&s, dcol, dpos);
                        if (moved) CHECK(ok);
                        if (!ok && f.movecol != FCS_MOVECOL_CANCEL) CHECK(!moved && s.nhist <= h0 + 1);
                    }
                }
            }
            else if (what < 700) fcs_click(&s, col, pos);
            else if (what < 780) fcs_dblclick(&s, col, pos);
            else if (what < 880) fcs_char(&s, '0' + trand() % 10);
            else if (what < 910) { fcs_rbutton_down(&s, col, pos); fcs_rbutton_up(&s); }
            else if (what < 945) fcs_command(&s, FCS_CMD_UNDO);
            else if (what < 960) fcs_command(&s, FCS_CMD_REDO);
            else if (what < 990) fcs_mouse_move(&s, col, pos, trand() % 2);
            else {
                static const int cmds[4] = { FCS_CMD_NEW, FCS_CMD_SELECT, FCS_CMD_RESTART, FCS_CMD_CHEAT };
                f.cheat = trand() % 5 == 0 ? FCS_CHEAT_WIN : FCS_CHEAT_NONE;
                fcs_command(&s, cmds[trand() % 4]);
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
            if (s.game_number != 0) CHECK(moves_ok(&s));
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
            CHECK_EQ(s.moves, 0);
            undone++;
        }
        fcs_free(&s);
    }
    printf("random playout%s: %ld actions, %ld deals, %ld lost, %ld won, %ld games undone to the deal\n",
           extras == 2 ? " (single click, drag and drop, Undo All)" : extras ? " (standard supermove, full range)" : "",
           actions, deals, losses, wins, undone);
    CHECK(undone > 50);
    CHECK(wins > 0 && losses > 0);
}

static void test_random_playout(void)
{
    random_playout(4242, 0);
    random_playout(777, 1);
    random_playout(1414, 2);
}

/* Greedy legal play with undo stress: many moves, many undos interleaved. */
static void test_undo_stress(void)
{
    tsrand(99);
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
            if (trand() % 5 == 0 && fcs_undo_enabled(&s)) {
                fcs_command(&s, FCS_CMD_UNDO);
                nsnap--;
                CHECK(board_eq(&s.board, &snap[nsnap - 1]));
                continue;
            }
            int h = s.nhist;
            fcs_click(&s, 1 + trand() % 8, 0);
            int col = trand() % 9, row = trand() % 2, slot = trand() % 4;   /* (one call each: order) */
            fcs_click(&s, col, 4 * row + slot);
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
        fcs_click(s, 1 + trand() % 8, 0);
        int col = trand() % 9, row = trand() % 2, slot = trand() % 4;       /* (one call each: order) */
        fcs_click(s, col, 4 * row + slot);
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
    tsrand(7);
    FcBoard snaps[40];
    int n = 0;
    for (int game = 1; game < 50; game++) {     /* a game the random clicks do not lose early */
        int r = 1;
        start_game(&s, &f, game);
        CHECK_EQ(f.redo_en, 0);
        n = 0;
        snaps[0] = s.board;
        while (n < 24 && (r = legal_action(&s)) == 1) snaps[++n] = s.board;
        if (r != -1 && n >= 8) break;
    }
    CHECK(n >= 8 && s.game_number != 0);
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
            int what = trand() % 10;
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

/* ---- v1.1 extras ---------------------------------------------------------------------------------- */

/* Move counter: committed user actions; a supermove is one move, autoplay is not counted, Undo -1,
 * Redo +1, pure deselects and illegal clicks nothing. */
static void test_move_counter(void)
{
    FcSession s; Fake f;
    fake_reset(&f, &s);
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, NULL);
    CHECK_EQ(fcs_moves(&s), 0);
    start_game(&s, &f, 5);
    CHECK_EQ(fcs_moves(&s), 0);
    static const char *const cols[8] = { "KH 4S", "QD 5H", "KC 9H 8S 7H", "KS", "KD", "QS", "TS", "JH" };
    custom(&s, cols, NULL);
    int st0 = f.nstatus;

    fcs_click(&s, 1, 0);                         /* select: nothing */
    fcs_click(&s, 1, 0);                         /* pure deselect: nothing */
    CHECK_EQ(fcs_moves(&s), 0);
    CHECK_EQ(f.nstatus, st0);
    fcs_click(&s, 1, 0);
    fcs_click(&s, 2, 0);                         /* 4S onto 5H */
    CHECK_EQ(fcs_moves(&s), 1);
    CHECK_EQ(f.nstatus, st0 + 1);
    fcs_click(&s, 1, 0);
    fcs_click(&s, 4, 0);                         /* KH onto KS: illegal, message, deselect */
    CHECK_EQ(f.nmsg, 1);
    CHECK_EQ(fcs_moves(&s), 1);
    fcs_click(&s, 3, 0);
    fcs_click(&s, 4, 0);                         /* 9H..7H onto KS: illegal too */
    CHECK_EQ(fcs_moves(&s), 1);

    /* a 3-card supermove is one move (5 single-card steps) */
    int fwd = f.nfwd;
    fcs_click(&s, 3, 0);
    fcs_click(&s, 7, 0);                         /* 9H 8S 7H onto TS */
    CHECK_EQ(f.nfwd - fwd, 5);
    CHECK_EQ(fc_last_index(&s.board, 7), 3);
    CHECK_EQ(fcs_moves(&s), 2);
    /* double click to a free cell */
    fcs_click(&s, 8, 0);
    fcs_dblclick(&s, 8, 0);
    CHECK_EQ(s.board.board[0][0], C("JH"));
    CHECK_EQ(fcs_moves(&s), 3);
    /* undo / redo */
    fcs_command(&s, FCS_CMD_UNDO);
    CHECK_EQ(fcs_moves(&s), 2);
    fcs_command(&s, FCS_CMD_UNDO);
    CHECK_EQ(fcs_moves(&s), 1);
    fcs_command(&s, FCS_CMD_REDO);
    CHECK_EQ(fcs_moves(&s), 2);
    fcs_command(&s, FCS_CMD_UNDO);
    fcs_command(&s, FCS_CMD_UNDO);
    CHECK_EQ(fcs_moves(&s), 0);
    fcs_command(&s, FCS_CMD_UNDO);               /* nothing left */
    CHECK_EQ(fcs_moves(&s), 0);
    fcs_command(&s, FCS_CMD_REDO);
    fcs_command(&s, FCS_CMD_REDO);
    fcs_command(&s, FCS_CMD_REDO);
    CHECK_EQ(fcs_moves(&s), 3);

    /* a move followed by autoplay counts once; a deselect that only autoplays counts nothing (it is
     * undoable, and undoing it changes nothing either) */
    fc_board_clear(&s.board);
    set_col(&s.board, 1, "KS AC");
    set_col(&s.board, 2, "QD 2C 5S");
    set_col(&s.board, 3, "KC");
    set_col(&s.board, 4, "6H");
    fix_cards_left(&s.board);
    int m0 = fcs_moves(&s), h0 = s.nhist;
    fcs_click(&s, 3, 0);
    fcs_click(&s, 3, 0);                         /* deselect: AC goes home (no user step) */
    CHECK_EQ(s.board.board[0][4], C("AC"));
    CHECK_EQ(s.nhist, h0 + 1);
    CHECK_EQ(fcs_moves(&s), m0);
    fcs_command(&s, FCS_CMD_UNDO);
    CHECK_EQ(fcs_moves(&s), m0);
    fcs_command(&s, FCS_CMD_REDO);
    CHECK_EQ(fcs_moves(&s), m0);
    fcs_click(&s, 1, 0);                         /* KS to a free cell */
    fcs_click(&s, 0, 1);
    CHECK_EQ(fcs_moves(&s), m0 + 1);
    fcs_click(&s, 2, 0);                         /* 5S onto 6H, then autoplay sends 2C home */
    fcs_click(&s, 4, 0);
    CHECK_EQ(s.board.board[0][4], C("2C"));
    CHECK_EQ(fcs_moves(&s), m0 + 2);
    CHECK(moves_ok(&s));

    /* a new deal resets it */
    start_game(&s, &f, 6);
    CHECK_EQ(fcs_moves(&s), 0);
    fcs_free(&s);
}

/* Game clock: starts at the first counted move after a deal, stops on win and loss, reset by a deal;
 * Select Game cancelled leaves it running. GetTickCount wraps. */
static void test_game_clock(void)
{
    char buf[32];
    static const struct { uint32_t ms; const char *t; } fmt[] = {
        { 0, "0:00" }, { 999, "0:00" }, { 1000, "0:01" }, { 59999, "0:59" }, { 60000, "1:00" },
        { 252000, "4:12" }, { 3599999, "59:59" }, { 3600000, "1:00:00" }, { 3723000, "1:02:03" },
        { 360000000u, "100:00:00" } };
    for (size_t i = 0; i < sizeof fmt / sizeof fmt[0]; i++) {
        fcs_format_time(fmt[i].ms, buf, sizeof buf);
        CHECK_STR(buf, fmt[i].t);
    }

    FcSession s; Fake f; Reg r = { 0 };
    CeStore st = make_store(&r);
    fake_reset(&f, &s);
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, &st);
    f.ms = 0xFFFFF000u;                          /* 4096 ms before GetTickCount wraps */
    CHECK_EQ(fcs_clock_running(&s), 0);
    CHECK_EQ(fcs_elapsed_ms(&s), 0);
    start_game(&s, &f, 5);
    static const char *const cols[8] = { "KH 4S", "QD 5H", "KS", "KC", "KD", "QS", "QC", "JH" };
    custom(&s, cols, NULL);
    f.ms += 5000;
    fcs_click(&s, 1, 0);                         /* selecting and deselecting do not start it */
    fcs_click(&s, 1, 0);
    CHECK_EQ(fcs_clock_running(&s), 0);
    CHECK_EQ(fcs_elapsed_ms(&s), 0);
    int st0 = f.nstatus;
    fcs_click(&s, 1, 0);
    fcs_click(&s, 2, 0);                         /* the first move */
    CHECK_EQ(fcs_clock_running(&s), 1);
    CHECK(f.nstatus > st0);
    CHECK_EQ(fcs_elapsed_ms(&s), 0);
    f.ms += 252000;                              /* across the wrap */
    CHECK_EQ(fcs_elapsed_ms(&s), 252000);
    fcs_command(&s, FCS_CMD_UNDO);               /* undo keeps it running */
    CHECK_EQ(fcs_clock_running(&s), 1);
    f.ms += 1000;
    CHECK_EQ(fcs_elapsed_ms(&s), 253000);
    f.gn[0] = GN_CANCEL; f.ngn = 1; f.gni = 0;
    fcs_command(&s, FCS_CMD_SELECT);             /* resign yes, then cancel: nothing changes */
    CHECK_EQ(s.game_number, 5);
    CHECK_EQ(fcs_clock_running(&s), 1);
    CHECK_EQ(fcs_moves(&s), 0);

    /* win: stops (the shown time stays), the next deal resets */
    fcs_command(&s, FCS_CMD_REDO);
    CHECK_EQ(fcs_moves(&s), 1);
    near_win(&s);
    f.ms += 2000;
    fcs_click(&s, 1, 0);
    fcs_click(&s, 0, 7);                         /* KS home: a counted move, then the win */
    CHECK_EQ(f.nwin, 1);
    CHECK_EQ(fcs_clock_running(&s), 0);
    CHECK_EQ(fcs_moves(&s), 2);
    CHECK_EQ(fcs_elapsed_ms(&s), 255000);
    f.ms += 60000;
    CHECK_EQ(fcs_elapsed_ms(&s), 255000);        /* frozen */
    start_game(&s, &f, 8);
    CHECK_EQ(fcs_clock_running(&s), 0);
    CHECK_EQ(fcs_elapsed_ms(&s), 0);
    CHECK_EQ(fcs_moves(&s), 0);

    /* loss (cheat "lose"): stops too */
    custom(&s, cols, NULL);
    fcs_click(&s, 1, 0);
    fcs_click(&s, 2, 0);
    CHECK_EQ(fcs_clock_running(&s), 1);
    f.ms += 7000;
    f.cheat = FCS_CHEAT_LOSE;
    fcs_command(&s, FCS_CMD_CHEAT);
    fcs_click(&s, 3, 0);
    fcs_click(&s, 3, 0);
    CHECK_EQ(f.nlose, 1);
    CHECK_EQ(fcs_clock_running(&s), 0);
    CHECK_EQ(fcs_elapsed_ms(&s), 7000);
    CHECK_EQ(fcs_moves(&s), 1);
    /* restart: reset */
    fcs_command(&s, FCS_CMD_RESTART);
    CHECK_EQ(s.game_number, 8);
    CHECK_EQ(fcs_elapsed_ms(&s), 0);
    CHECK_EQ(fcs_moves(&s), 0);
    /* a new game while running (resign): reset and stopped */
    custom(&s, cols, NULL);
    fcs_click(&s, 1, 0);
    fcs_click(&s, 2, 0);
    f.ms += 3000;
    CHECK_EQ(fcs_elapsed_ms(&s), 3000);
    fcs_command(&s, FCS_CMD_NEW);
    CHECK(s.game_number != 8);
    CHECK_EQ(fcs_clock_running(&s), 0);
    CHECK_EQ(fcs_elapsed_ms(&s), 0);
    /* without a clock callback the time stays 0 */
    ui.now_ms = NULL;
    FcSession s2;
    fcs_init(&s2, &ui, NULL);
    start_game(&s2, &f, 5);
    custom(&s2, cols, NULL);
    fcs_click(&s2, 1, 0);
    fcs_click(&s2, 2, 0);
    CHECK_EQ(fcs_clock_running(&s2), 1);
    CHECK_EQ(fcs_elapsed_ms(&s2), 0);
    fcs_free(&s2);
    fcs_free(&s);
}

/* Standard supermove rule through the controller: capacity messages (307) with the standard numbers,
 * the move itself, the Move-to-Empty-Column dialog, the cursor. XP's rule stays the default. */
static void test_standard_supermove(void)
{
    static const char *const fill[4] = { "KS", "KH", "KD", "KC" };
    static const char *const fillc[6] = { "QS", "QH", "QD", "QC", "JS", "JH" };
    /* 7-card run 9H..3H onto TC: XP's table vs the standard one */
    static const struct { int f, e, std; const char *msg; } t[] = {
        { 1, 2, 0, "That move requires moving 7 cards. You only have enough free space to move 6." },
        { 1, 2, 1, NULL },                                       /* 2 * 4 = 8 */
        { 1, 1, 1, "That move requires moving 7 cards. You only have enough free space to move 4." },
        { 0, 2, 1, "That move requires moving 7 cards. You only have enough free space to move 4." },
        { 0, 3, 1, NULL },                                       /* 1 * 8 */
        { 4, 0, 1, "That move requires moving 7 cards. You only have enough free space to move 5." },
        { 2, 1, 1, "That move requires moving 7 cards. You only have enough free space to move 6." },
        { 3, 1, 1, NULL },
        { 0, 1, 1, "That move requires moving 7 cards. You only have enough free space to move 2." } };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        FcSession s; Fake f;
        fake_reset(&f, &s);
        FcSessionUI ui = fake_ui(&f);
        fcs_init(&s, &ui, NULL);
        s.extras.standard_supermove = t[i].std;
        f.check_replay = 2;
        start_game(&s, &f, 5);
        const char *cols[8] = { "9H 8S 7H 6S 5H 4S 3H", "TC" }, *fcs[4] = { 0 };
        for (int k = 0; k < 4 - t[i].f; k++) fcs[k] = fill[k];
        for (int c = 3; c <= 8 - t[i].e; c++) cols[c - 1] = fillc[c - 3];
        custom(&s, cols, fcs);
        FcBoard before = s.board;
        fcs_click(&s, 1, 3);
        CHECK_EQ(fcs_cursor(&s, 2, 0, 1), t[i].msg ? FCS_CURSOR_ARROW : FCS_CURSOR_DOWNARROW);
        fcs_click(&s, 2, 0);
        if (t[i].msg) {
            CHECK_EQ(f.nmsg, 1);
            CHECK_EQ(f.last_msg_id, 307);
            CHECK_STR(f.last_msg, t[i].msg);
            CHECK_EQ(fc_last_index(&s.board, 1), 6);
        } else {
            CHECK_EQ(f.nmsg, 0);
            CHECK_EQ(fc_last_index(&s.board, 1), -1);
            CHECK_EQ(fc_last_index(&s.board, 2), 7);
            CHECK_EQ(s.board.board[2][7], C("3H"));
            CHECK_EQ(fc_free_cells_empty(&s.board), t[i].f);
            CHECK_EQ(fc_empty_columns(&s.board), t[i].e + 1);
            CHECK_EQ(fcs_moves(&s), 1);
            fcs_command(&s, FCS_CMD_UNDO);
            CHECK(board_eq(&s.board, &before));
            CHECK_EQ(fcs_moves(&s), 0);
        }
        fcs_free(&s);
    }
    /* Move to Empty Column: "Move column" moves min(run, (f+1)*2^(e-1)); the dialog shows whenever that
     * is at least 2 (with no free cell too, unlike XP) */
    static const struct { int f, e, std, answer, moved, dialog; } m[] = {
        { 3, 1, 0, FCS_MOVECOL_COLUMN, 4, 1 },   /* XP */
        { 1, 2, 0, FCS_MOVECOL_COLUMN, 2, 1 },   /* XP: free cells only */
        { 0, 2, 0, FCS_MOVECOL_COLUMN, 1, 0 },   /* XP: no free cell, no dialog */
        { 3, 1, 1, FCS_MOVECOL_COLUMN, 4, 1 },
        { 1, 2, 1, FCS_MOVECOL_COLUMN, 4, 1 },   /* 2 * 2: through the other empty column */
        { 1, 3, 1, FCS_MOVECOL_COLUMN, 7, 1 },   /* 2 * 4 = 8 >= the whole run */
        { 0, 2, 1, FCS_MOVECOL_COLUMN, 2, 1 },
        { 0, 3, 1, FCS_MOVECOL_COLUMN, 4, 1 },
        { 0, 1, 1, FCS_MOVECOL_COLUMN, 1, 0 },   /* capacity 1: single card, no dialog */
        { 2, 3, 1, FCS_MOVECOL_SINGLE, 1, 1 },
        { 2, 3, 1, FCS_MOVECOL_CANCEL, 0, 1 } };
    for (size_t i = 0; i < sizeof m / sizeof m[0]; i++) {
        FcSession s; Fake f;
        fake_reset(&f, &s);
        FcSessionUI ui = fake_ui(&f);
        fcs_init(&s, &ui, NULL);
        s.extras.standard_supermove = m[i].std;
        f.check_replay = 2;
        start_game(&s, &f, 5);
        f.movecol = m[i].answer;
        const char *cols[8] = { "9H 8S 7H 6S 5H 4S 3H", "TC" }, *fcs[4] = { 0 };
        for (int k = 0; k < 4 - m[i].f; k++) fcs[k] = fill[k];
        for (int c = 3; c <= 8 - m[i].e; c++) cols[c - 1] = fillc[c - 3];
        custom(&s, cols, fcs);
        fcs_click(&s, 1, 6);
        fcs_click(&s, 8, 0);                     /* the (last) empty column */
        CHECK_EQ(f.nmovecol, m[i].dialog);
        CHECK_EQ(fc_last_index(&s.board, 8) + 1, m[i].moved);
        CHECK_EQ(fc_last_index(&s.board, 1) + 1, 7 - m[i].moved);
        if (m[i].moved > 0) CHECK_EQ(s.board.board[8][m[i].moved - 1], C("3H"));
        CHECK_EQ(fc_free_cells_empty(&s.board), m[i].f);
        CHECK_EQ(fc_empty_columns(&s.board), m[i].e - (m[i].moved > 0) + (m[i].moved == 7));
        CHECK_EQ(f.nmsg, 0);
        CHECK_EQ(fcs_moves(&s), m[i].moved > 0);
        fcs_free(&s);
    }
}

/* New Game range: XP's 1..32767 by default, 1..1000000 with the option; still never the same number
 * twice in a row. */
static void test_deal_range(void)
{
    for (int full = 0; full <= 1; full++) {
        FcSession s; Fake f;
        fake_reset(&f, &s);
        FcSessionUI ui = fake_ui(&f);
        ui.confirm_resign = NULL;
        fcs_init(&s, &ui, NULL);
        s.extras.full_range = full;
        int big = 0, prev = 0, ok = 1, repeat = 0;
        for (int i = 0; i < 3000; i++) {
            f.now = 1700000000u + (uint32_t)(i / 3);     /* several requests in the same second */
            fcs_command(&s, FCS_CMD_NEW);
            int n = s.game_number;
            ok &= n >= 1 && n <= (full ? 1000000 : 32767);
            big += n > 32767;
            repeat += n == prev;
            prev = n;
        }
        CHECK(ok);
        CHECK_EQ(repeat, 0);
        if (full) CHECK(big > 2800);             /* ~96.7% expected above 32767 */
        else CHECK_EQ(big, 0);
        /* Select Game's suggestion uses the same generator */
        f.gn[0] = GN_CANCEL; f.ngn = 1; f.gni = 0;
        fcs_command(&s, FCS_CMD_SELECT);
        CHECK(f.gn_init[0] >= 1 && f.gn_init[0] <= (full ? 1000000 : 32767));
        fcs_free(&s);
    }
    /* uniformity of the full-range generator: deciles of 200000 seeds */
    int bins[10] = { 0 };
    for (uint32_t t = 0; t < 200000u; t++) {
        int n = fc_random_game_number_full(1600000000u + t * 7u);
        if (n < 1 || n > 1000000) { CHECK(0); break; }
        bins[(n - 1) / 100000]++;
    }
    for (int i = 0; i < 10; i++) CHECK(bins[i] > 19000 && bins[i] < 21000);
    CHECK_EQ(fc_random_game_number_full(1), fc_random_game_number_full(1));   /* deterministic */
}

/* ---- won deals --------------------------------------------------------------------------------------- */

typedef struct MemFile {
    uint8_t data[FC_WON_MAX_FILE + 64];
    long len;                /* -1 = no file */
    int nwrite, fail_write;
} MemFile;

static long mem_read(void *ctx, void *buf, size_t cap)
{
    MemFile *m = ctx;
    if (m->len < 0) return -1;
    if ((size_t)m->len > cap) return (long)cap + 1;
    memcpy(buf, m->data, (size_t)m->len);
    return m->len;
}

static int mem_write(void *ctx, const void *data, size_t len)
{
    MemFile *m = ctx;
    if (m->fail_write || len > sizeof m->data) return 0;
    memcpy(m->data, data, len);
    m->len = (long)len;
    m->nwrite++;
    return 1;
}

static void test_won_deals(void)
{
    FcWonDeals w, w2;
    fc_won_init(&w);
    fc_won_init(&w2);
    CHECK_EQ(fc_won_count(&w), 0);
    CHECK(!fc_won_has(&w, 1));
    CHECK_EQ(fc_won_add(&w, 0), 0);              /* not a game */
    CHECK_EQ(fc_won_add(&w, -3), 0);
    CHECK_EQ(fc_won_add(&w, 1000001), 0);
    CHECK_EQ(fc_won_count(&w), 0);
    static const int games[] = { 1, 617, 32767, 32768, 999999, 1000000, -1, -2, 11982 };
    for (size_t i = 0; i < sizeof games / sizeof games[0]; i++) CHECK_EQ(fc_won_add(&w, games[i]), 1);
    CHECK_EQ(fc_won_add(&w, 617), 0);            /* already there */
    CHECK_EQ(fc_won_count(&w), 9);
    for (size_t i = 0; i < sizeof games / sizeof games[0]; i++) CHECK(fc_won_has(&w, games[i]));
    CHECK(!fc_won_has(&w, 0));
    CHECK(!fc_won_has(&w, 2));
    CHECK(!fc_won_has(&w, 616));
    CHECK(!fc_won_has(&w, -3));

    /* file image: round trip; size follows the highest won game */
    static uint8_t buf[FC_WON_MAX_FILE];
    size_t n = fc_won_serialize(&w, buf);
    CHECK_EQ(n, FC_WON_MAX_FILE);                /* 1000000 is won: the whole bitset */
    CHECK(memcmp(buf, "FCWD", 4) == 0);
    CHECK_EQ(fc_won_deserialize(&w2, buf, n), 1);
    CHECK_EQ(fc_won_count(&w2), 9);
    for (size_t i = 0; i < sizeof games / sizeof games[0]; i++) CHECK(fc_won_has(&w2, games[i]));
    CHECK(!fc_won_has(&w2, 618));
    fc_won_free(&w2);
    FcWonDeals small;
    fc_won_init(&small);
    fc_won_add(&small, -2);
    fc_won_add(&small, 100);
    n = fc_won_serialize(&small, buf);
    CHECK_EQ(n, FC_WON_HEADER + (102 + 1 + 7) / 8); /* index 102 is the highest */
    CHECK_EQ(fc_won_deserialize(&w2, buf, n), 1);
    CHECK(fc_won_has(&w2, -2) && fc_won_has(&w2, 100) && !fc_won_has(&w2, -1));
    CHECK_EQ(fc_won_count(&w2), 2);
    fc_won_free(&w2);
    FcWonDeals empty;
    fc_won_init(&empty);
    n = fc_won_serialize(&empty, buf);
    CHECK_EQ(n, FC_WON_HEADER + 1);
    CHECK_EQ(fc_won_deserialize(&w2, buf, n), 1);
    CHECK_EQ(fc_won_count(&w2), 0);
    fc_won_free(&w2);

    /* damage of every kind is rejected and leaves the set unchanged */
    n = fc_won_serialize(&small, buf);
    fc_won_add(&w2, 5);
    for (size_t k = 0; k < n; k++) {             /* any single flipped bit */
        for (int bit = 0; bit < 8; bit++) {
            buf[k] ^= (uint8_t)(1u << bit);
            int r = fc_won_deserialize(&w2, buf, n);
            buf[k] ^= (uint8_t)(1u << bit);
            if (r) { CHECK(!r); k = n; break; }
        }
    }
    CHECK_EQ(fc_won_count(&w2), 1);
    CHECK(fc_won_has(&w2, 5));
    CHECK_EQ(fc_won_deserialize(&w2, buf, n - 1), 0);      /* truncated */
    CHECK_EQ(fc_won_deserialize(&w2, buf, 10), 0);
    CHECK_EQ(fc_won_deserialize(&w2, buf, 0), 0);
    CHECK_EQ(fc_won_deserialize(&w2, NULL, 0), 0);
    buf[n] = 0;
    CHECK_EQ(fc_won_deserialize(&w2, buf, n + 1), 0);      /* trailing garbage */
    CHECK_EQ(fc_won_deserialize(&w2, buf, n), 1);          /* the intact image still loads */
    fc_won_free(&w2);

    /* persistence through the I/O interface: missing file, saved file, damaged file */
    static MemFile mf;
    memset(&mf, 0, sizeof mf);
    mf.len = -1;
    CeBlobIO io = { &mf, mem_read, mem_write };
    CHECK_EQ(fc_won_load(&w2, &io), 0);
    CHECK_EQ(fc_won_count(&w2), 0);
    CHECK_EQ(fc_won_save(&w, &io), 1);
    CHECK_EQ(fc_won_load(&w2, &io), 1);
    CHECK_EQ(fc_won_count(&w2), 9);
    CHECK(fc_won_has(&w2, 1000000) && fc_won_has(&w2, -1));
    mf.data[30] ^= 0x10;
    CHECK_EQ(fc_won_load(&w2, &io), -1);
    CHECK_EQ(fc_won_count(&w2), 0);
    mf.len = (long)FC_WON_MAX_FILE + 10;         /* too big */
    CHECK_EQ(fc_won_load(&w2, &io), -1);
    CHECK_EQ(fc_won_load(&w2, NULL), 0);
    fc_won_free(&w2);
    fc_won_free(&small);
    fc_won_free(&w);

    /* the session: a win (incl. -1/-2 and the cheat) adds the game and saves once */
    memset(&mf, 0, sizeof mf);
    mf.len = -1;
    FcSession s; Fake f; Reg r = { 0 };
    CeStore st = make_store(&r);
    fake_reset(&f, &s);
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, &st);
    CHECK_EQ(fcs_attach_won_deals(&s, &io), 0);
    CHECK_EQ(fcs_won_count(&s), 0);
    start_game(&s, &f, 7);
    near_win(&s);
    fcs_click(&s, 1, 0);
    fcs_click(&s, 1, 0);
    CHECK_EQ(f.nwin, 1);
    CHECK(fcs_won_before(&s, 7));
    CHECK_EQ(fcs_won_count(&s), 1);
    CHECK_EQ(mf.nwrite, 1);
    fcs_command(&s, FCS_CMD_RESTART);            /* the same deal again: no new entry, no write */
    near_win(&s);
    fcs_click(&s, 1, 0);
    fcs_click(&s, 1, 0);
    CHECK_EQ(f.nwin, 2);
    CHECK_EQ(fcs_won_count(&s), 1);
    CHECK_EQ(mf.nwrite, 1);
    start_game(&s, &f, -1);                       /* -1 won through the cheat */
    f.cheat = FCS_CHEAT_WIN;
    fcs_command(&s, FCS_CMD_CHEAT);
    fcs_click(&s, 1, 0);
    fcs_click(&s, 1, 0);
    CHECK_EQ(f.nwin, 3);
    CHECK(fcs_won_before(&s, -1));
    CHECK(!fcs_won_before(&s, -2));
    CHECK_EQ(mf.nwrite, 2);
    start_game(&s, &f, -2);                       /* a loss adds nothing */
    f.cheat = FCS_CHEAT_LOSE;
    fcs_command(&s, FCS_CMD_CHEAT);
    fcs_click(&s, 1, 0);
    fcs_click(&s, 1, 0);
    CHECK_EQ(f.nlose, 1);
    CHECK(!fcs_won_before(&s, -2));
    CHECK_EQ(fcs_won_count(&s), 2);
    fcs_free(&s);
    /* the next run sees them; a failed write keeps the set in memory */
    FcSession s3;
    fcs_init(&s3, &ui, &st);
    CHECK_EQ(fcs_attach_won_deals(&s3, &io), 1);
    CHECK_EQ(fcs_won_count(&s3), 2);
    CHECK(fcs_won_before(&s3, 7) && fcs_won_before(&s3, -1));
    mf.fail_write = 1;
    f.s = &s3;
    start_game(&s3, &f, 9);
    near_win(&s3);
    fcs_click(&s3, 1, 0);
    fcs_click(&s3, 1, 0);
    CHECK(fcs_won_before(&s3, 9));
    CHECK_EQ(fcs_won_count(&s3), 3);
    fcs_free(&s3);
}

/* Extras persistence: our own store, every value written, defaults off. */

/* ---- v1.4 extras ---------------------------------------------------------------------------------- */

static void set_free(FcBoard *b, int i, const char *card) { b->board[0][i] = card ? C(card) : FC_EMPTY; }

/* Single click: the best place, in the documented order; nowhere: selected (XP); its double-click. */
static void test_single_click(void)
{
    FcSession s; Fake f;
    fake_reset(&f, &s);
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, NULL);
    start_game(&s, &f, 5);
    s.opts.messages = 0;
    s.extras.single_click = 1;
    int dc, dp;

    /* 1. an ace: home (safe), as one counted move */
    static const char *const a1[8] = { "KH AS", "QD 5H", "KS", "KC", "KD", "QS", "QC", "JH" };
    custom(&s, a1, NULL);
    fcs_click(&s, 1, 0);
    CHECK_EQ(s.board.board[0][4], C("AS"));
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(s.nhist, 1);
    CHECK_EQ(fcs_moves(&s), 1);
    CHECK_EQ(s.click_moved, 1);
    fcs_dblclick(&s, 1, 0);                     /* the double-click that follows: ignored */
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(s.board.board[1][0], C("KH"));
    CHECK_EQ(s.nhist, 1);
    fcs_command(&s, FCS_CMD_UNDO);
    CHECK_EQ(s.board.board[1][1], C("AS"));

    /* 2. 3H could go home (2H there) but is not safe (no black twos home): a column first */
    static const char *const a2[8] = { "KC 3H", "QD 4S", "KS", "KD", "QS", "QC", "JH", "TH" };
    custom(&s, a2, NULL);
    set_home(&s.board, 4, "2H");
    s.board.home_rank[2] = 1;
    fix_cards_left(&s.board);
    s.sel = 1; s.sel_col = 1; s.sel_pos = 1;
    CHECK(fcs_single_dest(&s, &dc, &dp) && dc == 2);
    s.sel = 0; s.sel_col = s.sel_pos = -1;
    fcs_click(&s, 1, 1);
    CHECK_EQ(s.board.board[2][2], C("3H"));
    /* 3. no column: an empty column (the column is not one run: KC 3H) */
    custom(&s, (const char *const[8]){ "KC 3H", "QD 5S", "KS", "KD", "QS", "QC", "JH", NULL }, NULL);
    set_home(&s.board, 4, "2H");
    fix_cards_left(&s.board);
    fcs_click(&s, 1, 1);
    CHECK_EQ(s.board.board[8][0], C("3H"));
    /* 4. no column, no empty column: home after all (before a free cell) */
    custom(&s, (const char *const[8]){ "KC 3H", "QD 5S", "KS", "KD", "QS", "QC", "JH", "TH" }, NULL);
    set_home(&s.board, 4, "2H");
    fix_cards_left(&s.board);
    fcs_click(&s, 1, 1);
    CHECK_EQ(s.board.board[0][4], C("3H"));
    /* 5. nothing else: the leftmost free cell; a whole-run column never to an empty column */
    custom(&s, (const char *const[8]){ "KC 3H", "QD 5S", "KS", "KD", "QS", "QC", "JH", NULL }, NULL);
    set_free(&s.board, 0, "9D");
    fix_cards_left(&s.board);
    fcs_click(&s, 4, 0);                        /* KD: a lone card (one run): a free cell, not column 8 */
    CHECK_EQ(s.board.board[0][1], C("KD"));
    CHECK_EQ(s.board.board[8][0], FC_EMPTY);
    /* a free cell's card: onto a column, else an empty column, never another free cell */
    custom(&s, (const char *const[8]){ "KC 3H", "QD 5S", "KS", "KD", "QS", "QC", "TS", "8H" }, NULL);
    set_free(&s.board, 2, "9D");
    fix_cards_left(&s.board);
    fcs_click(&s, 0, 2);
    CHECK_EQ(s.board.board[7][1], C("9D"));
    custom(&s, (const char *const[8]){ "KC 3H", "QD 5S", "KS", "KD", "QS", "QC", "JH", "TH" }, NULL);
    set_free(&s.board, 2, "9D");
    fix_cards_left(&s.board);
    fcs_click(&s, 0, 2);                        /* nowhere: selected, as XP's click */
    CHECK_EQ(s.sel, 1);
    CHECK_EQ(s.sel_col, 0);
    CHECK_EQ(s.click_moved, 0);
    fcs_click(&s, 0, 3);                        /* the next click is XP's destination click */
    CHECK_EQ(s.board.board[0][3], C("9D"));
    CHECK_EQ(s.sel, 0);
    /* a run: XP's count onto the column that fits; the empty column asks "Move to Empty Column..." */
    custom(&s, (const char *const[8]){ "KC 9H 8S 7H", "QD 5S", "KS", "KD", "QS", "QC", "TS", "JH" }, NULL);
    fcs_click(&s, 1, 2);
    CHECK_EQ(fc_last_index(&s.board, 7), 3);    /* 9H 8S 7H onto TS */
    CHECK_EQ(s.board.board[7][3], C("7H"));
    custom(&s, (const char *const[8]){ "KC 9H 8S 7H", "QD 5S", "KS", "KD", "QS", "QC", "JD", NULL }, NULL);
    f.movecol = FCS_MOVECOL_COLUMN;
    int nmc = f.nmovecol;
    fcs_click(&s, 1, 3);
    CHECK_EQ(f.nmovecol, nmc + 1);
    CHECK_EQ(fc_last_index(&s.board, 8), 2);
    /* off: XP's click selects */
    s.extras.single_click = 0;
    custom(&s, (const char *const[8]){ "KH 4S", "QD 5H", "KS", "KC", "KD", "QS", "QC", "JH" }, NULL);
    fcs_click(&s, 1, 0);
    CHECK_EQ(s.sel, 1);
    fcs_dblclick(&s, 1, 0);                     /* XP: the double-click sends it to a free cell */
    CHECK_EQ(s.board.board[0][0], C("4S"));
    fcs_free(&s);
}

/* Drag and drop: the press selects (XP), the cards a drag lifts, the drop is XP's move there. */
static void test_drag_drop(void)
{
    FcSession s; Fake f;
    fake_reset(&f, &s);
    f.check_replay = 2;                         /* (small boards: the card only) */
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, NULL);
    start_game(&s, &f, 5);
    s.extras.drag_drop = 1;
    int first;
    custom(&s, (const char *const[8]){ "AS 9H 8S 7H", "QD 5S", "KS", "KD", "QS", "QC", "TS", "JH" }, NULL);
    set_free(&s.board, 1, "4D");
    fix_cards_left(&s.board);
    CHECK_EQ(fcs_press(&s, 1, 1), 1);           /* 9H: the column is selected (its bottom card) */
    CHECK(s.sel && s.sel_col == 1 && s.sel_pos == 3);
    CHECK_EQ(fcs_drag_cards(&s, 1, 1, &first), 3);
    CHECK_EQ(first, 1);
    CHECK_EQ(fcs_drag_cards(&s, 1, 3, &first), 1);
    CHECK_EQ(fcs_drag_cards(&s, 1, 0, &first), 0);   /* AS: above the run */
    CHECK_EQ(fcs_drag_cards(&s, 2, 1, &first), 0);   /* not the selection */
    CHECK(fcs_drop_ok(&s, 7, 0));               /* 9H 8S 7H onto TS */
    CHECK(!fcs_drop_ok(&s, 2, 1));              /* 5S */
    CHECK(!fcs_drop_ok(&s, 1, 3));              /* its own column */
    CHECK(!fcs_drop_ok(&s, FCS_MISS, -1));
    CHECK(fcs_drop_ok(&s, 0, 0));               /* an empty free cell: the bottom card */
    CHECK(!fcs_drop_ok(&s, 0, 1));              /* a full one */
    CHECK(fcs_drop_ok(&s, 0, 4) == 0);          /* 7H home: no */
    int fwd = f.nfwd;
    CHECK_EQ(fcs_drop(&s, 7, 0), 1);
    CHECK_EQ(s.board.board[7][3], C("7H"));
    CHECK_EQ(s.board.board[0][4], C("AS"));     /* uncovered, and autoplayed home ... */
    CHECK_EQ(f.nfwd - fwd, 1);                  /* ... the only card flown: the dropped ones are there */
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(s.nhist, 1);
    CHECK_EQ(fcs_moves(&s), 1);
    fcs_command(&s, FCS_CMD_UNDO);              /* one Undo, flown back as any action */
    CHECK_EQ(s.board.board[1][3], C("7H"));

    /* a free cell's card */
    CHECK_EQ(fcs_press(&s, 0, 1), 1);
    CHECK_EQ(fcs_drag_cards(&s, 0, 1, &first), 1);
    CHECK(!fcs_drop_ok(&s, 0, 1));              /* onto itself */
    CHECK(fcs_drop_ok(&s, 2, 1));               /* 4D onto 5S */
    CHECK_EQ(fcs_drop(&s, 2, 1), 1);
    CHECK_EQ(s.board.board[2][2], C("4D"));
    fcs_command(&s, FCS_CMD_UNDO);

    /* refused: the messages as a click there (on: the box, deselected; off: silent, still selected) */
    fcs_press(&s, 1, 2);
    CHECK_EQ(fcs_drop(&s, 2, 1), 0);
    CHECK_EQ(f.last_msg_id, 306);
    CHECK_EQ(s.sel, 0);
    s.opts.messages = 0;
    fcs_press(&s, 1, 2);
    CHECK_EQ(fcs_drop(&s, 2, 1), 0);
    CHECK_EQ(s.sel, 1);
    CHECK_EQ(fcs_drop(&s, FCS_MISS, -1), 0);    /* dropped nowhere: deselected (XP's click on a miss) */
    CHECK_EQ(s.sel, 0);
    /* too many cards: the supermove limit (no free cell, no empty column: 1) */
    custom(&s, (const char *const[8]){ "AS 9H 8S 7H", "QD 5S", "KS", "KD", "QS", "QC", "TS", "JH" },
           (const char *const[4]){ "4D", "4C", "3D", "3C" });
    fcs_press(&s, 1, 1);
    CHECK(!fcs_drop_ok(&s, 7, 0));
    s.opts.messages = 1;
    CHECK_EQ(fcs_drop(&s, 7, 0), 0);
    CHECK_EQ(f.last_msg_id, 307);
    /* an empty column: XP's dialog; Cancel moves nothing */
    custom(&s, (const char *const[8]){ "AS 9H 8S 7H", "QD 5S", "KS", "KD", "QS", "QC", "JD", NULL }, NULL);
    fcs_press(&s, 1, 1);
    CHECK(fcs_drop_ok(&s, 8, -1));
    f.movecol = FCS_MOVECOL_CANCEL;
    int nmc = f.nmovecol;
    CHECK_EQ(fcs_drop(&s, 8, -1), 0);
    CHECK_EQ(f.nmovecol, nmc + 1);
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(fc_last_index(&s.board, 8), -1);
    fcs_press(&s, 1, 1);
    f.movecol = FCS_MOVECOL_COLUMN;
    fwd = f.nfwd;
    CHECK_EQ(fcs_drop(&s, 8, -1), 1);
    CHECK_EQ(fc_last_index(&s.board, 8), 2);
    CHECK_EQ(s.board.board[8][0], C("9H"));
    CHECK_EQ(f.nfwd - fwd, 1);                  /* (AS home) */

    /* the release: XP keeps the selection; with single click on the card moves */
    custom(&s, (const char *const[8]){ "KC 9H 8S 7H", "QD 5S", "KS", "KD", "QS", "QC", "TS", "JH" }, NULL);
    CHECK_EQ(fcs_press(&s, 1, 3), 1);
    CHECK_EQ(fcs_release(&s), 0);
    CHECK_EQ(s.sel, 1);
    CHECK_EQ(fcs_press(&s, 7, 0), 0);           /* a press with a selection: XP's destination click */
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(s.board.board[7][3], C("7H"));
    fcs_command(&s, FCS_CMD_UNDO);
    s.extras.single_click = 1;
    int h0 = s.nhist;
    CHECK_EQ(fcs_press(&s, 1, 3), 1);           /* the press selects only ... */
    CHECK_EQ(s.board.board[1][3], C("7H"));
    CHECK_EQ(fcs_release(&s), 1);               /* ... the release (a click) moves */
    CHECK_EQ(s.board.board[7][3], C("7H"));
    fcs_dblclick(&s, 7, 3);                     /* its double-click: ignored */
    CHECK_EQ(s.sel, 0);
    CHECK_EQ(s.nhist, h0 + 1);
    fcs_free(&s);
}

/* Undo All: asked first; back to the deal at once (no flights); the next Redo brings it all back. */
static void test_undo_all(void)
{
    FcSession s; Fake f;
    fake_reset(&f, &s);
    f.check_replay = 1;
    FcSessionUI ui = fake_ui(&f);
    fcs_init(&s, &ui, NULL);
    s.opts.messages = 0;
    tsrand(31);
    int n = 0, r = 1;
    FcBoard dealt;
    for (int game = 3; game < 60; game++) {
        start_game(&s, &f, game);
        dealt = s.board;
        n = 0;
        while (n < 16 && (r = legal_action(&s)) == 1) n++;
        if (r != -1 && n >= 6) break;
    }
    CHECK(n >= 6 && s.game_number != 0);
    fcs_command(&s, FCS_CMD_UNDO);              /* one already on the redo stack */
    FcBoard end = s.board;
    int nh = s.nhist, moves = fcs_moves(&s), nconf = f.nconfirm;
    f.confirm_answer = 0;
    fcs_command(&s, FCS_CMD_UNDO_ALL);          /* No */
    CHECK_EQ(f.nconfirm, nconf + 1);
    CHECK_EQ(f.last_confirm_id, FCS_STR_UNDO_ALL);
    CHECK_EQ(s.nhist, nh);
    CHECK(board_eq(&s.board, &end));
    f.confirm_answer = 1;
    int back = f.nback, fwd = f.nfwd;
    fcs_command(&s, FCS_CMD_UNDO_ALL);
    CHECK(board_eq(&s.board, &dealt));
    CHECK_EQ(s.nhist, 0);
    CHECK_EQ(s.nredo, nh + 1);
    CHECK_EQ(s.redo_group, nh);
    CHECK_EQ(f.nback, back);                    /* not flown */
    CHECK_EQ(fcs_moves(&s), 0);
    CHECK_EQ(f.undo_en, 0);
    CHECK_EQ(f.redo_en, 1);
    CHECK_EQ(f.cards_left, dealt.cards_left);
    nconf = f.nconfirm;
    fcs_command(&s, FCS_CMD_UNDO_ALL);          /* nothing to undo: not asked */
    CHECK_EQ(f.nconfirm, nconf);
    fcs_command(&s, FCS_CMD_REDO);              /* everything back at once */
    CHECK(board_eq(&s.board, &end));
    CHECK_EQ(s.nhist, nh);
    CHECK_EQ(s.nredo, 1);
    CHECK_EQ(s.redo_group, 0);
    CHECK_EQ(f.nfwd, fwd);
    CHECK_EQ(fcs_moves(&s), moves);
    CHECK_EQ(f.cards_left, end.cards_left);
    fcs_command(&s, FCS_CMD_REDO);              /* then the single one, flown as ever */
    CHECK_EQ(s.nredo, 0);
    CHECK_EQ(s.nhist, nh + 1);
    /* Undo All, then a new action: the group is gone */
    fcs_command(&s, FCS_CMD_UNDO_ALL);
    CHECK(s.redo_group > 0);
    r = legal_action(&s);
    CHECK(r != 0);
    CHECK_EQ(s.redo_group, 0);
    CHECK_EQ(s.nredo, 0);
    fcs_free(&s);
}

static void test_extras_store(void)
{
    Reg r = { 0 };
    CeStore st = make_store(&r);
    FcExtras x;
    fc_extras_load(&x, &st);
    CHECK_EQ(x.show_time_moves, 0); CHECK_EQ(x.standard_supermove, 0);
    CHECK_EQ(x.full_range, 0); CHECK_EQ(x.full_screen, 0);
    fc_extras_load(&x, NULL);
    CHECK_EQ(x.show_time_moves, 0);
    x.show_time_moves = 1; x.full_range = 1; x.full_screen = 1;
    fc_extras_save(&x, &st);
    CHECK_EQ(rv(&r, "ShowTimeMoves"), 1); CHECK_EQ(rv(&r, "StandardSupermove"), 0);
    CHECK_EQ(rv(&r, "FullRangeDeals"), 1); CHECK_EQ(rv(&r, "FullScreen"), 1);
    CHECK_EQ(r.nflush, 1);
    FcExtras y;
    fc_extras_load(&y, &st);
    CHECK_EQ(y.show_time_moves, 1); CHECK_EQ(y.standard_supermove, 0);
    CHECK_EQ(y.full_range, 1); CHECK_EQ(y.full_screen, 1);
    CHECK_EQ(rv(&r, "SingleClick"), 0); CHECK_EQ(rv(&r, "DragDrop"), 0);
    CHECK_EQ(rv(&r, "EnhancedAnimations"), 0);  /* 2d */
    x.single_click = 1;
    x.enhanced_anim = 1;
    fc_extras_save(&x, &st);
    CHECK_EQ(rv(&r, "SingleClick"), 1);
    CHECK_EQ(rv(&r, "EnhancedAnimations"), 1);
    fc_extras_load(&y, &st);
    CHECK_EQ(y.single_click, 1); CHECK_EQ(y.drag_drop, 0); CHECK_EQ(y.enhanced_anim, 1);
    CHECK_EQ(rv(&r, "LargePrint"), 0);          /* 2c, Large Print */
    CHECK_EQ(y.large_print, 0);
    x.large_print = 3;
    fc_extras_save(&x, &st);
    CHECK_EQ(rv(&r, "LargePrint"), 1);
    fc_extras_load(&y, &st);
    CHECK_EQ(y.large_print, 1);
    reg_set(&r, "StandardSupermove", 7);         /* any non-zero value is on */
    fc_extras_load(&y, &st);
    CHECK_EQ(y.standard_supermove, 1);
    /* the session starts with all extras off and never writes them to XP's store */
    FcSession s;
    Reg xp = { 0 };
    CeStore xs = make_store(&xp);
    fcs_init(&s, NULL, &xs);
    CHECK_EQ(s.extras.show_time_moves + s.extras.standard_supermove + s.extras.full_range + s.extras.full_screen, 0);
    s.extras.show_time_moves = 1;
    fcs_close(&s);
    CHECK_EQ(rv(&xp, "ShowTimeMoves"), -1);
    fcs_free(&s);
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
    test_peek_ends_on_move();
    test_key0_keeps_messages_option();
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
    test_move_counter();
    test_game_clock();
    test_standard_supermove();
    test_deal_range();
    test_won_deals();
    test_extras_store();
    test_single_click();
    test_drag_drop();
    test_undo_all();
    printf("test_session: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
