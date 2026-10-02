/*
 * FreeCell HD — solver (platform independent, no Windows headers). For the v1.2 extras (ROADMAP §1b):
 * Hint, "no longer winnable" warning and auto-finish.
 *
 * MOVES ARE USER ACTIONS. One solver move is one click pair the session (session.c) accepts, played
 * exactly as fcs_click plays it: the user's part through the game.c primitives the session uses
 * (fc_queue, fc_supermove / fc_move_cards_std, fc_move_run_via_free_cells), then XP's safe autoplay
 * (fc_autoplay) to its fixpoint. The supermove rule is a parameter: XP (f+1)(e+1) or the 'standard'
 * option (f+1)*2^e (FcExtras.standard_supermove). A solution therefore maps 1:1 onto clicks:
 *
 *   for each FcSolveMove m, on the board as the previous moves (and their autoplay) left it:
 *     fcs_click(s, m.src_col, m.src_pos);    selects the card (src_pos is the exposed card of a column;
 *                                            any card of that column would select it too)
 *     fcs_click(s, m.dst_col, m.dst_pos);    moves it; if m.movecol != FC_SM_NODIALOG the session asks
 *                                            "Move to Empty Column..." and the answer must be m.movecol
 *                                            (FCS_MOVECOL_COLUMN = 1 "Move column", FCS_MOVECOL_SINGLE = 0
 *                                            "Move single card"); with FC_SM_NODIALOG no dialog appears
 *
 *   kind               first click            second click                   what moves
 *   FC_SM_HOME         free cell (0,i) or     (0, 4..7): the suit's home      the exposed card
 *                      column (c, last)       slot, else the leftmost empty
 *   FC_SM_FREECELL     column (c, last)       (0, leftmost empty free cell)   the exposed card
 *   FC_SM_COLUMN       free cell or column    (d, last of d): a non-empty     1 card from a free cell;
 *                                             column                          fc_cards_to_move cards
 *                                                                             (supermove) from a column
 *   FC_SM_EMPTY        free cell or column    (d, -1): leftmost empty column  1 card, or the run the
 *                                                                             "Move column" answer takes
 *   FC_SM_AUTOPLAY     (c, last), or (0,i)    the same position again         nothing; only autoplay runs
 *                      if no column has cards                                 (a fresh deal, which the
 *                                                                             session never autoplays)
 *
 * Positions follow fcs_click: top row col 0 (pos 0..3 free cells, 4..7 home cells), tableau col 1..8.
 * fc_solve_play applies one move to a board exactly as the session would (tests replay every solution
 * through fcs_click with a scripted MoveCol answer and compare the boards after each move).
 *
 * SEARCH. Weighted best-first (priority 6*g + h, g = user actions, h a weighted disorder estimate) over
 * canonical states (free cells as a multiset, columns as a multiset, home piles by rank), integer only. Fixed memory: a node pool of
 * max_nodes 80-byte states and an open-addressing hash table of uint32 indexes (load <= 1/2), allocated
 * once by fc_solver_new. Deterministic: same board, rules and limits -> same result and moves.
 * Every state the session can reach is generated (moves that cannot change the canonical state, such as
 * free cell -> free cell, are left out), so an exhausted search proves that no sequence of session
 * actions wins.
 */
#ifndef FC_SOLVER_H
#define FC_SOLVER_H

#include "game.h"

#include <stddef.h>
#include <stdint.h>

enum {
    FC_SOLVE_SOLVED = 0,          /* moves lead to a win */
    FC_SOLVE_UNSOLVABLE = 1,      /* proven: every reachable state explored, none wins */
    FC_SOLVE_GAVE_UP = 2,         /* the node budget (max_nodes) ran out */
    FC_SOLVE_CANCELLED = 3        /* *cancel became non-zero */
};

enum { FC_SM_AUTOPLAY = 0, FC_SM_HOME = 1, FC_SM_FREECELL = 2, FC_SM_COLUMN = 3, FC_SM_EMPTY = 4 };
#define FC_SM_NODIALOG (-2)       /* FcSolveMove.movecol: the MoveCol dialog does not appear */

