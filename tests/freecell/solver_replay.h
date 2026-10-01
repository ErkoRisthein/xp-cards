/*
 * FreeCell HD — replays a solver solution through the session (fcs_click) with a scripted UI that
 * answers the "Move to Empty Column..." dialog as the solution says. Shared by tests/test_solver.c and
 * tests/solver_bench.c.
 */
#ifndef FC_SOLVER_REPLAY_H
#define FC_SOLVER_REPLAY_H

#include "freecell/session.h"
#include "freecell/solver.h"

#include <string.h>

typedef struct ReplayUI {
    int deal;                 /* ask_game_number answer */
    int answer;               /* ask_move_column answer for the current move */
    int ndialog, nmsg, nwin, nlose;
} ReplayUI;

static int rp_movecol(void *c) { ReplayUI *r = c; r->ndialog++; return r->answer; }
static void rp_message(void *c, int id, const char *t) { (void)id; (void)t; ((ReplayUI *)c)->nmsg++; }
static int rp_gamenum(void *c, int init, int *v) { (void)init; *v = ((ReplayUI *)c)->deal; return 1; }
static int rp_win(void *c, int *sel) { (void)sel; ((ReplayUI *)c)->nwin++; return 0; }
static int rp_lose(void *c, int *same) { (void)same; ((ReplayUI *)c)->nlose++; return 0; }

static void replay_init(FcSession *s, ReplayUI *r)
{
    FcSessionUI ui;
    memset(&ui, 0, sizeof ui);
    memset(r, 0, sizeof *r);
    ui.ctx = r;
    ui.message = rp_message;
    ui.ask_move_column = rp_movecol;
    ui.ask_game_number = rp_gamenum;
    ui.you_win = rp_win;
    ui.you_lose = rp_lose;
    fcs_init(s, &ui, NULL);
}

/* Deal game n in the session (Select Game). */
static void replay_deal(FcSession *s, ReplayUI *r, int n)
{
    r->deal = n;
    fcs_command(s, FCS_CMD_SELECT);
}

/* Play the moves through fcs_click from the session's current board. After every move the session's
 * board must equal fc_solve_play's, the MoveCol dialog must have appeared exactly when expected, no
 * illegal-move message, and the move committed as one undoable action (counted unless FC_SM_AUTOPLAY)
 * that does not end the game in a loss.
 * expect_win: the last move must win. Returns NULL if all is well, else what went wrong (and *at = the
 * move index). */
static const char *replay_moves(FcSession *s, ReplayUI *r, const FcSolveMove *mv, int n, int std, int expect_win, int *at)
{
    s->extras.standard_supermove = std;
    FcBoard want = s->board;
    int wins = r->nwin;
    for (int i = 0; i < n; i++) {
        const FcSolveMove *m = &mv[i];
        *at = i;
        if (!fc_solve_play(&want, m, std)) return "fc_solve_play refused the move";
        fcs_click(s, m->src_col, m->src_pos);
        if (!s->sel || s->sel_col != m->src_col || s->sel_pos != m->src_pos) return "first click did not select the source";
        r->answer = m->movecol == FC_SM_NODIALOG ? FCS_MOVECOL_CANCEL : m->movecol;
        int nd = r->ndialog, nm = r->nmsg, nh = s->nhist, moves = s->moves, lost = r->nlose;
        fcs_click(s, m->dst_col, m->dst_pos);
        if (r->ndialog - nd != (m->movecol != FC_SM_NODIALOG)) return "MoveCol dialog expectation";
        if (r->nmsg != nm) return "illegal-move message";
        if (s->sel) return "still selected after the move";
        if (memcmp(&s->board, &want, sizeof want) != 0) return "board differs from fc_solve_play";
        if (r->nlose != lost) return "game lost (no more legal moves)";
        if (s->board.cards_left > 0) {
            if (s->nhist != nh + 1) return "not one committed action";
            if (s->moves != moves + (m->kind != FC_SM_AUTOPLAY)) return "move counter";
        }
    }
    *at = n;
    if (expect_win && (r->nwin != wins + 1 || s->board.cards_left != 0)) return "not won";
    return NULL;
}

#endif
