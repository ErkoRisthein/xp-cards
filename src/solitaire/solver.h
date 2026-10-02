/*
 * Solitaire HD — full-information Klondike solver (platform independent, no Windows headers).
 *
 * FULL INFORMATION. The solver sees every card: the face-down tableau cards and the order of the stock.
 * It answers "can this position still be won with perfect play?", so it is used ONLY to pick winnable
 * deals (the precomputed table in winnable_seeds.h) and for an optional "no more winning moves"
 * check. It must never drive Hint, which may only use what the player can see (docs/DESIGN.md).
 *
 * RULES are the session's (game.h, session.c; rules.md §2-3): XP's drop rules (a king or a run headed by
 * one only onto an empty column; foundations take aces then the next card of the suit, single cards),
 * any face-up tableau card with the run on it can be dragged, only the waste's top card, foundation
 * tops back to the tableau. A press on the stock draws `draw` cards (1 or 3; fewer when fewer are left),
 * the packet turned over; a press on the empty stock turns the waste back (recycle) when the pass limit
 * allows it: unlimited with Standard and None scoring, Vegas draw - 1 recycles (draw one: 1 pass, draw
 * three: 3 passes). The Ctrl+Alt+Shift draw-one cheat is never used.
 *
 * MOVES ARE SESSION ACTIONS. A solution is a list of SolSolveMove, each one input the session accepts,
 * played on the board left by the previous ones:
 *
 *   kind             session call                                     what happens
 *   SOL_SM_DRAW      sol_press(s, SOL_STOCK, 0, 0)                     n = min(draw, stock) cards drawn
 *   SOL_SM_RECYCLE   sol_press(s, SOL_STOCK, 0, 0)  (stock empty)      the waste turned back (n cards)
 *   SOL_SM_TURN      sol_press(s, src, index, 0)                       the face-down top card turned up
 *   SOL_SM_MOVE      sol_begin_drag(s, src, index); sol_drop(s, dst)   n cards src[index..] -> dst
 *
 * Every face-down card is turned (SOL_SM_TURN) as soon as a move exposes it, and the solver plays the
 * provably safe foundation moves by itself after every move (SOL_SM_MOVE with forced = 1; see "Search"):
 * both are ordinary actions in the list. A foundation is named by its pile: the one holding the
 * suit, or the leftmost empty one for an ace. sol_solve_apply plays one action on a board exactly as
 * the session does (the tests replay every solution through the session API and compare the boards).
 * With the opt-in "turn face-down cards automatically" extra the session may already have turned the
 * card: a SOL_SM_TURN whose card is face up is then skipped.
 *
 * SEARCH. Weighted best-first (priority W_G * g + h, g = solver moves, h a weighted count of face-down,
 * not-home and talon cards) over canonical states, integer only, deterministic (same board, draw and
 * pass limit -> same result and moves):
 *   - Macro moves for the stock: the stock and the waste are one sequence (waste bottom .. top, then
 *     the stock in draw order) and a position in it; a talon move plays a card that some number of draws
 *     (and at most one recycle, which a limited pass budget counts) brings to the waste top. Draw one
 *     reaches every card from the waste top on (after a recycle: all); draw three only the cards the
 *     3-card grid from the current position (after a recycle: from the start) lands on.
 *   - Safe moves to the foundations are played at once: aces and twos always; a card of rank r >= 3
 *     when both opposite-colour suits have rank r - 1 home and the other suit of its colour rank r - 2
 *     (then every card that could ever be placed on it, recursively, is home: any winning line from the
 *     position before the move maps onto one from the position after). From the tableau always; from
 *     the talon only with draw one (taking a card out of the sequence then never makes another card
 *     harder to reach; with draw three it shifts the grid): the waste top, and with unlimited passes
 *     any talon card.
 *   - Face-down cards are turned as soon as they are exposed; a position whose cards are all face up
 *     with an empty talon (or draw one with unlimited passes) is won by playing the lowest card home
 *     repeatedly, without a search.
 *   - Canonical state (32 bytes): the 7 columns as sorted 32-bit words (face-down count and origin,
 *     run length, base card, one suit bit per further run card), the cards home per suit (redundant,
 *     in spare bits) and the talon (cards left as a bit set over the starting sequence, the waste size,
 *     the recycles used). The waste size is left out with draw one and unlimited passes (every card is
 *     reachable), the recycle count with unlimited passes.
 *   - Move filters: a move that only prepares something (a partial run to the other card that takes it,
 *     a run that empties a column, a foundation move that is not safe, a foundation card back down, and
 *     with draw one and unlimited passes a talon card to the tableau) is generated only when what it
 *     prepares can follow at once (solver.c generate). Runs that uncover a face-down card, and with
 *     draw three or a pass limit every talon move, are always generated. This finds wins fast but is
 *     NOT complete: a follow-up that needs two preparing moves (4S home once 3S has gone home from the
 *     talon and 3D has moved off 4S) has neither generated, since neither lets it follow at once.
 *   - Dead ends: a card stuck in a column, or with no recycle left in the waste, above the next lower
 *     card of its suit and both cards it could go on can never leave; the position is lost.
 *   So a search the filters exhaust proves nothing by itself: it is followed by a second search over
 *   every legal move (the safe moves and the dead-end tests kept, both sound), and only when that one
 *   is exhausted too is the position UNSOLVABLE: no sequence of session actions wins. (Checked against
 *   the plain search, sol_solver_filters off, on 600 positions of random play: the 545 that both
 *   decided all agree; the filters alone had called a winnable one lost, test_sol_solver.c.)
 * Fixed memory: a pool of max_nodes 52-byte states and an open-addressing hash table of uint32 indexes
 * (load <= 1/2), allocated once by sol_solver_new; nothing but the solution buffer is allocated later.
 */
