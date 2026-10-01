/*
 * FreeCell HD — WinMain, the main window and its window procedure.
 *
 * The window procedure is a thin adapter from Win32 messages to the session (src/core/session.h):
 * after every session call it syncs the view (renders what changed) and refreshes the cursor.
 * Differences from XP's window (layout.md §1): freely resizable and maximizable (no 640-px limit,
 * minimum size from the layout), the board scales, the placement is remembered, and (v1.1) a
 * borderless full-screen mode (Game > Full Screen, F11 / Alt+Enter, Esc leaves) that keeps the menu bar.
 */
#include "app.h"

#include <commctrl.h>
#include <string.h>

App g_app;

/* ---- assets: card / king PNGs are RCDATA resources (ids = FC_ASSET_*) ---------------------------- */

static const void *res_loader(int id, size_t *len, void *ctx)
{
    App *a = ctx;
    HRSRC r = FindResourceW(a->inst, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    HGLOBAL g;
    if (!r || !(g = LoadResource(a->inst, r)))
        return NULL;
    *len = SizeofResource(a->inst, r);
    return LockResource(g);
}

/* ---- window geometry ------------------------------------------------------------------------------ */

static void outer_size(int cw, int ch, int *w, int *h)
{
    RECT r = { 0, 0, cw, ch };
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, TRUE, 0);
    *w = r.right - r.left;
    *h = r.bottom - r.top;
}

static void primary_work_area(RECT *w)
{
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, w, 0)) {
        w->left = w->top = 0;
        w->right = GetSystemMetrics(SM_CXSCREEN);
        w->bottom = GetSystemMetrics(SM_CYSCREEN);
    }
}

/* First run: centred on the primary work area, client of XP's proportions at the largest scale whose
 * window fits ~85% of the work area height (and 95% of its width). */
static void default_rect(RECT *out)
{
    RECT wk;
    int ww, wh, cw = 632, ch = 427, w = 640, h = 480;
    double s;
    primary_work_area(&wk);
    ww = wk.right - wk.left;
    wh = wk.bottom - wk.top;
    for (s = 8.0; s >= 0.5; s -= 0.01) {
        fc_layout_client_for_scale(s, &cw, &ch);
        outer_size(cw, ch, &w, &h);
        if (h <= wh * 0.85 && w <= ww * 0.95)
            break;
    }
    out->left = wk.left + (ww - w) / 2;
    out->top = wk.top + (wh - h) / 2;
    if (out->left < wk.left) out->left = wk.left;
    if (out->top < wk.top) out->top = wk.top;
    out->right = out->left + w;
    out->bottom = out->top + h;
}

static void min_track(int *w, int *h)
{
    int cw, ch;
    fc_layout_min_client(&cw, &ch);
    outer_size(cw, ch, w, h);
}

/* Validate a saved placement: its normal rect must lie on a monitor (it is shrunk to and moved into
 * that monitor's work area). WINDOWPLACEMENT uses workspace coordinates (relative to the primary
 * monitor's work area). Fills *screen with the normal rect in screen coordinates. */
