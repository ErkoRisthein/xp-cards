/*
 * FreeCell HD — WinMain, the main window and its window procedure.
 *
 * The window procedure is a thin adapter from Win32 messages to the session (src/freecell/session.h):
 * after every session call it syncs the view (renders what changed) and refreshes the cursor.
 * Differences from XP's window (layout.md §1): freely resizable and maximizable (no 640-px limit,
 * minimum size from the layout), the board scales, the placement is remembered, and (v1.1) a
 * borderless full-screen mode (Game > Full Screen, F11 / Alt+Enter, Esc leaves) that keeps the menu bar.
 * Window geometry, placement and full screen are the engine's (engine/win32/window.h).
 */
#include "app.h"

#include <commctrl.h>
#include <string.h>

App g_app;

/* ---- window geometry ------------------------------------------------------------------------------ */

static void min_track(int *w, int *h)
{
    int cw, ch;
    fc_layout_min_client(&cw, &ch);
    ce_window_outer_size(cw, ch, w, h);
}

/* ---- full screen (extra) ------------------------------------------------------------------------- */

static void fullscreen_done(App *a)
{
    a->s.extras.full_screen = a->fs.on;
    if (a->menu)
        CheckMenuItem(a->menu, IDM_FULLSCREEN, MF_BYCOMMAND | (a->fs.on ? MF_CHECKED : MF_UNCHECKED));
    ce_menubar_reset(&a->mbt);
    DrawMenuBar(a->hwnd);
    menubar_draw(a);
    view_refresh_cursor(a);
}

/* Enter: remember the placement (prev, or the current one), drop the caption and the sizing frame
 * (the menu bar stays, so "Cards Left" and the extras stay visible) and cover the monitor. Leave: the
 * old style and exactly the old placement (normal or maximized). */
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

/* WM_CLOSE / WM_ENDSESSION: the placement (in full screen: the one to come back to) and the extras. */
static void save_window_state(App *a)
{
    ce_fullscreen_save_placement(&a->fs, a->hwnd, FC_APP_KEY);
    a->s.extras.full_screen = a->fs.on;
    fc_extras_save(&a->s.extras, &a->app_store);
}

/* ---- input ---------------------------------------------------------------------------------------- */

static int input_blocked(App *a) { return a->in_modal > 0 || !a->have_layout || !a->cs; }

static void after_input(App *a)
{
    view_anim_idle(a);                                /* the session call is over: no flight follows */
    view_sync(a);
    view_refresh_cursor(a);
    solver_deliver(a);                                /* a solver answer held meanwhile */
}

static void on_click(App *a, LPARAM lp, int dbl)
{
    int col, pos, hit;
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp), sel0 = a->s.sel, sw0 = a->s.swallow_click;
    if (input_blocked(a)) {
        ce_log("click %d,%d ignored (modal %d)", x, y, a->in_modal);
        return;
    }
    hit = view_hit(a, x, y, fcs_has_selection(&a->s) ? FC_HIT_DEST : FC_HIT_SOURCE, &col, &pos);
    if (!hit) {
        col = FCS_MISS;
        pos = -1;
    }
    if (dbl)
        fcs_dblclick(&a->s, col, pos);
    else
        fcs_click(&a->s, col, pos);
    ce_log("%s %d,%d -> col %d pos %d; sel %d -> %d%s", dbl ? "dblclick" : "click", x, y, col, pos, sel0,
           a->s.sel, sw0 ? " (swallowed: activation click)" : "");
    after_input(a);
}