typedef struct FcSolveMove {
    int8_t src_col, src_pos;      /* first click (selects) */
    int8_t dst_col, dst_pos;      /* second click (moves); dst_pos = -1 for an empty column */
    int8_t movecol;               /* FC_SM_NODIALOG, or the dialog answer FCS_MOVECOL_SINGLE / _COLUMN */
    int8_t kind;                  /* FC_SM_* */
    int8_t ncards;                /* cards the user's part moves (autoplay not counted) */
    int8_t reserved;
    Card   card;                  /* the card that lands on the destination (base of a moved run) */
} FcSolveMove;

typedef struct FcSolveResult {
    int      status;              /* FC_SOLVE_* */
    int      nmoves;              /* SOLVED: number of moves (0 if the board is already won) */
    const FcSolveMove *moves;     /* owned by the solver; valid until the next fc_solve / fc_solver_free */
    uint32_t nodes;               /* distinct states stored */
    uint32_t expanded;            /* states expanded */
} FcSolveResult;

/* Node budget: the default suits Hint on the XP target (solves all but a handful of deals 1..32000; see
 * docs/DESIGN.md "Solver"); the maximum keeps the memory under 32 MiB. */
#define FC_SOLVER_DEFAULT_NODES 100000u   /* about 9 MiB */
#define FC_SOLVER_MAX_NODES     360000u   /* about 31.5 MiB */
#define FC_SOLVER_NODE_BYTES    80u

typedef struct FcSolver FcSolver;

/* max_nodes = node budget, the most distinct positions one search stores (0 = FC_SOLVER_DEFAULT_NODES,
 * larger values are clamped to FC_SOLVER_MAX_NODES). Memory, all allocated here: FC_SOLVER_NODE_BYTES
 * per node plus a hash table of 4-byte slots (the power of two >= 2 * max_nodes). Returns NULL when
 * out of memory. Reusable for any number of fc_solve calls (one thread at a time per solver; separate
 * solvers are independent; nothing global). */
FcSolver *fc_solver_new(uint32_t max_nodes);
void      fc_solver_free(FcSolver *sv);
size_t    fc_solver_memory(const FcSolver *sv);   /* bytes allocated */

/* Solve from board b (any position the session can show: a fresh deal or mid-game, not won by the
 * cheat) under the given supermove rule (0 = XP, 1 = standard). cancel may be NULL; it is read before
 * every expansion and when non-zero the search stops with FC_SOLVE_CANCELLED. Returns res->status. */
int fc_solve(FcSolver *sv, const FcBoard *b, int standard_supermove, const volatile int *cancel,
             FcSolveResult *res);

/* Every move fc_solve considers from b: all actions the session accepts except those that cannot
 * change the position up to free-cell and column order (free cell -> free cell, a lone card or a whole
 * column into an empty column, a deselect unless b is a fresh deal that autoplay has not touched).
 * out must hold FC_SOLVE_MAX_MOVES. Returns the count. */
#define FC_SOLVE_MAX_MOVES 160
int fc_solve_moves(const FcBoard *b, int standard_supermove, FcSolveMove *out);

/* The search's estimate of the work left on b (its heuristic with the default weights: cards not home,
 * cards above a lower card of their column, the cards covering the next card each home pile needs,
 * occupied free cells, less for empty columns; lower is better, 0 = won). Hint cycling (2c) ranks the
 * alternatives to the solver's move by it. */
int fc_solve_estimate(const FcBoard *b);

/* Play move m on b as the session does (user part + autoplay). Returns 1, or 0 (b unchanged) if m
 * does not fit b (no card at the source, or an illegal destination). */
int fc_solve_play(FcBoard *b, const FcSolveMove *m, int standard_supermove);

/* Auto-finish test: 1 iff repeatedly moving any card that can go to its home pile (ignoring the
 * autoplay safety rule) empties the board. When moves is not NULL it receives the actions that do it
 * (FC_SM_HOME, each followed by autoplay like any session move; at most 52; lowest rank first) and
 * *nmoves their number (also when the result is 0: then the moves only go part of the way). */
int fc_sure_win(const FcBoard *b, FcSolveMove *moves, int *nmoves);

#endif