#ifndef SOL_SOLVER_H
#define SOL_SOLVER_H

#include "game.h"

#include <stddef.h>
#include <stdint.h>

enum {
    SOL_SOLVE_SOLVED = 0,         /* moves lead to a win (checked by replaying them on the board) */
    SOL_SOLVE_UNSOLVABLE = 1,     /* proven: every reachable position explored, none wins */
    SOL_SOLVE_GAVE_UP = 2,        /* the node budget (max_nodes) ran out, or out of memory */
    SOL_SOLVE_CANCELLED = 3,      /* *cancel became non-zero */
    SOL_SOLVE_INVALID = 4         /* the board is not a Klondike position (sol_board_valid, a talon of
                                     more than 24 cards, more than 7 face-down cards in a column) */
};

enum { SOL_SM_DRAW = 1, SOL_SM_RECYCLE = 2, SOL_SM_TURN = 3, SOL_SM_MOVE = 4 };

typedef struct SolSolveMove {
    uint8_t kind;                 /* SOL_SM_* */
    uint8_t src;                  /* MOVE: source pile; TURN: tableau pile; DRAW, RECYCLE: SOL_STOCK */
    uint8_t index;                /* MOVE: index of the first (bottom) card moved in src; TURN: the top
                                     card's index; DRAW, RECYCLE: 0 */
    uint8_t dst;                  /* MOVE: destination pile; DRAW: SOL_WASTE; RECYCLE: SOL_STOCK */
    uint8_t n;                    /* cards moved / drawn / turned back; TURN: 1 */
    uint8_t card;                 /* MOVE: the bottom card moved; TURN: the card turned up; DRAW: the new
                                     waste top; RECYCLE: the new stock top (sol_card_id, 0..51) */
    uint8_t forced;               /* MOVE: 1 = a safe foundation move the solver played by itself */
    uint8_t reserved;
} SolSolveMove;

typedef struct SolSolveResult {
    int      status;              /* SOL_SOLVE_* */
    int      nmoves;              /* SOLVED: number of actions (0 if the board is already won) */
    const SolSolveMove *moves;    /* owned by the solver; valid until the next sol_solve / sol_solver_free */
    uint32_t nodes;               /* distinct positions stored */
    uint32_t expanded;            /* positions expanded */
} SolSolveResult;

/* Node budget. The default suits an in-game check on the XP target; the offline table generator
 * (tests/sol_seed_tables.c) uses larger budgets. Memory: SOL_SOLVER_NODE_BYTES per node plus a hash
 * table of 4-byte slots (the power of two >= 2 * max_nodes), i.e. 60 to 68 bytes per node. */
#define SOL_SOLVER_DEFAULT_NODES 150000u     /* about 10 MiB */
#define SOL_SOLVER_MAX_NODES     16000000u   /* about 1 GiB: offline use only */
#define SOL_SOLVER_NODE_BYTES    52u

typedef struct SolSolver SolSolver;

/* max_nodes = node budget, the most distinct positions one search stores (0 = SOL_SOLVER_DEFAULT_NODES,
 * larger values are clamped to SOL_SOLVER_MAX_NODES). Returns NULL when out of memory. Reusable for any
 * number of sol_solve calls (one thread at a time per solver; separate solvers are independent; nothing
 * global). */
SolSolver *sol_solver_new(uint32_t max_nodes);
void       sol_solver_free(SolSolver *sv);
size_t     sol_solver_memory(const SolSolver *sv);   /* bytes allocated */

/* Solve from board b (any position the session can show) with `draw` cards per stock press (1 or 3)
 * and recycles_left more recycles allowed (< 0 = unlimited; see sol_solve_recycles_left). cancel may be
 * NULL; it is read before every expansion and when non-zero the search stops with SOL_SOLVE_CANCELLED.
 * Returns res->status. */
int sol_solve(SolSolver *sv, const SolBoard *b, int draw, int recycles_left, const volatile int *cancel,
              SolSolveResult *res);

/* XP's pass limit as a recycle budget: -1 (unlimited) with Standard and None scoring; with Vegas
 * draw - 1 - recycles (draw one: 1 pass, draw three: 3 passes), never below 0. */
int sol_solve_recycles_left(int scoring, int draw, int recycles);

/* Play one action on b as the session does (draw: the game's draw count, 1 or 3). Returns 1, or 0 (b
 * unchanged) if it does not fit b: a draw from an empty stock, a recycle with stock cards left or an
 * empty waste, a turn of a card that is not a face-down top (a face-up one: 1, nothing changes), a move
 * the session refuses (sol_begin_drag / sol_drop: face-down, waste or foundation cards other than the
 * top, an illegal destination) or whose n / card do not match. The pass limit is the caller's. */
int sol_solve_apply(SolBoard *b, const SolSolveMove *m, int draw);

#ifdef SOL_SOLVER_TUNING
/* For the offline tool and the tests. Heuristic weights (FD, FD_DEPTH, HOME, TALON, G); w = NULL: the
 * defaults. Filters off (on = 0): every legal move and no dead-end test, the plain search the move
 * filters are checked against (much slower). */
void sol_solver_tune(SolSolver *sv, const int *w, int n);
void sol_solver_filters(SolSolver *sv, int on);
#endif

#endif
