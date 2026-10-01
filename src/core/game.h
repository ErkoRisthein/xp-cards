/*
 * FreeCell HD — game rules primitives (platform independent, no Windows headers).
 *
 * Faithful to Windows XP FreeCell; see docs/xp-reference/rules.md (section numbers quoted below).
 * The data model mirrors XP's own board array so the reverse-engineered pseudocode maps 1:1.
 */
#ifndef FC_GAME_H
#define FC_GAME_H

#include <stdint.h>

#define FC_EMPTY   (-1)
#define FC_NCOLS   8          /* tableau columns 1..8 */
#define FC_COLLEN  24         /* slots per column (XP used 21; 7 dealt + Q..A = 19 is the max) */
#define FC_MAX_STEPS 256      /* single-card steps in one action (user move + supermove + autoplay) */

/* Special game numbers */
#define FC_GAME_NONE      0
#define FC_GAME_MIN       (-2)
#define FC_GAME_MAX       1000000

typedef int Card;             /* rank*4 + suit; rank 0=A..12=K; suit 0=C 1=D 2=H 3=S; FC_EMPTY */

static inline int fc_rank(Card c)   { return c >> 2; }
static inline int fc_suit(Card c)   { return c & 3; }
static inline int fc_is_red(Card c) { return (c & 3) == 1 || (c & 3) == 2; }

/*
 * board[0][0..3] = free cells, board[0][4..7] = home cells (top card of the pile only),
 * board[1..8][i] = tableau column, i = 0 is the card furthest from the player; the exposed
 * card is at the last non-empty index. Unused slots are FC_EMPTY.
 */
typedef struct FcBoard {
    Card    board[9][FC_COLLEN];
    int8_t  home_rank[4];       /* by suit: rank of top home card, -1 if none   (XP 0x1008360) */
    int8_t  suit_home_slot[4];  /* by suit: top-row slot 4..7 owning it, or -1 (XP 0x1008330) */
    int     cards_left;         /* cards not on home cells (52 after deal)      (XP 0x1007800) */
} FcBoard;

/* One single-card move, as logged by XP's QueueMove (rules.md §4.2). */
typedef struct FcStep {
    int8_t src_col, src_pos, dst_col, dst_pos;
    Card   card;
} FcStep;

/* A committed user action: the board before it plus every single-card step (user move, supermove
 * parking/unparking, autoplay). Used for animation (replay forward) and undo (replay backward). */
typedef struct FcAction {
    FcBoard before;
    int     nsteps;
    FcStep  steps[FC_MAX_STEPS];
} FcAction;

/* ---- Deal (rules.md §1) ---------------------------------------------------------------------- */
void fc_board_clear(FcBoard *b);                 /* empty board, homes -1, cards_left 0 */
void fc_deal(FcBoard *b, int game_number);       /* MS LCG deal; also the fixed -1 / -2 layouts */
int  fc_random_game_number(uint32_t time_seed);  /* XP RandomGameNumber: srand(t); rand(); rand(); ... */

/* ---- Predicates (rules.md §2) ----------------------------------------------------------------- */
int  fc_can_stack(Card src, Card dst);           /* dst rank = src rank + 1, opposite colours */
int  fc_last_index(const FcBoard *b, int col);   /* -1 if empty; col 1..8 */
int  fc_free_cells_empty(const FcBoard *b);      /* number of empty free cells */
int  fc_empty_columns(const FcBoard *b);         /* number of empty tableau columns */
int  fc_capacity(int free_cells, int empty_cols);/* (f+1)*(e+1) */
int  fc_max_movable(const FcBoard *b);
int  fc_cards_to_move(const FcBoard *b, int src_col, int dst_col); /* XP CardsToMove, §2.3 */
int  fc_safe_to_autoplay(const FcBoard *b, Card c, int cheat_win); /* §3 */

/* ---- Building actions (rules.md §2.6, §3, §4.2) -------------------------------------------- */
void fc_action_begin(FcAction *a, const FcBoard *b);
/* Apply one single-card move to b and append it to a (XP QueueMove). */
void fc_queue(FcBoard *b, FcAction *a, int src_col, int src_pos, int dst_col, int dst_pos);
void fc_move_run_via_free_cells(FcBoard *b, FcAction *a, int src_col, int dst_col);
void fc_supermove(FcBoard *b, FcAction *a, int src_col, int dst_col);
void fc_autoplay(FcBoard *b, FcAction *a, int cheat_win);
int  fc_home_slot_for(FcBoard *b, int suit);     /* assigns leftmost empty home slot on first use */

/* Undo one action: restores a->before (b must be the board right after the action). */
void fc_undo_action(FcBoard *b, const FcAction *a);

/* ---- Game over (rules.md §6) -------------------------------------------------------------------- */
/* Returns the number of single-card legal moves counted the XP way when all free cells are full and
 * no column is empty (0, 1 or "2" meaning >= 2); returns 2 if any free cell or column is empty. */
int  fc_count_moves_xp(const FcBoard *b);
int  fc_is_won(const FcBoard *b);                /* cards_left == 0 */

#endif
