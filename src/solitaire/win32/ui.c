/*
 * Solitaire HD — the session's UI callbacks (session.h SolSessionUI), the dialogs (resources.md §5),
 * the menu graying (§2.3), Help and About, and where settings are kept.
 *
 * Settings: Options and Back in XP's own key and format, HKCU\Software\Microsoft\Solitaire (REG_DWORD,
 * rules.md §10), so the original and the HD game share them; the extras (window placement, full
 * screen, the Options dialog's Extras group: AutoTurn ClickToMove AutoFinish WinnableOnly SaveGame
 * WarnUnwinnable) in HKCU\Software\xp-cards\Solitaire HD; the statistics and the saved game in
 * %APPDATA%\xp-cards\Solitaire HD\statistics.bin and game.bin (written atomically).
 *
 * Every modal prompt first brings the window up to date and counts itself in a->in_modal, so input and
 * commands that arrive through the modal loop are ignored meanwhile. XP's dialogs are not centred:
 * they appear at their template position from the main window's client origin (here also kept on the
 * monitor's work area, and logged for the e2e scripts).
 */
#include "app.h"

#include <stddef.h>
#include <string.h>

const WCHAR SOL_APP_KEY[] = CE_APP_KEY_ROOT L"Solitaire HD";

static const CeRegStore xp_key = { L"Software\\Microsoft\\Solitaire", 0, NULL, NULL };
static const CeRegStore app_key = { SOL_APP_KEY, 0, NULL, NULL };

static const CeAppFile stats_file = { L"Solitaire HD", L"statistics" };
static const CeAppFile game_file = { L"Solitaire HD", L"game" };

CeStore storage_xp_store(void) { return ce_reg_store(&xp_key); }
CeStore storage_app_store(void) { return ce_reg_store(&app_key); }
CeBlobIO storage_stats_io(void) { return ce_app_file_io(&stats_file); }
void storage_stats_set_aside(void) { ce_app_file_set_aside(&stats_file); }
CeBlobIO storage_game_io(void) { return ce_app_file_io(&game_file); }
void storage_game_set_aside(void) { ce_app_file_set_aside(&game_file); }

/* ---- small helpers -------------------------------------------------------------------------------- */

int load_wstr(App *a, UINT id, WCHAR *out, int n, const WCHAR *fallback)
{
    return ce_load_wstr(a->inst, id, out, n, fallback);
}

static void app_name(App *a, WCHAR *out, int n) { load_wstr(a, IDS_APPNAME, out, n, L"Solitaire"); }

void modal_begin(App *a) { a->in_modal++; }

/* A solver answer that arrived while the dialog was up is retried from the main message loop. */
void modal_end(App *a)
{
    if (--a->in_modal == 0 && a->solve_ready && a->hwnd)
        PostMessageW(a->hwnd, WM_APP_SOLVED, 0, 0);
}

uint32_t app_time(App *a)
{
    return a->have_fake_time ? a->fake_time : ce_unix_time();
}

static int msgbox(App *a, const WCHAR *text, UINT type)
{
    WCHAR cap[32];
    int r;
    app_name(a, cap, 32);
    view_anim_idle(a);
    view_sync_now(a);
    modal_begin(a);
    r = MessageBoxW(a->hwnd, text, cap, type);
    modal_end(a);
    return r;
}

static INT_PTR run_dialog(App *a, int id, DLGPROC proc, LPARAM lp)
{
    INT_PTR r;
    view_anim_idle(a);
    view_sync_now(a);
    modal_begin(a);
    r = DialogBoxParamW(a->inst, MAKEINTRESOURCEW(id), a->hwnd, proc, lp);
    modal_end(a);
    return r;
}

/* XP leaves a dialog at its template position (relative to the client origin); keep it on screen. */
static void place_dialog(HWND d, const char *what)
{
    RECT r;
    GetWindowRect(d, &r);
    ce_dialog_move(d, r, g_app.hwnd, what);
}

