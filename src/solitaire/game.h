/*
 * Solitaire HD — XP Solitaire (Klondike, sol.exe) rules primitives, platform independent.
 *
 * Everything here follows docs/xp-reference/solitaire/rules.md (cited as §n): the card and pile model
 * (§0), the deal with msvcrt's rand (§1), the drop predicates (§2.1), the score tables and arithmetic
 * (§4), the Options / Back registry formats (§10) and the status-bar score text (§4.5). The controller
 * (session.h) builds the game flows on top of these.
 */
#ifndef SOL_GAME_H
#define SOL_GAME_H

#include <stddef.h>
#include <stdint.h>

/* ---- Cards (§0: the cards.dll ordinal rank*4 + suit, as the engine's card set) ------------------
 * rank 0 = A .. 12 = K; suit 0 = clubs, 1 = diamonds, 2 = hearts, 3 = spades. SOL_UP marks a face-up
 * card (XP keeps it in bit 15 of the card word). */
typedef uint8_t SolCard;
#define SOL_UP      0x80
#define SOL_KING    12

static inline int sol_card_id(SolCard c) { return c & 0x3F; }        /* 0..51, the engine's face index */
static inline int sol_rank(SolCard c)    { return (c & 0x3F) >> 2; }
static inline int sol_suit(SolCard c)    { return c & 3; }
static inline int sol_is_up(SolCard c)   { return (c & SOL_UP) != 0; }
/* XP's colour test (0x10042CE): opposite colours when (s1 ^ s2) is 1 or 2. */
static inline int sol_opposite(SolCard a, SolCard b) { int x = (a ^ b) & 3; return x == 1 || x == 2; }

/* ---- Piles (§0.1): XP's fixed order, also the order of the drop-target search, autoplay and the
 * keyboard cursor. Foundations have no fixed suits. Macros with the same values as layout.h's, so both
 * headers can be included together. */
#define SOL_NPILES 13
#define SOL_STOCK 0
#define SOL_WASTE 1
#define SOL_FOUND0 2                    /* foundations 2..5, left to right */
#define SOL_TAB0 6                      /* tableau columns 6..12, left to right */
/* XP's column classes (tcls): what the keyboard's Tab / Shift+arrows compare. */
enum { SOL_CLASS_STOCK = 1, SOL_CLASS_WASTE = 2, SOL_CLASS_FOUND = 3, SOL_CLASS_TAB = 4 };

static inline int sol_is_found(int p) { return p >= SOL_FOUND0 && p < SOL_FOUND0 + 4; }
static inline int sol_is_tab(int p)   { return p >= SOL_TAB0 && p < SOL_NPILES; }
static inline int sol_pile_class(int p)
{
    return p == SOL_STOCK ? SOL_CLASS_STOCK : p == SOL_WASTE ? SOL_CLASS_WASTE
         : sol_is_found(p) ? SOL_CLASS_FOUND : SOL_CLASS_TAB;
}

typedef struct SolPile {
    uint8_t n;                          /* number of cards; c[0] is the bottom, c[n-1] the top */
    SolCard c[52];
} SolPile;

typedef struct SolBoard {
    SolPile p[SOL_NPILES];
} SolBoard;

/* ---- msvcrt's rand (§1.1): x = x*214013 + 2531011, (x >> 16) & 0x7FFF; srand(seed) sets x = seed. -- */
static inline int sol_rand(uint32_t *state)
{
    *state = *state * 214013u + 2531011u;
    return (int)((*state >> 16) & 0x7FFF);
}

void sol_board_clear(SolBoard *b);
/* XP's deal (§1.2): srand(seed); the stock holds 0..51 face down and is shuffled in five naive passes
 * (j = rand() % 52, swap i and j); then row by row, column t of row r gets the stock's top card, face up
 * when t == r. The 24 cards left stay in the stock in shuffled order. *rng_after (may be NULL) receives
 * the rand state after the 260 calls: the win cascade continues from it (cascade.h). */
void sol_deal_board(SolBoard *b, unsigned seed, uint32_t *rng_after);

/* Drop predicates (§2.1): may the cards src[idx..top] go onto pile dst? Tableau: a king (or a run headed
 * by one) only onto an empty column, anything else onto a face-up card of the opposite colour one rank
 * higher. Foundation: single cards only, an ace onto any empty foundation, else the next rank of the
 * same suit. Stock and waste never accept. dst == src never accepts. */
