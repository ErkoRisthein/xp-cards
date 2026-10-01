/*
 * Solitaire HD — WinMain, the main window and its window procedure.
 *
 * The window procedure is a thin adapter from Win32 messages to the session (src/solitaire/session.h,
 * whose header lists the mapping); the view (view.c) renders what changed after every session call.
 * Mouse plumbing is XP's (layout.md §6): a press acts on button-down (a stock draw, a turn-over, or a
 * drag with SetCapture), the stack follows the pointer, the release drops it on the target found by
 * the last move; a double-click sends a card home; a right-button press plays every card it can to
 * the foundations. Differences from XP's window (layout.md §1): resizable with a scaled board (minimum
 * size from the layout), the placement remembered, a borderless full-screen mode (Game > Full Screen,
 * F11 / Alt+Enter, Esc leaves it), Redo (Ctrl+Y).
 */
#include "app.h"

#include <commctrl.h>
#include <stdlib.h>
#include <string.h>

App g_app;

#define REG_FULLSCREEN "FullScreen"

/* ---- window geometry ------------------------------------------------------------------------------ */

static int status_h_now(App *a) { return a->s.opts.status_bar ? a->status_h : 0; }

static void min_track(App *a, int *w, int *h)
{
    int cw, ch;
    sol_layout_min_client(status_h_now(a), &cw, &ch);
    ce_window_outer_size(cw, ch, w, h);
}

/* First run: XP's proportions at the largest scale that fits (engine/win32/window.h). */
static void client_for_scale(double s, int *w, int *h)
{
    sol_layout_client_for_scale(s, status_h_now(&g_app), w, h);
}

/* ---- full screen (extra) ------------------------------------------------------------------------- */

static void fullscreen_done(App *a)
{
    if (a->menu)
        CheckMenuItem(a->menu, IDM_FULLSCREEN, MF_BYCOMMAND | (a->fs.on ? MF_CHECKED : MF_UNCHECKED));
    DrawMenuBar(a->hwnd);
    ce_log("full screen %s", a->fs.on ? "on" : "off");
}

static void fullscreen_enter(App *a, const WINDOWPLACEMENT *prev)
{
    if (ce_fullscreen_enter(&a->fs, a->hwnd, prev))
        fullscreen_done(a);
}

void fullscreen_set(App *a, int on)
{
    if (on)
        fullscreen_enter(a, NULL);
    else if (ce_fullscreen_leave(&a->fs, a->hwnd))
        fullscreen_done(a);
}

/* WM_CLOSE / WM_ENDSESSION: the placement (in full screen: the one to come back to) and the mode.
 * XP saves nothing at exit: Options and Back are written when their dialogs are confirmed. */
static void save_window_state(App *a)
{
    ce_fullscreen_save_placement(&a->fs, a->hwnd, SOL_APP_KEY);
    ce_store_set(&a->app_store, REG_FULLSCREEN, a->fs.on ? 1u : 0u);
}

/* ---- input ---------------------------------------------------------------------------------------- */

static int input_blocked(App *a) { return a->in_modal > 0 || !a->have_layout || !a->gfx || a->s.busy; }

static int key_mods(void)
{
    int m = 0;
    if (GetKeyState(VK_SHIFT) < 0)
        m |= SOL_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) < 0)
        m |= SOL_MOD_CTRL;
    if (GetKeyState(VK_MENU) < 0)
        m |= SOL_MOD_ALT;
    return m;
}

static void release_capture(App *a)
{
    if (a->lcapture) {
        a->lcapture = 0;                              /* first: WM_CAPTURECHANGED must not cancel */
        ReleaseCapture();
    }
}

void after_input(App *a)
{
    view_anim_idle(a);
    if (!sol_dragging(&a->s))
        release_capture(a);
    view_sync(a);
    menu_update(a);
}

/* A drag that does not land (Esc, focus lost, capture lost): the cards slide back (XP: MouseUp with
 * fCancel -> AnimateBack), nothing is recorded. */