static int checked(HWND d, int id) { return IsDlgButtonChecked(d, id) == BST_CHECKED; }
static void set_check(HWND d, int id, int on) { CheckDlgButton(d, id, on ? BST_CHECKED : BST_UNCHECKED); }

static int dialogs_allowed(App *a) { return !a->in_modal && !a->s.busy && a->gfx; }

/* ---- menu ------------------------------------------------------------------------------------------ */

static void set_item(App *a, int id, int on, int *cache)
{
    if (*cache == on)
        return;
    *cache = on;
    EnableMenuItem(a->menu, (UINT)id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
}

/* XP's WM_INITMENU (0x10018E6): Undo needs something to undo and no drag; Deal, Deck and About are
 * grayed while a card is dragged. Kept up to date after every input as well (the e2e scripts and
 * accelerators see the same state). */
void menu_update(App *a)
{
    int idle = sol_idle(&a->s) && !a->s.busy;
    if (!a->menu)
        return;
    set_item(a, IDM_UNDO, sol_undo_enabled(&a->s), &a->menu_undo);
    set_item(a, IDM_REDO, sol_redo_enabled(&a->s), &a->menu_redo);
    set_item(a, IDM_HINT, sol_hint_enabled(&a->s), &a->menu_hint);         /* extras */
    set_item(a, IDM_FINISH, sol_finish_enabled(&a->s), &a->menu_finish);
    if (a->menu_idle != idle) {
        a->menu_idle = idle;
        EnableMenuItem(a->menu, IDM_DEAL, MF_BYCOMMAND | (idle ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(a->menu, IDM_DECK, MF_BYCOMMAND | (idle ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(a->menu, IDM_ABOUT, MF_BYCOMMAND | (idle ? MF_ENABLED : MF_GRAYED));
    }
}

/* ---- Options (dialog 103, proc 0x100575F) ------------------------------------------------------------ */

static void options_enable_cumulative(HWND d, int vegas)
{
    EnableWindow(GetDlgItem(d, IDC_CUMULATIVE), vegas);
    EnableWindow(GetDlgItem(d, IDC_CUMSCORE), vegas);
}

typedef struct OptionsParam {
    SolOptions o;
    SolExtras  x;
} OptionsParam;

static const struct { int id; size_t off; } extra_boxes[] = {
    { IDC_AUTOTURN, offsetof(SolExtras, auto_turn) },
    { IDC_CLICKMOVE, offsetof(SolExtras, click_move) },
    { IDC_AUTOFINISH, offsetof(SolExtras, auto_finish) },
    { IDC_WINNABLE, offsetof(SolExtras, winnable_only) },
    { IDC_SAVEGAME, offsetof(SolExtras, save_game) },
    { IDC_WARNUNWINNABLE, offsetof(SolExtras, warn_unwinnable) },
};

static int *extra_of(SolExtras *x, int k) { return (int *)((char *)x + extra_boxes[k].off); }

static INT_PTR CALLBACK options_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
    OptionsParam *op = (OptionsParam *)GetWindowLongPtrW(d, DWLP_USER);
    SolOptions *o = op ? &op->o : NULL;
    int k;
    switch (m) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(d, DWLP_USER, lp);
        op = (OptionsParam *)lp;
        o = &op->o;
        place_dialog(d, "options");
        for (k = 0; k < (int)(sizeof extra_boxes / sizeof extra_boxes[0]); k++)
            set_check(d, extra_boxes[k].id, *extra_of(&op->x, k));
        CheckRadioButton(d, IDC_STANDARD, IDC_NONE, IDC_STANDARD + o->scoring);
        CheckRadioButton(d, IDC_DRAWONE, IDC_DRAWTHREE, o->draw == 1 ? IDC_DRAWONE : IDC_DRAWTHREE);
        set_check(d, IDC_STATUSBAR, o->status_bar);
        set_check(d, IDC_TIMED, o->timed);
        set_check(d, IDC_OUTLINE, o->outline);
        set_check(d, IDC_CUMULATIVE, o->cumulative);
        options_enable_cumulative(d, o->scoring == SOL_SCORING_VEGAS);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_DRAWONE:
        case IDC_DRAWTHREE:                           /* BS_RADIOBUTTON: the dialog checks them itself */
            CheckRadioButton(d, IDC_DRAWONE, IDC_DRAWTHREE, LOWORD(wp));
            return TRUE;
        case IDC_STANDARD:
        case IDC_VEGAS:
        case IDC_NONE:
            CheckRadioButton(d, IDC_STANDARD, IDC_NONE, LOWORD(wp));
            options_enable_cumulative(d, LOWORD(wp) == IDC_VEGAS);
            return TRUE;
        case IDOK:
            o->draw = checked(d, IDC_DRAWONE) ? 1 : 3;
            o->scoring = checked(d, IDC_VEGAS) ? SOL_SCORING_VEGAS : checked(d, IDC_NONE) ? SOL_SCORING_NONE
                                                                                         : SOL_SCORING_STANDARD;
            o->timed = checked(d, IDC_TIMED);
            o->status_bar = checked(d, IDC_STATUSBAR);
            o->outline = checked(d, IDC_OUTLINE);
            o->cumulative = checked(d, IDC_CUMULATIVE);
            for (k = 0; k < (int)(sizeof extra_boxes / sizeof extra_boxes[0]); k++)
                *extra_of(&op->x, k) = checked(d, extra_boxes[k].id);
            EndDialog(d, 1);
            return TRUE;
        case IDCANCEL:
            EndDialog(d, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void dlg_options(App *a)
{
    OptionsParam op;
    int redeal;
    if (!dialogs_allowed(a))
        return;
    op.o = a->s.opts;
    op.x = a->s.extras;
    if (run_dialog(a, IDD_OPTIONS, options_proc, (LPARAM)&op) != 1) {
        solver_deliver(a);
        return;
    }
    if (sol_dragging(&a->s))
        sol_cancel_drag(&a->s);                       /* (the dialog took the focus: already cancelled) */
    sol_set_extras(&a->s, &op.x);                     /* first: a redeal below may want WinnableOnly */
    sol_extras_save(&a->s.extras, &a->app_store);
    redeal = sol_apply_options(&a->s, &op.o);         /* writes Options; a new Draw / Timed / Scoring deals */
    ce_log("options: 0x%02x%s; extras: turn %d, click %d, finish %d, winnable %d, save %d, warn %d",
           (unsigned)sol_options_pack(&a->s.opts), redeal ? ", new deal" : "", a->s.extras.auto_turn,
           a->s.extras.click_move, a->s.extras.auto_finish, a->s.extras.winnable_only, a->s.extras.save_game,
           a->s.extras.warn_unwinnable);
    if ((a->status != NULL) != (a->s.opts.status_bar != 0))
        status_show(a, a->s.opts.status_bar);         /* shows or hides it at once, the board re-laid out */
    status_update(a);
    after_input(a);
}

/* ---- Select Card Back (dialog 101, proc 0x1005AB4) ---------------------------------------------------- */

typedef struct DeckParam {
    int      back;                  /* the selection (focus = selection, as XP) */
    CeImage *img[SOL_NBACKS];       /* the 12 backs at the buttons' inner size, decoded once */
    int      iw, ih;
} DeckParam;

/* Draw a premultiplied image over a solid colour. */
static void draw_image(HDC dc, const CeImage *img, int x, int y, COLORREF bg)
{
    BITMAPINFO bi;
    void *bits = NULL;
    HBITMAP b;
    HDC mem;
    CeImage v;
    if (!img || img->w <= 0 || img->h <= 0)
        return;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = img->w;
    bi.bmiHeader.biHeight = -img->h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    b = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!b || !bits) {
        if (b)
            DeleteObject(b);
        return;
    }
    v = ce_image_wrap(img->w, img->h, img->w, (uint32_t *)bits);
    ce_fill_rect(&v, 0, 0, img->w, img->h, CE_RGB(GetRValue(bg), GetGValue(bg), GetBValue(bg)));
    ce_blit(&v, img, 0, 0);
    mem = CreateCompatibleDC(dc);
    if (mem) {
        HGDIOBJ old = SelectObject(mem, b);
        BitBlt(dc, x, y, img->w, img->h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
    }
    DeleteObject(b);
}

/* XP's frame (0x1005A48): two nested 1-px frames in the highlight colour around the selected back, in
 * the face colour (invisible) around the others. */
static void deck_frame(HDC dc, RECT rc, int focused)
{
    HBRUSH br = GetSysColorBrush(focused ? COLOR_HIGHLIGHT : COLOR_BTNFACE);
    FrameRect(dc, &rc, br);
    InflateRect(&rc, -1, -1);
    FrameRect(dc, &rc, br);
}

static void deck_draw(HWND d, const DRAWITEMSTRUCT *di, DeckParam *p)
{
    int i = (int)di->CtlID - IDC_BACK0;
    RECT rc = di->rcItem, in = di->rcItem;
    InflateRect(&in, -3, -3);
    if (i < 0 || i >= SOL_NBACKS)
        return;
    if (di->itemAction & (ODA_DRAWENTIRE | ODA_SELECT)) {
        FillRect(di->hDC, &in, GetSysColorBrush(COLOR_BTNFACE));
        draw_image(di->hDC, p->img[i], in.left + (in.right - in.left - (p->img[i] ? p->img[i]->w : 0)) / 2,
                   in.top + (in.bottom - in.top - (p->img[i] ? p->img[i]->h : 0)) / 2, GetSysColor(COLOR_BTNFACE));
        if (di->itemState & ODS_SELECTED)
            InvertRect(di->hDC, &in);                 /* pressed */
    }
    if ((di->itemAction & ODA_FOCUS) && (di->itemState & ODS_FOCUS))
        p->back = i;                                  /* focus = selection */
    deck_frame(di->hDC, rc, (di->itemState & ODS_FOCUS) != 0);
}

static INT_PTR CALLBACK deck_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
    DeckParam *p = (DeckParam *)GetWindowLongPtrW(d, DWLP_USER);
    switch (m) {
    case WM_INITDIALOG: {
        RECT br;
        int i;
        double t0 = ce_now_ms();
        SetWindowLongPtrW(d, DWLP_USER, lp);
        p = (DeckParam *)lp;
        place_dialog(d, "deck");
        GetClientRect(GetDlgItem(d, IDC_BACK0), &br);
        p->iw = br.right - 6;                         /* the back inset 3 px (XP) */
        p->ih = br.bottom - 6;
        if (p->iw * 7 > p->ih * 5)                    /* keep the card's 5:7 shape */
            p->iw = p->ih * 5 / 7;
        else
            p->ih = p->iw * 7 / 5;
        for (i = 0; i < SOL_NBACKS; i++)
            p->img[i] = sol_back_image(ce_rcdata_loader, (void *)g_app.inst, i, p->iw, p->ih);
        ce_log("deck dialog: 12 backs at %dx%d in %.1f ms", p->iw, p->ih, ce_now_ms() - t0);
        SetFocus(GetDlgItem(d, IDC_BACK0 + p->back));
        return FALSE;                                 /* the focus is set */
    }
    case WM_DRAWITEM:
        if (p && wp >= IDC_BACK0 && wp <= IDC_BACKLAST) {
            deck_draw(d, (const DRAWITEMSTRUCT *)lp, p);
            return TRUE;
        }
        break;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= IDC_BACK0 && id <= IDC_BACKLAST && p) {
            p->back = id - IDC_BACK0;
            if (HIWORD(wp) == BN_DOUBLECLICKED) {
                EndDialog(d, 1);                      /* a double-click is OK */
            } else if (GetFocus() != GetDlgItem(d, id)) {
                SetFocus(GetDlgItem(d, id));          /* (a click focuses it anyway) */
            }
            return TRUE;
        }
        if (id == IDOK) {
            EndDialog(d, 1);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(d, 0);
            return TRUE;
        }
        break;
    }
    case WM_DESTROY:
        if (p) {
            int i;
            for (i = 0; i < SOL_NBACKS; i++) {
                ce_image_free(p->img[i]);
                p->img[i] = NULL;
            }
        }
        break;
    }
    return FALSE;
}

void dlg_deck(App *a)
{
    DeckParam p;
    if (!dialogs_allowed(a) || !sol_idle(&a->s))
        return;
    memset(&p, 0, sizeof p);
    p.back = a->s.back;
    if (run_dialog(a, IDD_DECK, deck_proc, (LPARAM)&p) != 1)
        return;
    sol_set_back(&a->s, p.back);                      /* writes Back = index + 1; never redeals */
    ce_log("back %d (%s)", a->s.back, sol_back_names[a->s.back]);
    after_input(a);
}

/* ---- Statistics (extra, v1.1) -------------------------------------------------------------------------- */

static void stats_save(App *a)
{
    CeBlobIO io = storage_stats_io();
    if (!sol_stats_save(&a->s.stats, &io))
        ce_log("statistics: could not be written");
}

static void stats_show(HWND d, int mode)
{
    App *a = &g_app;
    char buf[256];
    WCHAR w[256];
    if (mode < 0 || mode >= SOL_STATS_MODES)
        mode = 0;
    sol_stats_format(&a->s.stats.m[mode], mode % 3 == 1 ? SOL_SCORING_VEGAS : mode % 3 == 2 ? SOL_SCORING_NONE
                                                                                        : SOL_SCORING_STANDARD,
                     a->s.currency, buf, sizeof buf);
    ce_to_wide(buf, w, 256);
    SetDlgItemTextW(d, IDC_STATS_VALUES, w);
}

static INT_PTR CALLBACK stats_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
    App *a = &g_app;
    HWND combo = GetDlgItem(d, IDC_STATS_MODE);
    switch (m) {
    case WM_INITDIALOG: {
        WCHAR t[256];
        int i, cur = (int)lp;
        place_dialog(d, "statistics");
        for (i = 0; i < SOL_STATS_MODES; i++) {
            static const WCHAR *const names[SOL_STATS_MODES] = { L"Draw One, Standard", L"Draw One, Vegas",
                L"Draw One, None", L"Draw Three, Standard", L"Draw Three, Vegas", L"Draw Three, None" };
            load_wstr(a, IDS_STATS_MODE0 + i, t, 64, names[i]);
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)t);
        }
        SendMessageW(combo, CB_SETCURSEL, (WPARAM)cur, 0);
        load_wstr(a, IDS_STATS_LABELS, t, 256,
                  L"Games played:\nGames won:\nWin percentage:\nCurrent streak:\nLongest winning streak:\n"
                  L"Longest losing streak:\nBest time:\nBest score:");
        SetDlgItemTextW(d, IDC_STATS_LABELS, t);
        stats_show(d, cur);
        ce_log("statistics dialog: mode %d", cur);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_STATS_MODE:
            if (HIWORD(wp) == CBN_SELCHANGE)
                stats_show(d, (int)SendMessageW(combo, CB_GETCURSEL, 0, 0));
            return TRUE;
        case IDC_STATS_RESET: {
            WCHAR text[128], cap[32];
            load_wstr(a, IDS_RESETSTATS, text, 128, L"Are you sure you want to delete all statistics?");
            app_name(a, cap, 32);
            if (MessageBoxW(d, text, cap, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                sol_reset_stats(&a->s);               /* written through stats_changed */
                ce_log("statistics: reset");
                stats_show(d, (int)SendMessageW(combo, CB_GETCURSEL, 0, 0));
            }
            return TRUE;
        }
        case IDOK:
        case IDCANCEL:
            EndDialog(d, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Game > Statistics (F4): the current game's mode first (or the Options' when no game is on). */
void dlg_statistics(App *a)
{
    int mode;
    if (!dialogs_allowed(a) || !sol_idle(&a->s))
        return;
    mode = a->s.dealt ? sol_stats_mode(a->s.draw, a->s.game_scoring) : sol_stats_mode(a->s.opts.draw, a->s.opts.scoring);
    run_dialog(a, IDD_STATS, stats_proc, (LPARAM)mode);
    solver_deliver(a);
}

/* ---- Help (resources.md §2.1): sol.chm through HtmlHelp, our own text when it is missing ---------- */

static const WCHAR how_to_play[] =
    L"The goal is to move all 52 cards to the four foundations (top right), building each suit up "
    L"from Ace to King.\n\n"
    L"On the seven tableau columns, build down in alternating colors: a red card goes on a black card "
    L"one rank higher, and a black card on a red one. Drag a face-up card, together with the cards on "
    L"it, to move it. Only a King (or a run headed by one) may go to an empty column. Click a "
    L"face-down card on top of a column to turn it over.\n\n"
    L"Click the deck (top left) to turn over cards: one or three at a time (Options). Only the top "
    L"card of the turned-over pile can be played. When the deck is empty, click its place to turn the "
    L"pile over again (Vegas scoring allows one pass with Draw One, three with Draw Three; then it "
    L"shows an X).\n\n"
    L"Double-click a card to send it to a foundation; right-click (or Ctrl+A) to send every card "
    L"that can go there.\n\n"
    L"Scoring. Standard: 10 points for each card moved to a foundation, 5 for each card moved from "
    L"the deck to a column, 5 for each card turned over, -15 for a card moved from a foundation back "
    L"to a column, -100 for each pass through the deck after the first (Draw One; Draw Three: -20 "
    L"after the fourth), -2 every 10 seconds of a timed game, -2 for each Undo; a timed game won in "
    L"30 seconds or more earns a bonus. Vegas: you bet $52 and win $5 for each card on a "
    L"foundation; Cumulative keeps a running total.\n\n"
    L"Keyboard: arrow keys, Tab, Home and End move between the piles and cards; Enter or Space picks "
    L"up and drops; Esc cancels. F2 Deal, Ctrl+Y Redo, F11 or Alt+Enter Full Screen (Esc leaves it), "
    L"H Hint, F6 Finish (once every card is face up and the deck is used up), F4 Statistics.\n\n"
    L"Options > Extras (all off by default): turn cards over automatically, single click moves a card, "
    L"finish automatically, deal only winnable games, save the game on exit, warn when the game can't "
    L"be won.";

static void builtin_help(App *a)
{
    view_sync_now(a);
    modal_begin(a);
    MessageBoxW(a->hwnd, how_to_play, L"How to Play Solitaire", MB_OK | MB_ICONINFORMATION);
    modal_end(a);
}

static void help_open(App *a, const WCHAR *file, UINT cmd)
{
    if (a->in_modal)
        return;
    if (!ce_help_open_chm(a->hwnd, file, cmd))
        builtin_help(a);
}

void help_contents(App *a)
{
    WCHAR chm[32];
    load_wstr(a, IDS_HELPFILE, chm, 32, L"sol.chm");
    help_open(a, chm, CE_HH_DISPLAY_TOPIC);
}

void help_search(App *a)
{
    WCHAR chm[32];
    load_wstr(a, IDS_HELPFILE, chm, 32, L"sol.chm");
    help_open(a, chm, CE_HH_DISPLAY_INDEX);
}

void help_howto(App *a) { help_open(a, L"NTHelp.chm", CE_HH_DISPLAY_TOPIC); }

/* XP: ShellAboutW(hwnd, "Solitaire", "Developed for Microsoft by Wes Cherry", icon 500). Ours names
 * the HD game and the art; ce_shell_about passes writable copies (XP's ShellAboutW writes into the
 * title: a string literal crashed it on real XP). */
void help_about(App *a)
{
    WCHAR text[256];
    HICON icon;
    if (a->in_modal || !sol_idle(&a->s))
        return;
    load_wstr(a, IDS_ABOUTTEXT, text, 256,
              L"A resizable re-creation of Windows XP Solitaire.\r\nCards by Adrian Kennard: cards.revk.uk (CC0)");
    icon = LoadIconW(a->inst, MAKEINTRESOURCEW(IDI_SOLITAIRE));
    view_sync_now(a);
    modal_begin(a);
    ce_shell_about(a->hwnd, L"Solitaire HD#Solitaire HD", text, icon);
    modal_end(a);
}

/* ---- session callbacks ------------------------------------------------------------------------------- */

/* The session's state changed: render it now (the win flow runs right after the last move's call). */
static void cb_invalidate(void *ctx)
{
    view_sync((App *)ctx);
}

static void cb_status_changed(void *ctx)
{
    status_update((App *)ctx);
}

static void cb_set_timer(void *ctx, int id, int ms)
{
    App *a = ctx;
    if (!a->hwnd)
        return;
    if (ms > 0)
        SetTimer(a->hwnd, (UINT_PTR)id, (UINT)ms, NULL);
    else
        KillTimer(a->hwnd, (UINT_PTR)id);
}

/* The win (rules.md §8): the text in the status bar, the cascade, then the text cleared. */
static void cb_win_cascade(void *ctx)
{
    App *a = ctx;
    WCHAR text[128], press[64], bonus[24];
    load_wstr(a, IDS_PRESSESC, press, 64, L"Press Esc or a mouse button to stop...");
    if (a->s.opts.scoring == SOL_SCORING_STANDARD) {
        load_wstr(a, IDS_BONUS, bonus, 24, L"Bonus: ");
        wsprintfW(text, L"%s%d  %s", bonus, a->s.bonus, press);
    } else {
        lstrcpyW(text, press);
    }
    ce_log("won%s: score %d, bonus %d, %d s", a->s.forced_win ? " (Alt+Shift+2)" : "", a->s.score, a->s.bonus,
           sol_seconds(&a->s));
    menu_update(a);
    status_set_left(a, text);
    view_cascade(a);
    status_set_left(a, L"");
}

static int cb_deal_again(void *ctx)
{
    App *a = ctx;
    WCHAR text[64];
    load_wstr(a, IDS_DEALAGAIN, text, 64, L"Deal Again?");
    return msgbox(a, text, MB_YESNO | MB_ICONEXCLAMATION) == IDYES;
}

static void cb_post_command(void *ctx, int cmd)
{
    App *a = ctx;
    if (a->hwnd)
        PostMessageW(a->hwnd, WM_COMMAND, MAKEWPARAM((WORD)cmd, 0), 0);
}

static uint32_t cb_now_seed(void *ctx) { return app_time((App *)ctx); }

static void cb_kbd_cursor(void *ctx, int pile, int card, int dragging)
{
    view_kbd_cursor((App *)ctx, pile, card, dragging);
}

/* ---- the extras' callbacks ---- */

static void cb_message(void *ctx, int id, const char *text)
{
    App *a = ctx;
    WCHAR fb[160], w[160];
    ce_to_wide(text, fb, 160);
    load_wstr(a, (UINT)id, w, 160, fb);
    ce_log("message %d", id);
    msgbox(a, w, MB_OK | MB_ICONINFORMATION);
}

static void cb_animate_move(void *ctx, int src, int dst) { view_animate_move((App *)ctx, src, dst); }

static void cb_stats_changed(void *ctx) { stats_save((App *)ctx); }

static void cb_solve_start(void *ctx, uint32_t id, const SolBoard *b, int draw, int left)
{
    solver_request((App *)ctx, id, b, draw, left);
}

static void cb_solve_cancel(void *ctx) { solver_cancel((App *)ctx); }

void ui_make(App *a, SolSessionUI *ui)
{
    memset(ui, 0, sizeof *ui);
    ui->ctx = a;
    ui->invalidate = cb_invalidate;
    ui->status_changed = cb_status_changed;
    ui->set_timer = cb_set_timer;
    ui->win_cascade = cb_win_cascade;
    ui->deal_again = cb_deal_again;
    ui->post_command = cb_post_command;
    ui->now_seed = cb_now_seed;
    ui->kbd_cursor = cb_kbd_cursor;
    ui->message = cb_message;
    ui->animate_move = cb_animate_move;
    ui->stats_changed = cb_stats_changed;
    ui->solve_start = cb_solve_start;
    ui->solve_cancel = cb_solve_cancel;
}