int  sol_can_drop(const SolBoard *b, int dst, int src, int idx);
/* XP NumCards(fFaceUpRunOnly): the number of face-up cards on top of the pile. */
int  sol_up_run(const SolBoard *b, int pile);
int  sol_is_won(const SolBoard *b);                  /* all four foundations hold 13 cards (§8) */
int  sol_board_equal(const SolBoard *a, const SolBoard *b);   /* the piles (counts and cards) */
/* Structural invariants: 52 distinct cards; stock face down; waste face up; foundations ace-up runs of
 * one suit, face up; tableau = face-down cards, then a face-up run descending in alternating colours.
 * Returns 1 if valid; else 0 with a reason in why (may be NULL). */
int  sol_board_valid(const SolBoard *b, char *why, size_t n);

/* A board packed into 65 bytes (13 counts, then the 52 cards in pile order): the undo snapshots. */
#define SOL_PACKED_SIZE 65
void sol_board_pack(const SolBoard *b, uint8_t out[SOL_PACKED_SIZE]);
void sol_board_unpack(SolBoard *b, const uint8_t in[SOL_PACKED_SIZE]);

/* ---- Scoring (§4) --------------------------------------------------------------------------------- */
enum { SOL_SCORING_STANDARD = 0, SOL_SCORING_VEGAS = 1, SOL_SCORING_NONE = 2 };
/* Score events (§4.1): XP's indices into the score tables. */
enum {
    SOL_EV_CLOCK = 0,          /* every 10 s of play, and Undo: Standard -2 */
    SOL_EV_RECYCLE = 1,        /* waste turned back into the stock */
    SOL_EV_TO_FOUND = 2,       /* waste or tableau -> foundation */
    SOL_EV_WASTE_TO_TAB = 3,
    SOL_EV_TURN = 4,           /* a face-down tableau card turned over */
    SOL_EV_FOUND_TO_TAB = 5,
    SOL_EV_DEAL = 6,
    SOL_EV_WIN = 7
};
extern const int sol_std_table[8];      /* -2, -20, 10, 5, 5, -15, 0, 0 */
extern const int sol_vegas_table[8];    /*  0,   0,  5, 0, 0,  -5, -52, 0 */

/* ScoreMove (0x100505F): the event of a successful move from src to dst, or -1 for none. */
int  sol_move_event(int dst, int src);
/* The Standard time bonus (§4.3): 35 * (20000 / seconds), seconds = ticks >> 2, paid from 30 s on. */
int  sol_time_bonus(int ticks);
/* KlondChangeScore + DefChangeScore (§4.3): apply event ev to *score. draw = 1 or 3; ticks = the clock
 * (250 ms units); recycles = the recycle count, already incremented for SOL_EV_RECYCLE. Standard is
 * clamped at 0 after every change, Vegas may go negative, None never changes. Returns the Standard win
 * bonus for SOL_EV_WIN (0 in Vegas and None), else the change actually applied. */
int  sol_change_score(int *score, int ev, int scoring, int timed, int draw, int ticks, int recycles);

/* ---- Options and Back (§10): HKCU\Software\Microsoft\Solitaire, REG_DWORD values -------------------- */
typedef struct SolOptions {
    int status_bar;            /* bit 0 */
    int timed;                 /* bit 1: Timed game */
    int outline;               /* bit 2: Outline dragging */
    int draw;                  /* bit 3: 3 = Draw Three (set), 1 = Draw One */
    int scoring;               /* bits 4-5: SOL_SCORING_* (3 reads as Standard) */
    int cumulative;            /* bit 6: Cumulative score (Vegas) */
} SolOptions;
#define SOL_OPTIONS_DEFAULT 0x0Bu      /* Status bar, Timed, Draw Three, Standard: a missing value */
#define SOL_REG_OPTIONS  "Options"
#define SOL_REG_BACK     "Back"
#define SOL_REG_CURRENCY "iCurrency"   /* read, never written; sCurrency is ignored (always "$") */

void     sol_options_unpack(SolOptions *o, uint32_t v);    /* other bits are ignored */
uint32_t sol_options_pack(const SolOptions *o);
/* Card backs: index 0..11 = XP's back ids 54..65 = the engine's backs 0..11. The registry holds
 * id - 53; a stored value loads as clamp(value + 53, 54, 65). */
#define SOL_NBACKS 12
int      sol_back_from_reg(uint32_t v);
uint32_t sol_back_to_reg(int back);

/* The status-bar score value (§4.5) without its trailing space: "-" if negative, then (Vegas only) "$"
 * placed by iCurrency (0 "$1", 1 "1$", 2 "$ 1", 3 "1 $"; other values as 0), then |score|. */
void sol_format_score(int score, int vegas, int icurrency, char *buf, size_t n);

#endif