static int fix_placement(WINDOWPLACEMENT *wp, int show, RECT *screen)
{
    POINT o = { 0, 0 };
    MONITORINFO pmi, mi;
    HMONITOR pm, m;
    RECT r = wp->rcNormalPosition, wk;
    int w = r.right - r.left, h = r.bottom - r.top, minw, minh, ox, oy, maxed;
    min_track(&minw, &minh);
    if (w < minw || h < minh || w > 30000 || h > 30000)
        return 0;
    memset(&pmi, 0, sizeof pmi);
    pmi.cbSize = sizeof pmi;
    pm = MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY);
    if (!pm || !GetMonitorInfoW(pm, &pmi))
        return 0;
    ox = pmi.rcWork.left - pmi.rcMonitor.left;
    oy = pmi.rcWork.top - pmi.rcMonitor.top;
    OffsetRect(&r, ox, oy);
    m = MonitorFromRect(&r, MONITOR_DEFAULTTONULL);
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    if (!m || !GetMonitorInfoW(m, &mi))
        return 0;
    wk = mi.rcWork;
    if (w > wk.right - wk.left) w = wk.right - wk.left;
    if (h > wk.bottom - wk.top) h = wk.bottom - wk.top;
    if (w < minw || h < minh)
        return 0;                                     /* the monitor is smaller than our minimum */
    r.right = r.left + w;
    r.bottom = r.top + h;
    if (r.right > wk.right) OffsetRect(&r, wk.right - r.right, 0);
    if (r.bottom > wk.bottom) OffsetRect(&r, 0, wk.bottom - r.bottom);
    if (r.left < wk.left) OffsetRect(&r, wk.left - r.left, 0);
    if (r.top < wk.top) OffsetRect(&r, 0, wk.top - r.top);
    *screen = r;
    OffsetRect(&r, -ox, -oy);
    wp->rcNormalPosition = r;
    maxed = wp->showCmd == SW_SHOWMAXIMIZED ||
            (wp->showCmd == SW_SHOWMINIMIZED && (wp->flags & WPF_RESTORETOMAXIMIZED));
    wp->flags = 0;
    wp->ptMinPosition.x = wp->ptMinPosition.y = -1;
    wp->ptMaxPosition.x = wp->ptMaxPosition.y = -1;
    if (show == SW_SHOWMINIMIZED || show == SW_MINIMIZE || show == SW_SHOWMINNOACTIVE) {
        wp->showCmd = (UINT)show;                     /* started minimized: honour it */
        if (maxed)
            wp->flags = WPF_RESTORETOMAXIMIZED;
    } else {
        wp->showCmd = maxed || show == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    }
    return 1;
}

/* ---- full screen (extra) ------------------------------------------------------------------------- */

static int monitor_rect(HWND h, RECT *r)
{
    MONITORINFO mi;
    HMONITOR m = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    if (!m || !GetMonitorInfoW(m, &mi))
        return 0;
    *r = mi.rcMonitor;
    return 1;
}