static void drag_cancel(App *a, int zip, const char *why)
{
    if (!sol_dragging(&a->s))
        return;
    release_capture(a);
    if (zip)
        view_zip_back(a);
    sol_cancel_drag(&a->s);
    ce_log("drag cancelled (%s)", why);
}

/* The button went up: drop on the target the last move found, or slide back. */
static void drag_drop(App *a)
{
    int t = a->s.target, ok;
    release_capture(a);
    if (t < 0 || !sol_can_drop_on(&a->s, t))
        view_zip_back(a);
    ok = sol_drop(&a->s, t);
    ce_log("drop: target %d, %s; score %d", t, ok ? "moved" : "refused", a->s.score);
}

static void on_button(App *a, LPARAM lp, int dbl)
{
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp), pile, card, r, mods = key_mods();
    if (input_blocked(a)) {
        ce_log("%s %d,%d ignored", dbl ? "dblclick" : "press", x, y);
        return;
    }
    if (sol_dragging(&a->s))
        return;                                       /* XP's MouseDown: nothing while a card is selected */
    if (!view_hit(a, x, y, &pile, &card)) {
        pile = SOL_MISS;
        card = -1;
    }
    a->press_valid = 1;
    a->press_x = x;
    a->press_y = y;
    r = dbl ? sol_dblclick(&a->s, pile, card, mods) : sol_press(&a->s, pile, card, mods);
    a->press_valid = 0;
    if (r == SOL_PRESS_DRAG && sol_dragging(&a->s) && a->hwnd) {
        SetCapture(a->hwnd);
        a->lcapture = 1;
        sol_drag_over(&a->s, -1);                     /* the view tracks the target: none until a move */
    }
    ce_log("%s %d,%d mods %d -> pile %d card %d: %s; score %d", dbl ? "dblclick" : "press", x, y, mods, pile,
           card, r == SOL_PRESS_DRAG ? "drag" : r == SOL_PRESS_DONE ? "done" : "nothing", a->s.score);
    after_input(a);
}

/* WM_KEYDOWN: XP's KeyHit (rules.md §9.1). Returns 0 for a key the game does not use. */
static int on_key(App *a, WPARAM wp)
{
    int mods = key_mods(), handled;
    if (wp == VK_ESCAPE) {
        if (sol_dragging(&a->s)) {
            drag_cancel(a, 1, "Esc");
            after_input(a);
            return 1;
        }
        if (a->fs.on) {
            fullscreen_set(a, 0);                     /* Esc leaves full screen */
            return 1;
        }
    }
    if ((wp == VK_RETURN || wp == VK_SPACE) && sol_dragging(&a->s)) {
        /* XP: MouseUp, a drop on the highlighted target (also during a mouse drag); a refused one
         * slides back first */
        if (sol_key_drop_target(&a->s) < 0)
            view_zip_back(a);
        release_capture(a);
    }
    handled = sol_key(&a->s, (int)wp, mods);
    if (handled)
        ce_log("key 0x%02x mods %d: cursor pile %d card %d%s; score %d", (unsigned)wp, mods, a->s.kbd_pile,
               a->s.kbd_card, sol_dragging(&a->s) ? " (dragging)" : "", a->s.score);
    after_input(a);
    return handled;
}

static void on_command(App *a, int id)
{
    switch (id) {
    case IDM_DEAL: case IDM_UNDO: case IDM_REDO: case IDM_FORCEWIN:
        if (a->in_modal || a->s.busy || !a->gfx)
            return;
        if (id == IDM_FORCEWIN)
            drag_cancel(a, 0, "force win");
        sol_command(&a->s, id);                       /* IDM_* == SOL_CMD_* */
        ce_log("command %d: seed %u, score %d", id, a->s.seed, a->s.score);
        after_input(a);
        break;
    case IDM_DECK: dlg_deck(a); break;
    case IDM_OPTIONS: dlg_options(a); break;
    case IDM_FULLSCREEN:
        if (!a->in_modal && !a->s.busy)
            fullscreen_set(a, !a->fs.on);
        break;
    case IDM_EXIT: PostMessageW(a->hwnd, WM_SYSCOMMAND, SC_CLOSE, 0); break;   /* as XP */
    case IDM_HELPCONTENTS: help_contents(a); break;
    case IDM_HELPSEARCH: help_search(a); break;
    case IDM_HELPHOWTO: help_howto(a); break;
    case IDM_ABOUT: help_about(a); break;
    }
}