static void on_command(App *a, int id)
{
    switch (id) {
    case IDM_NEWGAME: case IDM_SELECTGAME: case IDM_RESTART: case IDM_UNDO: case IDM_REDO: case IDM_CHEAT:
    case IDM_HINT: case IDM_FINISH:
        if (a->in_modal)
            return;
        fcs_command(&a->s, id);                       /* IDM_* == FCS_CMD_* */
        after_input(a);
        break;
    case IDM_STATISTICS: dlg_statistics(a); break;
    case IDM_OPTIONS: dlg_options(a); break;
    case IDM_FULLSCREEN:
        if (!a->in_modal && !a->s.busy)
            fullscreen_set(a, !a->fs.on);
        break;
    case IDM_EXIT: SendMessageW(a->hwnd, WM_CLOSE, 0, 0); break;
    case IDM_HELPCONTENTS: help_contents(a); break;
    case IDM_HELPSEARCH: help_search(a); break;
    case IDM_HELPHOWTO: help_howto(a); break;
    case IDM_ABOUT: help_about(a); break;
    }
}

/* ---- window procedure ------------------------------------------------------------------------------ */

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    App *a = &g_app;
    switch (m) {
    case WM_CREATE: {
        FcSessionUI ui;
        CeStore st = storage_store();
        CeBlobIO won = storage_won_io();
        a->hwnd = h;
        a->menu = GetMenu(h);
        ui_menu_init(a);
        ui_make(a, &ui);
        fcs_init(&a->s, &ui, &st);                    /* loads options, runs the entpack.ini migration */
        a->app_store = storage_app_store();
        fc_extras_load(&a->s.extras, &a->app_store);  /* the v1.1 extras, from our own key */
        if (fcs_attach_won_deals(&a->s, &won) < 0) {
            ce_log("won-deals.bin is damaged: set aside as won-deals.bad, starting empty");
            storage_won_set_aside();
        }
        ce_log("won deals: %u", (unsigned)fcs_won_count(&a->s));
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;                                     /* everything is painted from the back buffer */
    case WM_PAINT:
        view_paint(a);
        return 0;
    case WM_APP_SYNC:
        a->sync_posted = 0;
        if (a->dirty)
            view_sync(a);
        return 0;
    case WM_APP_SOLVED:
        solver_received(a, lp);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        int w, ht;
        min_track(&w, &ht);
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
        if (wp != SIZE_MINIMIZED && LOWORD(lp) > 0 && HIWORD(lp) > 0)
            view_resize(a, LOWORD(lp), HIWORD(lp));   /* 0 x 0 / minimized: keep the old buffer */
        ce_menubar_reset(&a->mbt);
        menubar_draw(a);
        clock_update(a);                              /* no "Time" ticks while minimized */
        return 0;
    case WM_MOVE:
        menubar_draw(a);
        return 0;
    case WM_DISPLAYCHANGE:
        ce_fullscreen_fit(&a->fs, h);                 /* full screen follows the new resolution */
        view_invalidate_all(a);
        ce_menubar_reset(&a->mbt);
        menubar_draw(a);
        break;
    case WM_SYSCOLORCHANGE:
    case WM_SETTINGCHANGE:
        ce_menubar_font_update(&a->mbt);
        DrawMenuBar(h);
        ce_menubar_reset(&a->mbt);
        menubar_draw(a);
        break;

    /* "Cards Left" lives in the non-client menu bar: redraw it after the system painted the frame */
    case WM_NCPAINT:
    case WM_NCACTIVATE:
    case WM_SETTEXT:
    case WM_EXITMENULOOP: {
        LRESULT r = DefWindowProcW(h, m, wp, lp);
        if (m == WM_NCPAINT)
            ce_menubar_reset(&a->mbt);
        menubar_draw(a);
        return r;
    }

    case WM_MOUSEACTIVATE:
        /* XP swallows the left click after any client-area activation (rules.md §4.6; 0x1001C3C
         * tests HTCLIENT only: a right- or middle-button activation arms it too) */
        if (LOWORD(lp) == HTCLIENT && !a->in_modal) {
            fcs_mouse_activate(&a->s);
            ce_log("WM_MOUSEACTIVATE: the next click is swallowed");
        }
        break;
    case WM_LBUTTONDOWN:
        on_click(a, lp, 0);
        return 0;
    case WM_LBUTTONDBLCLK:
        on_click(a, lp, 1);
        return 0;
    case WM_RBUTTONDOWN:
        if (!input_blocked(a)) {
            int col, pos;
            if (view_hit(a, (short)LOWORD(lp), (short)HIWORD(lp), FC_HIT_SOURCE, &col, &pos))
                fcs_rbutton_down(&a->s, col, pos);
            if (a->s.peek_col >= 0 && !a->s.kbd_peek && !a->rcapture) {
                a->rcapture = 1;                      /* get the button-up even outside the window */
                SetCapture(h);
            }
            after_input(a);
        }
        return 0;
    case WM_RBUTTONUP:
        if (a->rcapture) {
            a->rcapture = 0;
            ReleaseCapture();
        }
        fcs_rbutton_up(&a->s);
        after_input(a);
        return 0;
    case WM_CAPTURECHANGED:
        if (a->rcapture) {
            a->rcapture = 0;
            fcs_rbutton_up(&a->s);
            after_input(a);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (!input_blocked(a)) {
            view_mouse_move(a, (short)LOWORD(lp), (short)HIWORD(lp));
            if (a->dirty)
                view_sync(a);                         /* the king turned */
        }
        return 0;
    case WM_SETCURSOR:
        /* the client area shows the cursor we chose (XP: class cursor NULL + SetCursor on moves);
         * borders, caption and menu get the default handling */
        if ((HWND)wp == h && LOWORD(lp) == HTCLIENT) {
            SetCursor(a->cursor ? a->cursor : a->cur_arrow);
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && a->fs.on && !a->in_modal && !a->s.busy) {
            fullscreen_set(a, 0);                     /* Esc leaves full screen */
            return 0;
        }
        break;
    case WM_CHAR:
        if (!input_blocked(a)) {
            fcs_char(&a->s, (int)wp);
            after_input(a);
        }
        return 0;
    case WM_TIMER:
        if (wp == FC_TIMER_CLOCK) {                   /* "Time: m:ss" ticks */
            menubar_draw(a);
            clock_update(a);
            return 0;
        }
        if ((wp == FCS_TIMER_PEEK || wp == FCS_TIMER_HINT_WAIT) && a->in_modal)
            return 0;                                 /* the column peek / hint time limit wait for the dialog */
        fcs_timer(&a->s, (int)wp);
        after_input(a);
        return 0;
    case WM_COMMAND:
        on_command(a, LOWORD(wp));
        return 0;

    case WM_CLOSE:
        if (a->in_modal || a->s.busy)
            return 0;
        if (fcs_close(&a->s)) {                       /* resign prompt, loss, options saved */
            save_window_state(a);
            DestroyWindow(h);
        } else {
            after_input(a);
        }
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) {
            save_window_state(a);
            fc_options_save(&a->s.opts, &a->s.store);
        }
        return 0;
    case WM_DESTROY:
        view_anim_idle(a);
        KillTimer(h, FCS_TIMER_FLASH);
        KillTimer(h, FCS_TIMER_PEEK);
        KillTimer(h, FCS_TIMER_HINT);
        KillTimer(h, FCS_TIMER_HINT_WAIT);
        KillTimer(h, FC_TIMER_CLOCK);
        a->clock_timer = 0;
        solver_shutdown(a);
        help_shutdown(a);
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        a->hwnd = NULL;
        break;
    }
    return DefWindowProcW(h, m, wp, lp);
}

