/*
 * Solitaire HD — the move advice behind the opt-in extras (platform independent, no Windows headers):
 * the fair Hint, the destination of a single click (click-to-move), and Finish. Pure functions of the
 * board; the session (session.c) runs the state machines around them.
 *
 * FAIR. Nothing here reads a face-down card or the order of the stock: only what the player sees (the
 * face-up tableau cards, the number of face-down cards under them, the foundations, the waste's top
 * card, whether the stock is empty). The full-information solver (solver.h) is never used for advice.
 * tests/solitaire/test_sol_extras.c checks that every answer stays the same when the hidden cards are
 * shuffled.
 *
 * HINT RANKING (sol_hint_find): every legal move is classified, the best class wins; ties go to the
 * column with more face-down cards, then to the first in XP's pile order (source, then destination).
 *    1. turn over a face-down top card (left over when "turn cards over automatically" is off)
 *    2. an ace or a two to a foundation
 *    3. a safe card to a foundation: both foundations of the other colour already hold its rank - 1
 *       (no card it could hold is still in play)
 *    4. a tableau top card to a foundation that uncovers a face-down card
 *    5. a run (or a king's run to an empty column) that uncovers a face-down card; more face-down
 *       cards under it first
 *    6. the waste's card to a foundation
 *    7. a run moved off a card that can then go to a foundation (the only use of a "sideways" move),
 *       never onto a card that could go to a foundation itself (no progress; it would move back)
 *    8. the waste's card to the tableau (a king to an empty column)
 *    9. a run that empties a column while a king is waiting for it (on the waste, or heading a run on
 *       face-down cards)
 *   10. any other tableau card to a foundation
 *   11. draw from the stock
 *   12. turn the waste back into the stock (the pass limit allowing)
 * Never suggested: a card from a foundation back down, a sideways run move that frees nothing, a king's
 * run that already heads its column moved to another empty column, emptying a column no visible king
 * can use. When nothing is left: none (the session says "No hint is available.").
 *
 * CLICK-TO-MOVE (sol_click_dest), the destination of a click on a movable card (the cards from it to
 * the top of its pile):
 *    1. a single card (the pile's top card, or the waste's) goes to the leftmost foundation that takes it
 *       (as XP's double-click);
 *    2. otherwise the leftmost tableau column that takes the cards and whose top card could not go to a
 *       foundation itself (a card bound for home is not buried);
 *    3. otherwise the leftmost tableau column that takes them.
 * A king (or a king's run) goes to the leftmost empty column, unless it already heads its column (that
 * would gain nothing). Nothing else is ever chosen: a click on a card with no legal destination, on a
 * foundation's card (drag it) or on a face-down card does not move it (a face-down top card is turned
 * over, as in XP). The move is an ordinary drop (scored and undone as one), so with "turn cards over
 * automatically" the card it uncovers is turned over too.
 *
 * AUTO-HOME (sol_auto_home_step, the "Move cards home automatically" extra): XP FreeCell's autoplay rule
 * on Klondike's foundations. A face-up card on top of the waste or of a tableau column goes home when a
 * foundation takes it and it is safe there: an ace or a two always; any other card when both
 * foundations of the other colour already hold its rank - 1 (then no card it could hold is still in
 * play). One card per call: the waste's first, then the columns left to right, each to the leftmost
 * foundation that takes it; the session calls it again until it finds none.
 *
 * FINISH (sol_finish_ready / sol_finish_step): with the stock and the waste empty and every tableau card
 * face up, the game is a sure win: the lowest card left is always on top of its column (each column is
 * a descending run), so playing the lowest card home, again and again, wins.
 */
#ifndef SOL_ASSIST_H
#define SOL_ASSIST_H

#include "game.h"

enum { SOL_HINT_NONE = 0, SOL_HINT_TURN = 1, SOL_HINT_DRAW = 2, SOL_HINT_RECYCLE = 3, SOL_HINT_MOVE = 4 };

/* The hint's classes (1 = best; see the ranking above). */
enum { SOL_HC_TURN = 1, SOL_HC_LOW_HOME = 2, SOL_HC_SAFE_HOME = 3, SOL_HC_HOME_REVEAL = 4, SOL_HC_REVEAL = 5,
       SOL_HC_WASTE_HOME = 6, SOL_HC_FREE_HOME = 7, SOL_HC_WASTE_TAB = 8, SOL_HC_EMPTY_COL = 9,
       SOL_HC_HOME = 10, SOL_HC_DRAW = 11, SOL_HC_RECYCLE = 12 };

typedef struct SolHintMove {
    int kind;           /* SOL_HINT_* */
    int src, index;     /* MOVE: the cards src[index..top]; TURN: the face-down top card; DRAW / RECYCLE:
                           SOL_STOCK and its top card (-1 when empty) */
    int dst;            /* MOVE: the destination pile; DRAW / RECYCLE: SOL_WASTE; TURN: src */
    int cls;            /* SOL_HC_* */
} SolHintMove;

/* The best move on b by the ranking above. recycle_ok: the waste may be turned back now (the pass
 * limit; session.c recycle_allowed). Returns 1, or 0 with kind SOL_HINT_NONE. */
int sol_hint_find(const SolBoard *b, int recycle_ok, SolHintMove *out);

/* Where a click on card index of pile sends the cards index..top (see above); -1: nowhere. */
int sol_click_dest(const SolBoard *b, int pile, int index);

/* The next card that is safe to go home (see AUTO-HOME above). Returns 0 if none. */
int sol_auto_home_step(const SolBoard *b, int *src, int *dst);

/* The stock and the waste are empty, every tableau card is face up, and cards are left to play. */
int sol_finish_ready(const SolBoard *b);
/* The next card home: the lowest-ranked tableau top card that a foundation takes (leftmost column among
 * equals), to the leftmost foundation that takes it. Returns 0 if none. */
int sol_finish_step(const SolBoard *b, int *src, int *dst);

#endif
