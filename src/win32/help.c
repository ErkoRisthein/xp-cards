/*
 * FreeCell HD — Help menu (resources.md §2): Contents / Search open XP's freecell.chm through
 * HtmlHelpW, loaded at run time from hhctrl.ocx (XP resolves it the same way, by ordinal); How to Use
 * Help opens NTHelp.chm. When HtmlHelp or the help file is missing, a built-in "How to Play" text is
 * shown instead. About uses ShellAboutW like XP.
 */
#include "app.h"

typedef HWND (WINAPI *HtmlHelpW_fn)(HWND, LPCWSTR, UINT, DWORD_PTR);

#define HH_DISPLAY_TOPIC 0x0000
#define HH_DISPLAY_INDEX 0x0002
#define HH_CLOSE_ALL     0x0012

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
    L"Keyboard: 1-8 select or move to a column, 0 a free cell, 9 home. F2 New Game, F3 Select "
    L"Game, F10 Undo, Ctrl+Y Redo, F11 or Alt+Enter Full Screen (Esc leaves it).";

static HtmlHelpW_fn get_html_help(App *a)
{
    if (!a->hh_tried) {
        a->hh_tried = 1;
        a->hh = LoadLibraryW(L"hhctrl.ocx");
        if (a->hh)
            a->html_help = (void *)GetProcAddress(a->hh, "HtmlHelpW");
    }
    return (HtmlHelpW_fn)a->html_help;
}

static void builtin_help(App *a)
{
    a->in_modal++;
    MessageBoxW(a->hwnd, how_to_play, L"How to Play FreeCell", MB_OK | MB_ICONINFORMATION);
    a->in_modal--;
}

/* name without a path: HtmlHelp's own search (Windows\Help); then %windir%\Help\name explicitly. */
static int open_chm(App *a, const WCHAR *name, UINT cmd)
{
    HtmlHelpW_fn hh = get_html_help(a);
    WCHAR path[MAX_PATH + 32];
    UINT n;
    if (!hh)
        return 0;
    if (hh(a->hwnd, name, cmd, 0))
        return 1;
    n = GetWindowsDirectoryW(path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return 0;
    lstrcatW(path, L"\\Help\\");
    lstrcatW(path, name);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return 0;
    return hh(a->hwnd, path, cmd, 0) != NULL;
}

void help_contents(App *a)
{
    if (!a->in_modal && !open_chm(a, L"freecell.chm", HH_DISPLAY_TOPIC))
        builtin_help(a);
}

void help_search(App *a)
{
    if (!a->in_modal && !open_chm(a, L"freecell.chm", HH_DISPLAY_INDEX))
        builtin_help(a);
}

void help_howto(App *a)
{
    if (!a->in_modal && !open_chm(a, L"NTHelp.chm", HH_DISPLAY_TOPIC))
        builtin_help(a);
}

void help_about(App *a)
{
    HICON icon;
    if (a->in_modal)
        return;
    icon = LoadIconW(a->inst, MAKEINTRESOURCEW(IDI_FREECELL));
    a->in_modal++;
    /* "title#first line". The other text sits in a static control below the copyright line that
     * holds two lines of about 270 px (180 x 20 DLU) on XP; anything longer is clipped, so the art
     * credit is condensed (Tahoma 8: 230 and 247 px). The full wording is in the version info. */
    ShellAboutW(a->hwnd, L"FreeCell HD#FreeCell HD",
                L"A resizable re-creation of Windows XP FreeCell.\r\n"
                L"Card faces by Adrian Kennard: cards.revk.uk (CC0)",
                icon);
    a->in_modal--;
}

/* Close help windows before exit. hhctrl.ocx is not unloaded: its worker thread may still run, and
 * unloading under it is a known crash; the process exit releases it. */
void help_shutdown(App *a)
{
    HtmlHelpW_fn hh = (HtmlHelpW_fn)a->html_help;
    if (hh)
        hh(NULL, NULL, HH_CLOSE_ALL, 0);
}
