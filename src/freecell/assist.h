/*
 * FreeCell HD — the solver extras' state machines (assist.c): Hint, the unwinnable warning and the
 * solution cache. Internal to src/freecell: these are the hooks the controller (session.c) calls; the
 * public API (fcs_hint_enabled, fcs_solve_done, ...) is in session.h.
 */
#ifndef FC_ASSIST_H
#define FC_ASSIST_H

#include "session.h"

/* Why the position is being looked at (FcAssist.req_cause). */
enum { FCS_AS_DEAL = 1, FCS_AS_MOVE = 2, FCS_AS_UNDO = 3, FCS_AS_HINT = 4 };

void fcs_assist_free(FcSession *s);
/* The position changed while the game goes on: a deal, a committed move or Redo (before = the board
 * before it), an Undo. Ends a hint; with the warning on, checks the new position (from the cache, by
 * derivation, or with a background search) and warns when a move made it unwinnable. */
void fcs_assist_changed(FcSession *s, int cause, const FcBoard *before);
/* Input arrived (a click, a digit key, a command): a hint being shown or worked out ends. */
void fcs_assist_input(FcSession *s);
/* The game ended (win, loss, exit): any hint and search stop. */
void fcs_assist_stop(FcSession *s);
void fcs_assist_hint(FcSession *s);              /* Game > Hint, key H */
void fcs_assist_timer(FcSession *s, int id);     /* FCS_TIMER_HINT, FCS_TIMER_HINT_WAIT */
void fcs_assist_view(const FcSession *s, int *col, int *pos);   /* FcsViewState hint_col / hint_pos */

#endif