/* Cover the whole monitor the window is on (the taskbar included). */
static void fullscreen_fit(App *a)
{
    RECT r;
    if (a->fullscreen && monitor_rect(a->hwnd, &r))
        SetWindowPos(a->hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

static void fullscreen_done(App *a)
{
    a->s.extras.full_screen = a->fullscreen;
    if (a->menu)
        CheckMenuItem(a->menu, IDM_FULLSCREEN, MF_BYCOMMAND | (a->fullscreen ? MF_CHECKED : MF_UNCHECKED));
    menubar_reset(a);
    DrawMenuBar(a->hwnd);
    menubar_draw(a);
    view_refresh_cursor(a);
}

/* Enter: remember the placement (prev, or the current one) and the style, drop the caption and the
 * sizing frame (the menu bar stays, so "Cards Left" and the extras stay visible) and cover the
 * monitor. Leave: the old style and exactly the old placement (normal or maximized). */
static void fullscreen_enter(App *a, const WINDOWPLACEMENT *prev)
{
    WINDOWPLACEMENT wp;
    RECT r;
    HWND h = a->hwnd;
    if (!h || a->fullscreen || IsIconic(h))
        return;
    memset(&wp, 0, sizeof wp);
    wp.length = sizeof wp;
    if (prev)
        wp = *prev;
    else if (!GetWindowPlacement(h, &wp))
        return;
    if (wp.showCmd != SW_SHOWMAXIMIZED)
        wp.showCmd = SW_SHOWNORMAL;
    wp.flags = 0;
    if (!monitor_rect(h, &r))
        return;
    a->fs_prev = wp;
    a->fs_style = GetWindowLongW(h, GWL_STYLE);
    a->fullscreen = 1;
    SetWindowLongW(h, GWL_STYLE, a->fs_style & ~(WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MAXIMIZE));
    fullscreen_fit(a);
    tlog(a, "full screen on: monitor (%ld,%ld)-(%ld,%ld)", r.left, r.top, r.right, r.bottom);
    fullscreen_done(a);
}

static void fullscreen_leave(App *a)
{
    HWND h = a->hwnd;
    LONG vis;
    if (!h || !a->fullscreen)
        return;
    a->fullscreen = 0;
    vis = GetWindowLongW(h, GWL_STYLE) & WS_VISIBLE;
    SetWindowLongW(h, GWL_STYLE, (a->fs_style & ~(WS_VISIBLE | WS_MAXIMIZE | WS_MINIMIZE)) | vis);
    SetWindowPlacement(h, &a->fs_prev);
    SetWindowPos(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                 SWP_FRAMECHANGED);
    tlog(a, "full screen off");
    fullscreen_done(a);
}

void fullscreen_set(App *a, int on)
{
    if (on)
        fullscreen_enter(a, NULL);
    else
        fullscreen_leave(a);
}

/* WM_CLOSE / WM_ENDSESSION: the placement (in full screen: the one to come back to) and the extras. */
static void save_window_state(App *a)
{
    if (a->fullscreen)
        placement_save_wp(&a->fs_prev);
    else
        placement_save(a->hwnd);
    a->s.extras.full_screen = a->fullscreen;
    fc_extras_save(&a->s.extras, &a->app_store);
}

/* ---- input ---------------------------------------------------------------------------------------- */

static int input_blocked(App *a) { return a->in_modal > 0 || !a->have_layout || !a->cs; }

static void after_input(App *a)
{
    view_anim_idle(a);                                /* the session call is over: no flight follows */
    view_sync(a);
    view_refresh_cursor(a);
}

static void on_click(App *a, LPARAM lp, int dbl)
{
    int col, pos, hit;
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp), sel0 = a->s.sel, sw0 = a->s.swallow_click;
    if (input_blocked(a)) {
        tlog(a, "click %d,%d ignored (modal %d)", x, y, a->in_modal);
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
    tlog(a, "%s %d,%d -> col %d pos %d; sel %d -> %d%s", dbl ? "dblclick" : "click", x, y, col, pos, sel0,
         a->s.sel, sw0 ? " (swallowed: activation click)" : "");
    after_input(a);
}

static void on_command(App *a, int id)
{
    switch (id) {
    case IDM_NEWGAME: case IDM_SELECTGAME: case IDM_RESTART: case IDM_UNDO: case IDM_REDO: case IDM_CHEAT:
        if (a->in_modal)
            return;
        fcs_command(&a->s, id);                       /* IDM_* == FCS_CMD_* */
        after_input(a);
        break;
    case IDM_STATISTICS: dlg_statistics(a); break;
    case IDM_OPTIONS: dlg_options(a); break;
    case IDM_FULLSCREEN:
        if (!a->in_modal && !a->s.busy)
            fullscreen_set(a, !a->fullscreen);
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
        FcStore st = storage_store();
        FcBlobIO won = storage_won_io();
        a->hwnd = h;
        a->menu = GetMenu(h);
        ui_menu_init(a);
        ui_make(a, &ui);
        fcs_init(&a->s, &ui, &st);                    /* loads options, runs the entpack.ini migration */
        a->app_store = storage_app_store();
        fc_extras_load(&a->s.extras, &a->app_store);  /* the v1.1 extras, from our own key */
        if (fcs_attach_won_deals(&a->s, &won) < 0) {
            tlog(a, "won-deals.bin is damaged: set aside as won-deals.bad, starting empty");
            storage_won_set_aside();
        }
        tlog(a, "won deals: %u", (unsigned)fcs_won_count(&a->s));
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

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        int w, ht;
        min_track(&w, &ht);
        mm->ptMinTrackSize.x = w;
        mm->ptMinTrackSize.y = ht;
        outer_size(1920, 1200, &w, &ht);              /* allow a full-HD client even on a smaller screen */
        if (mm->ptMaxTrackSize.x < w) mm->ptMaxTrackSize.x = w;
        if (mm->ptMaxTrackSize.y < ht) mm->ptMaxTrackSize.y = ht;
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        a->in_sizemove = 1;
        a->sized_in_loop = 0;
        return 0;
    case WM_EXITSIZEMOVE:
        view_exit_sizemove(a);
        return 0;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED && LOWORD(lp) > 0 && HIWORD(lp) > 0)
            view_resize(a, LOWORD(lp), HIWORD(lp));   /* 0 x 0 / minimized: keep the old buffer */
        menubar_reset(a);
        menubar_draw(a);
        clock_update(a);                              /* no "Time" ticks while minimized */
        return 0;
    case WM_MOVE:
        menubar_draw(a);
        return 0;
    case WM_DISPLAYCHANGE:
        fullscreen_fit(a);                            /* full screen follows the new resolution */
        view_invalidate_all(a);
        menubar_reset(a);
        menubar_draw(a);
        break;
    case WM_SYSCOLORCHANGE:
    case WM_SETTINGCHANGE:
        menubar_font_update(a);
        DrawMenuBar(h);
        menubar_reset(a);
        menubar_draw(a);
        break;

    /* "Cards Left" lives in the non-client menu bar: redraw it after the system painted the frame */
    case WM_NCPAINT:
    case WM_NCACTIVATE:
    case WM_SETTEXT:
    case WM_EXITMENULOOP: {
        LRESULT r = DefWindowProcW(h, m, wp, lp);
        if (m == WM_NCPAINT)
            menubar_reset(a);
        menubar_draw(a);
        return r;
    }

    case WM_MOUSEACTIVATE:
        /* XP swallows the left click after any client-area activation (rules.md §4.6; 0x1001C3C
         * tests HTCLIENT only: a right- or middle-button activation arms it too) */
        if (LOWORD(lp) == HTCLIENT && !a->in_modal) {
            fcs_mouse_activate(&a->s);
            tlog(a, "WM_MOUSEACTIVATE: the next click is swallowed");
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
        if (wp == VK_ESCAPE && a->fullscreen && !a->in_modal && !a->s.busy) {
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
        if (wp == FCS_TIMER_PEEK && a->in_modal)
            return 0;                                 /* the column peek waits for the dialog */
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
        KillTimer(h, FC_TIMER_CLOCK);
        a->clock_timer = 0;
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
    MSG msg;
    WCHAR title[64], path[MAX_PATH];
    int have_wp, failed;
    double t0;

    a->inst = inst;
    InitCommonControls();                             /* activates comctl32 v6 (themed controls) */
    QueryPerformanceFrequency(&a->qpf);
    if (GetEnvironmentVariableW(L"FCHD_TIMING_LOG", path, MAX_PATH) - 1u < MAX_PATH - 1u)
        a->tlog = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);

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

    default_rect(&r);
    have_wp = placement_load(&wp) && fix_placement(&wp, show, &r);
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
    t0 = now_ms(a);
    a->cs = fc_cardset_new(res_loader, a);
    tlog(a, "card set decode: %.1f ms", now_ms(a) - t0);
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

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (a->hwnd && a->accel && (msg.hwnd == a->hwnd || IsChild(a->hwnd, msg.hwnd)) &&
            TranslateAcceleratorW(a->hwnd, a->accel, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    fcs_free(&a->s);
    view_free(a);
    fc_cardset_free(a->cs);
    a->cs = NULL;
    if (a->tlog && a->tlog != INVALID_HANDLE_VALUE)
        CloseHandle(a->tlog);
    return failed ? 1 : (int)msg.wParam;
}