/* ---- WinMain -------------------------------------------------------------------------------------- */

static void fatal(App *a, const WCHAR *text)
{
    WCHAR cap[64];
    load_wstr(a, IDS_APPNAME, cap, 64, L"FreeCell");
    MessageBoxW(NULL, text, cap, MB_OK | MB_ICONHAND);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show)
{
    App *a = &g_app;
    WNDCLASSEXW wc;
    WINDOWPLACEMENT wp;
    RECT r;
    WCHAR title[64];
    int have_wp, failed, ret, minw, minh;
    double t0;

    a->inst = inst;
    InitCommonControls();                             /* activates comctl32 v6 (themed controls) */
    ce_log_open(L"FCHD_TIMING_LOG");                  /* optional timing log (file name) */

    view_init(a);

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_DBLCLKS;                            /* XP: CS_DBLCLKS only, hCursor NULL */
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_FREECELL));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_FREECELL), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszMenuName = RES_MENU;
    wc.lpszClassName = FC_CLASS_NAME;
    if (!RegisterClassExW(&wc)) {
        fatal(a, L"FreeCell HD could not register its window class.");
        view_free(a);
        return 1;
    }
    a->accel = LoadAcceleratorsW(inst, RES_MENU);

    ce_window_default_rect(&r, fc_layout_client_for_scale);
    min_track(&minw, &minh);
    have_wp = ce_placement_load(FC_APP_KEY, &wp) && ce_placement_fix(&wp, show, minw, minh, &r);
    load_wstr(a, IDS_APPNAME, title, 64, L"FreeCell");
    if (!CreateWindowExW(0, FC_CLASS_NAME, title, WS_OVERLAPPEDWINDOW, r.left, r.top, r.right - r.left,
                         r.bottom - r.top, NULL, NULL, inst, NULL)) {
        fatal(a, L"Out of memory.  Close other applications and try again.");
        view_free(a);
        return 1;
    }
    if (a->s.extras.full_screen && show != SW_SHOWMINIMIZED && show != SW_MINIMIZE &&
        show != SW_SHOWMINNOACTIVE) {
        /* left in full screen last time: go straight there; leaving it restores the saved placement */
        WINDOWPLACEMENT prev;
        memset(&prev, 0, sizeof prev);
        prev.length = sizeof prev;
        if (have_wp) {
            WINDOWPLACEMENT hidden = wp;
            prev = wp;
            hidden.showCmd = SW_HIDE;
            SetWindowPlacement(a->hwnd, &hidden);     /* on the right monitor, still hidden */
        } else {
            GetWindowPlacement(a->hwnd, &prev);
        }
        a->s.extras.full_screen = 0;
        fullscreen_enter(a, &prev);                   /* shows it */
    } else if (have_wp) {
        a->s.extras.full_screen = 0;
        SetWindowPlacement(a->hwnd, &wp);             /* shows it (maximized if it was) */
    } else {
        a->s.extras.full_screen = 0;
        ShowWindow(a->hwnd, show);
    }
    UpdateWindow(a->hwnd);                            /* the empty table appears right away */

    /* Decode the 52 card faces and the kings (the slow part of the start-up) with the window on
     * screen; input waits until they are ready (no game is dealt at start-up anyway). */
    a->cursor = a->cur_wait;
    SetCursor(a->cursor);
    t0 = ce_now_ms();
    a->cs = fc_cardset_new(ce_rcdata_loader, (void *)a->inst);
    ce_log("card set decode: %.1f ms", ce_now_ms() - t0);
    a->cursor = a->cur_arrow;
    failed = !a->cs;
    if (failed) {
        WCHAR cap[64];
        load_wstr(a, IDS_APPNAME, cap, 64, L"FreeCell");
        MessageBoxW(a->hwnd, L"FreeCell HD could not load its card images (the program file may be "
                    L"damaged, or there is not enough memory).", cap, MB_OK | MB_ICONHAND);
        DestroyWindow(a->hwnd);                       /* no game: WM_CLOSE's prompt is not needed */
    } else {
        view_cardset_ready(a);
        view_refresh_cursor(a);
    }

    ret = ce_message_loop(&a->hwnd, a->accel);

    fcs_free(&a->s);
    view_free(a);
    fc_cardset_free(a->cs);
    a->cs = NULL;
    ce_log_close();
    return failed ? 1 : ret;
}