/* WM_MENUSELECT (0x1005E8E): the highlighted item's help in the status bar; popups, the system menu,
 * separators and the menu's closing clear it. */
static void on_menu_select(App *a, WPARAM wp, LPARAM lp)
{
    UINT flags = HIWORD(wp), id = LOWORD(wp);
    WCHAR text[64] = L"";
    if (!(flags == 0xFFFF && lp == 0) && !(flags & (MF_POPUP | MF_SYSMENU | MF_SEPARATOR)))
        load_wstr(a, id, text, 64, L"");
    status_set_left(a, text);
}

/* ---- window procedure ------------------------------------------------------------------------------ */

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    App *a = &g_app;
    switch (m) {
    case WM_CREATE: {
        SolSessionUI ui;
        CeStore st = storage_xp_store();
        a->hwnd = h;
        a->menu = GetMenu(h);
        a->menu_undo = a->menu_redo = a->menu_idle = -1;
        ui_make(a, &ui);
        sol_init(&a->s, &ui, &st);                    /* Options, Back, iCurrency from XP's key */
        a->app_store = storage_app_store();
        if (a->s.opts.status_bar)
            a->status = CreateWindowExW(0, SOL_STATUS_CLASS, L"", WS_CHILD | WS_BORDER | WS_VISIBLE, 0, 0, 0,
                                        0, h, NULL, a->inst, NULL);
        menu_update(a);
        ce_log("options 0x%02x, back %d (%s)", (unsigned)sol_options_pack(&a->s.opts), a->s.back,
               sol_back_names[a->s.back]);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;                                     /* everything is painted from the back buffer */
    case WM_PAINT:
        view_paint(a);
        return 0;
    case WM_APP_SYNC:
        a->sync_posted = 0;
        view_sync(a);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        int w, ht;
        min_track(a, &w, &ht);
        mm->ptMinTrackSize.x = w;
        mm->ptMinTrackSize.y = ht;
        ce_window_outer_size(1920, 1200, &w, &ht);    /* allow a full-HD client even on a smaller screen */
        if (mm->ptMaxTrackSize.x < w) mm->ptMaxTrackSize.x = w;
        if (mm->ptMaxTrackSize.y < ht) mm->ptMaxTrackSize.y = ht;
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        ce_backbuf_enter_sizemove(&a->bb);
        return 0;
    case WM_EXITSIZEMOVE:
        view_exit_sizemove(a);
        return 0;
    case WM_SIZE:
        sol_set_minimized(&a->s, wp == SIZE_MINIMIZED);   /* the clock pauses while minimized */
        if (wp != SIZE_MINIMIZED && LOWORD(lp) > 0 && HIWORD(lp) > 0)
            view_resize(a, LOWORD(lp), HIWORD(lp));   /* 0 x 0 / minimized: keep the old buffer */
        return 0;
    case WM_DISPLAYCHANGE:
        ce_fullscreen_fit(&a->fs, h);                 /* full screen follows the new resolution */
        view_invalidate_all(a);
        break;

    case WM_LBUTTONDOWN:
        on_button(a, lp, 0);
        return 0;
    case WM_LBUTTONDBLCLK:
        on_button(a, lp, 1);
        return 0;
    case WM_MOUSEMOVE:
        if (a->drag_on && sol_dragging(&a->s) && !a->in_modal)
            view_drag_to(a, (short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_LBUTTONUP:
        if (sol_dragging(&a->s) && !a->in_modal && !a->s.busy) {
            drag_drop(a);                             /* XP's MouseUp: also ends a keyboard drag */
            after_input(a);
        } else {
            release_capture(a);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (a->lcapture && (HWND)lp != h) {           /* someone took the mouse */
            a->lcapture = 0;
            drag_cancel(a, 1, "capture lost");
            after_input(a);
        }
        return 0;
    case WM_RBUTTONDOWN:
        if (!input_blocked(a) && !GetCapture()) {     /* XP: anywhere, only without mouse capture */
            int n = sol_autoplay(&a->s);
            ce_log("autoplay: %d card(s); score %d", n, a->s.score);
            after_input(a);
        }
        return 0;
    case WM_KILLFOCUS:
        if (sol_dragging(&a->s) && !a->s.busy) {      /* XP: losing the focus cancels a drag, as Esc */
            drag_cancel(a, 1, "focus lost");
            after_input(a);
        }
        break;
    case WM_KEYDOWN:
        if (!input_blocked(a)) {
            if (on_key(a, wp))
                return 0;
        } else if (wp == VK_ESCAPE && a->fs.on && !a->in_modal && !a->s.busy) {
            fullscreen_set(a, 0);
            return 0;
        }
        break;                                        /* (F1 -> WM_HELP and the like) */
    case WM_TIMER:
        if (wp == SOL_TIMER_CLOCK) {
            sol_timer(&a->s, SOL_TIMER_CLOCK);        /* XP: 250-ms ticks, the clock in the status bar */
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if ((HWND)wp == h && LOWORD(lp) == HTCLIENT && !a->gfx) {
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));   /* decoding the cards at start-up */
            return TRUE;
        }
        break;

    case WM_INITMENU:
        a->menu_undo = a->menu_redo = a->menu_idle = -1;
        menu_update(a);
        return 0;
    case WM_MENUSELECT:
        on_menu_select(a, wp, lp);
        return 0;
    case WM_HELP:
        help_contents(a);                             /* XP: F1 arrives as WM_HELP */
        return TRUE;
    case WM_COMMAND:
        on_command(a, LOWORD(wp));
        return 0;

    case WM_CLOSE:
        if (a->in_modal || a->s.busy) {
            a->cascade_abort = 1;
            return 0;
        }
        save_window_state(a);
        DestroyWindow(h);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp)
            save_window_state(a);
        return 0;
    case WM_DESTROY:
        KillTimer(h, SOL_TIMER_CLOCK);
        view_anim_idle(a);
        ce_help_shutdown();
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        a->hwnd = NULL;
        a->status = NULL;
        break;
    }
    return DefWindowProcW(h, m, wp, lp);
}

/* ---- WinMain -------------------------------------------------------------------------------------- */

static void fatal(App *a, const WCHAR *text)
{
    WCHAR cap[32];
    load_wstr(a, IDS_APPNAME, cap, 32, L"Solitaire");
    MessageBoxW(a->hwnd, text, cap, MB_OK | MB_ICONHAND);
}

/* Test hooks (see app.h). */
static void read_env(App *a)
{
    WCHAR v[32];
    DWORD n = GetEnvironmentVariableW(L"SOLHD_TIME", v, 32);
    if (n > 0 && n < 32) {
        a->have_fake_time = 1;
        a->fake_time = (uint32_t)wcstoul(v, NULL, 10);
        ce_log("SOLHD_TIME: time(NULL) = %lu", (unsigned long)a->fake_time);
    }
    n = GetEnvironmentVariableW(L"SOLHD_NO_WARP", v, 32);
    a->no_warp = n > 0 && n < 32 && v[0] != L'0';
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show)
{
    App *a = &g_app;
    WNDCLASSEXW wc;
    WINDOWPLACEMENT wp;
    RECT r;
    WCHAR title[32];
    HDC sdc;
    int have_wp, minw, minh, ret, minimized_start, no_deal;
    double t0;

    a->inst = inst;
    InitCommonControls();                             /* activates comctl32 v6 (themed controls) */
    ce_log_open(L"SOLHD_TIMING_LOG");
    read_env(a);
    view_init(a);
    a->status_h = status_height();
    sdc = GetDC(NULL);
    a->status_font = CreateFontW(-MulDiv(9, sdc ? GetDeviceCaps(sdc, LOGPIXELSY) : 96, 72), 0, 0, 0, FW_BOLD, 0, 0,
                                 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"MS Shell Dlg");
    if (sdc)
        ReleaseDC(NULL, sdc);

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_DBLCLKS;                            /* XP: CS_DBLCLKS | CS_BYTEALIGNWINDOW */
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_SOLITAIRE));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_SOLITAIRE), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);     /* XP: the arrow, always */
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MENU);
    wc.lpszClassName = SOL_CLASS_NAME;
    if (!RegisterClassExW(&wc) || !status_register(inst)) {
        fatal(a, L"Solitaire HD could not register its window class.");
        view_free(a);
        return 1;
    }
    a->accel = LoadAcceleratorsW(inst, RES_ACCEL);

    /* the Options value decides the status bar, hence the default and minimum sizes */
    {
        CeStore st = storage_xp_store();
        sol_options_unpack(&a->s.opts, ce_store_get(&st, SOL_REG_OPTIONS, SOL_OPTIONS_DEFAULT));
    }
    ce_window_default_rect(&r, client_for_scale);
    min_track(a, &minw, &minh);
    /* XP deals at the end of its start-up unless nCmdShow is SW_MINIMIZE / SW_SHOWMINNOACTIVE
     * (0x1001E6E); "/I" only creates the window minimized (0x1001C7C) */
    no_deal = show == SW_MINIMIZE || show == SW_SHOWMINNOACTIVE;
    minimized_start = no_deal || (cmdline && (wcsstr(cmdline, L"/I") || wcsstr(cmdline, L"/i")));
    have_wp = ce_placement_load(SOL_APP_KEY, &wp) &&
              ce_placement_fix(&wp, minimized_start ? SW_SHOWMINNOACTIVE : show, minw, minh, &r);
    load_wstr(a, IDS_APPNAME, title, 32, L"Solitaire");
    if (!CreateWindowExW(0, SOL_CLASS_NAME, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | (minimized_start ? WS_MINIMIZE : 0),
                         r.left, r.top, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL)) {
        fatal(a, L"Out of memory");
        view_free(a);
        return 1;
    }
    if (ce_store_get(&a->app_store, REG_FULLSCREEN, 0) && !minimized_start && show != SW_SHOWMINIMIZED) {
        /* left in full screen last time: go straight there; leaving it restores the saved placement */
        WINDOWPLACEMENT before;
        memset(&before, 0, sizeof before);
        before.length = sizeof before;
        if (have_wp) {
            WINDOWPLACEMENT hidden = wp;
            before = wp;
            hidden.showCmd = SW_HIDE;
            SetWindowPlacement(a->hwnd, &hidden);     /* on the right monitor, still hidden */
        } else {
            GetWindowPlacement(a->hwnd, &before);
        }
        fullscreen_enter(a, &before);                 /* shows it */
    } else if (have_wp) {
        SetWindowPlacement(a->hwnd, &wp);             /* shows it (maximized if it was) */
    } else {
        ShowWindow(a->hwnd, minimized_start ? SW_SHOWMINNOACTIVE : show);
    }
    UpdateWindow(a->hwnd);                            /* the empty table appears right away */

    /* Decode the 52 faces (the slow part of the start-up) with the window on screen; input waits. */
    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    t0 = ce_now_ms();
    a->gfx = sol_gfx_new(ce_rcdata_loader, (void *)a->inst);
    ce_log("card set decode: %.1f ms", ce_now_ms() - t0);
    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW));
    if (!a->gfx) {
        fatal(a, L"Solitaire HD could not load its card images (the program file may be damaged, or there "
                 L"is not enough memory).");
        DestroyWindow(a->hwnd);
        ret = 1;
    } else {
        view_gfx_ready(a);
        if (!no_deal)
            PostMessageW(a->hwnd, WM_COMMAND, IDM_DEAL, 0);   /* XP deals at the end of its start-up */
        ret = ce_message_loop(&a->hwnd, a->accel);
    }

    sol_free(&a->s);
    view_free(a);
    sol_gfx_free(a->gfx);
    a->gfx = NULL;
    status_free(a);
    ce_log_close();
    return ret;
}
