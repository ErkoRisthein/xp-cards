/*
 * FreeCell HD — Help menu (resources.md §2): Contents / Search open XP's freecell.chm through
 * HtmlHelpW (engine/win32/help.h: loaded at run time from hhctrl.ocx, as XP resolves it, by ordinal);
 * How to Use Help opens NTHelp.chm. When HtmlHelp or the help file is missing, a built-in "How to
 * Play" text is shown instead. About uses ShellAboutW like XP.
 */
#include "app.h"

static const WCHAR how_to_play[] =
    L"The goal is to move all 52 cards to the four home cells (top right), building each suit "
    L"up from Ace to King.\n\n"
    L"Columns are built down in alternating colors: a red card goes on a black card one rank "
    L"higher, and a black card on a red one. Any card can go into an empty column.\n\n"
    L"The four free cells (top left) each hold one card for a while.\n\n"
    L"To move a card, click it to select it (it is shown highlighted), then click where it should "
    L"go. Only the bottom card of a column or a card in a free cell can be moved. Click it again to "
    L"cancel. With the Double-click option on, double-clicking a card moves it to a free cell.\n\n"
    L"An ordered sequence moves as a whole when there is room: up to (empty free cells + 1) x "
    L"(empty columns + 1) cards, or (empty free cells + 1) x 2^(empty columns) with the option "
    L"\"Standard multi-card moves\". When moving to an empty column you can choose to move the "
    L"column or a single card.\n\n"
    L"Cards no longer needed on the table go home automatically. Hold the right mouse button on a "
    L"covered card to see it.\n\n"
    L"Hint (H) flashes a card to move, then where it goes; H again shows the next-best move. When the "
    L"rest is a sure win, Finish (F6) or the Finish button on the table sends every card home. Options "
    L"can warn when the game can no longer be won, and finish automatically. With \"Single click moves "
    L"a card\" a click sends a card to its best place; with \"Drag and drop cards\" cards can be dragged "
    L"where they should go; \"Enhanced animations\" adds a few quick effects. Game > Undo All goes back "
    L"to the start of the game.\n\n"
    L"Keyboard: 1-8 select or move to a column, 0 a free cell, 9 home. H Hint, F6 Finish, F2 New "
    L"Game, F3 Select Game, F10 or Ctrl+Z Undo (hold Ctrl+Z to undo more), Ctrl+Y Redo, F11 or "
    L"Alt+Enter Full Screen (Esc leaves it).";

static void builtin_help(App *a)
{
    modal_begin(a);
    MessageBoxW(a->hwnd, how_to_play, L"How to Play FreeCell", MB_OK | MB_ICONINFORMATION);
    modal_end(a);
}

void help_contents(App *a)
{
    if (!a->in_modal && !ce_help_open_chm(a->hwnd, L"freecell.chm", CE_HH_DISPLAY_TOPIC))
        builtin_help(a);
}

void help_search(App *a)
{
    if (!a->in_modal && !ce_help_open_chm(a->hwnd, L"freecell.chm", CE_HH_DISPLAY_INDEX))
        builtin_help(a);
}

void help_howto(App *a)
{
    if (!a->in_modal && !ce_help_open_chm(a->hwnd, L"NTHelp.chm", CE_HH_DISPLAY_TOPIC))
        builtin_help(a);
}

void help_about(App *a)
{
    HICON icon;
    if (a->in_modal)
        return;
    icon = LoadIconW(a->inst, MAKEINTRESOURCEW(IDI_FREECELL));
    modal_begin(a);
    /* "title#first line". The other text sits in a static control below the copyright line that
     * holds two lines of about 270 px (180 x 20 DLU) on XP; anything longer is clipped, so the art
     * credit is condensed (Tahoma 8: 230 and 247 px). The full wording is in the version info.
     * ce_shell_about passes writable copies (XP's ShellAboutW writes into the title). */
    ce_shell_about(a->hwnd, L"FreeCell HD#FreeCell HD",
                   L"A resizable re-creation of Windows XP FreeCell.\r\n"
                   L"Card faces by Adrian Kennard: cards.revk.uk (CC0)", icon);
    modal_end(a);
}

/* Close help windows before exit (engine/win32/help.h). */
void help_shutdown(App *a)
{
    (void)a;
    ce_help_shutdown();
}
