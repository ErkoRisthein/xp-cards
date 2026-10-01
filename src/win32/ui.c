/*
 * FreeCell HD — the session's UI callbacks (session.h FcSessionUI) and the dialogs (resources.md §5,
 * rules.md §2.5, §5, §6, §8, §9).
 *
 * Every modal prompt first brings the window up to date (view_sync_now) and counts itself in
 * a->in_modal, so input and commands that arrive through the modal loop are ignored meanwhile.
 * Dialogs are centred over the main window's client area like XP's helper 0x1002411 (and kept on the
 * monitor's work area); the YouWin dialog sits to the right of the big win king.
 */
#include "app.h"

#include <string.h>

/* ---- small helpers -------------------------------------------------------------------------------- */

int to_wide(const char *s, WCHAR *out, int n)
{
    int k = MultiByteToWideChar(CP_UTF8, 0, s ? s : "", -1, out, n);
    if (k <= 0) {
        if (n > 0)
            out[0] = 0;
        return 0;
    }
    return k - 1;
}

int load_wstr(App *a, UINT id, WCHAR *out, int n, const WCHAR *fallback)
{
    int k = LoadStringW(a->inst, id, out, n);
    if (k <= 0) {
        lstrcpynW(out, fallback, n);
        k = lstrlenW(out);
    }
    return k;
}

static void app_name(App *a, WCHAR *out, int n) { load_wstr(a, IDS_APPNAME, out, n, L"FreeCell"); }

void clamp_to_work_area(HWND near_wnd, RECT *r)
{
    MONITORINFO mi;
    RECT w;
    HMONITOR m = MonitorFromRect(r, MONITOR_DEFAULTTONULL);
    if (!m)
        m = MonitorFromWindow(near_wnd, MONITOR_DEFAULTTONEAREST);
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    if (m && GetMonitorInfoW(m, &mi))
        w = mi.rcWork;
    else if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &w, 0))
        return;
    if (r->right > w.right) OffsetRect(r, w.right - r->right, 0);
    if (r->bottom > w.bottom) OffsetRect(r, 0, w.bottom - r->bottom);
    if (r->left < w.left) OffsetRect(r, w.left - r->left, 0);
    if (r->top < w.top) OffsetRect(r, 0, w.top - r->top);
}

static void move_dialog(HWND dlg, RECT r, HWND owner, const char *what)
{
    POINT o = { 0, 0 };
    clamp_to_work_area(owner, &r);
    SetWindowPos(dlg, NULL, r.left, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (owner)
        ClientToScreen(owner, &o);
    tlog(&g_app, "%s dialog at %ld,%ld size %ldx%ld (client origin %ld,%ld)", what, r.left, r.top,
         r.right - r.left, r.bottom - r.top, o.x, o.y);
}

/* XP (0x1002411): centre the dialog over the main window's client area. */
void center_dialog(HWND dlg, HWND owner)
{
    RECT cr, dr, r;
    int w, h;
    GetWindowRect(dlg, &dr);
    w = dr.right - dr.left;
    h = dr.bottom - dr.top;
    if (!owner || IsIconic(owner) || !GetClientRect(owner, &cr) || cr.right <= 0 || cr.bottom <= 0) {
        HMONITOR m = MonitorFromWindow(owner ? owner : dlg, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi;
        memset(&mi, 0, sizeof mi);
        mi.cbSize = sizeof mi;
        if (!GetMonitorInfoW(m, &mi))
            return;
        cr = mi.rcWork;
    } else {
        MapWindowPoints(owner, NULL, (POINT *)&cr, 2);
    }
    r.left = cr.left + (cr.right - cr.left - w) / 2;
    r.top = cr.top + (cr.bottom - cr.top - h) / 2;
    r.right = r.left + w;
    r.bottom = r.top + h;
    move_dialog(dlg, r, owner, "centred");
}

void modal_begin(App *a) { a->in_modal++; }

/* A solver answer that arrived while the dialog was up is retried from the main message loop (the
 * dialog may have been opened from inside a session call that is still running). */
void modal_end(App *a)
{
    if (--a->in_modal == 0 && a->solve_ready && a->hwnd)
        PostMessageW(a->hwnd, WM_APP_SOLVED, 0, 0);
}

static int msgbox(App *a, const WCHAR *text, const WCHAR *caption, UINT type, UINT beep)
{
    int r;
    view_anim_idle(a);
    view_sync_now(a);
    modal_begin(a);
    if (beep != (UINT)-1)
        MessageBeep(beep);
    r = MessageBoxW(a->hwnd, text, caption, type);
    modal_end(a);
    return r;
}

static INT_PTR run_dialog(App *a, const WCHAR *name, DLGPROC proc, LPARAM lp)
{
    INT_PTR r;
    view_anim_idle(a);
    view_sync_now(a);
    modal_begin(a);
    r = DialogBoxParamW(a->inst, name, a->hwnd, proc, lp);
    modal_end(a);
    return r;
}

static int checked(HWND d, int id) { return IsDlgButtonChecked(d, id) == BST_CHECKED; }
static void set_check(HWND d, int id, int on) { CheckDlgButton(d, id, on ? BST_CHECKED : BST_UNCHECKED); }

/* ---- dialog procedures ------------------------------------------------------------------------------- */

typedef struct GameNumParam { int initial, value; } GameNumParam;

/* Extra: "You have won this game before." under the number box while it holds a won deal. */
static void game_num_won_line(HWND d)
{
    BOOL ok = FALSE;
    WCHAR text[96] = L"";
    int v = (int)GetDlgItemInt(d, IDC_GAMENUMBER, &ok, TRUE);
    if (ok && fcs_won_before(&g_app.s, v))
        load_wstr(&g_app, IDS_WONBEFORE, text, 96, L"You have won this game before.");
    SetDlgItemTextW(d, IDC_WONBEFORE, text);
}

/* GameNum (0x10024D4): pre-filled number (all selected), signed read on OK; the session validates. */
static INT_PTR CALLBACK game_num_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
    GameNumParam *p = (GameNumParam *)GetWindowLongPtrW(d, DWLP_USER);
    switch (m) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(d, DWLP_USER, lp);
        p = (GameNumParam *)lp;
        center_dialog(d, g_app.hwnd);
        SetDlgItemInt(d, IDC_GAMENUMBER, (UINT)p->initial, p->initial < 0);
        game_num_won_line(d);
        return TRUE;                                  /* focus the edit; the dialog selects its text */
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_GAMENUMBER:
            if (HIWORD(wp) == EN_CHANGE)
                game_num_won_line(d);
            return TRUE;
        case IDOK: {
            BOOL ok = FALSE;
            int v = (int)GetDlgItemInt(d, IDC_GAMENUMBER, &ok, TRUE);
            p->value = ok ? v : 0;
            EndDialog(d, 1);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(d, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* MoveCol (0x100248A): 1 = Move column, 0 = Move single card, 2 = Cancel. */
static INT_PTR CALLBACK move_col_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_INITDIALOG:
        center_dialog(d, g_app.hwnd);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_MOVECOLUMN: EndDialog(d, 1); return TRUE;
        case IDC_MOVESINGLE: EndDialog(d, 0); return TRUE;
        case IDCANCEL: EndDialog(d, 2); return TRUE;
        }
        break;
    }
    return FALSE;
}

typedef struct CheckParam { int check; int checkbox; } CheckParam;

/* YouWin: not centred (XP uses the template position, right of the big king): put it to the right of
 * the big win king, vertically centred on it, on the screen. */
static void place_you_win(HWND d)
{
    App *a = &g_app;
    RECT dr, r;
    POINT p;
    int w, h, gap;
    if (!a->have_layout || !a->hwnd || IsIconic(a->hwnd)) {
        center_dialog(d, a->hwnd);
        return;
    }
    GetWindowRect(d, &dr);
    w = dr.right - dr.left;
    h = dr.bottom - dr.top;
    gap = (int)(10 * a->L.s + 0.5);
    if (gap < 8)
        gap = 8;
    p.x = a->L.big_king.x + a->L.big_king.w + gap;
    p.y = a->L.big_king.y + (a->L.big_king.h - h) / 2;
    ClientToScreen(a->hwnd, &p);
    r.left = p.x;
    r.top = p.y;
    r.right = r.left + w;
    r.bottom = r.top + h;
    move_dialog(d, r, a->hwnd, "youwin");
}

/* YouWin / YouLose: Yes = 1, No / Esc = 0, the checkbox state is returned through the param. */
static INT_PTR CALLBACK game_over_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
    CheckParam *p = (CheckParam *)GetWindowLongPtrW(d, DWLP_USER);
    switch (m) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(d, DWLP_USER, lp);
        p = (CheckParam *)lp;
        set_check(d, p->checkbox, p->check);
        if (p->checkbox == IDC_SELECTGAME)
            place_you_win(d);
        else
            center_dialog(d, g_app.hwnd);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDYES: case IDOK:
            p->check = checked(d, p->checkbox);
            EndDialog(d, 1);
            return TRUE;
        case IDNO: case IDCANCEL:
            p->check = checked(d, p->checkbox);
            EndDialog(d, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void stats_fill(HWND d)
{
    FcStatsView v;
    char buf[256];
    WCHAR w[256], fmt[64];
    fc_stats_view(&g_app.s.stats, &v);
    fc_stats_format_session(&v, buf, sizeof buf);
    to_wide(buf, w, 256);
    SetDlgItemTextW(d, IDC_STATS_SESSION, w);         /* static controls expand the tabs, as XP */
    fc_stats_format_total(&v, buf, sizeof buf);
    to_wide(buf, w, 256);
    SetDlgItemTextW(d, IDC_STATS_TOTAL, w);
    fc_stats_format_streaks(&v, buf, sizeof buf);
    to_wide(buf, w, 256);
    SetDlgItemTextW(d, IDC_STATS_STREAKS, w);
    load_wstr(&g_app, IDS_WONDEALS, fmt, 64, L"Different games won: %u");   /* extra */
    wsprintfW(w, fmt, (unsigned)fcs_won_count(&g_app.s));
    SetDlgItemTextW(d, IDC_STATS_WONDEALS, w);
}

/* Stats (0x1002671): Clear asks 309; Yes clears and closes the dialog. */
static INT_PTR CALLBACK stats_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_INITDIALOG:
        center_dialog(d, g_app.hwnd);
        stats_fill(d);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK: case IDCANCEL:
            EndDialog(d, 0);
            return TRUE;
        case IDC_CLEARSTATS: {
            WCHAR text[160], cap[64];
            load_wstr(&g_app, IDS_CLEARSTATS, text, 160, L"Are you sure you want to delete all statistics?");
            app_name(&g_app, cap, 64);
            MessageBeep(MB_ICONQUESTION);
            if (MessageBoxW(d, text, cap, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                fc_stats_clear(&g_app.s.stats);
                EndDialog(d, 0);
            }
            return TRUE;
        }
        }
        break;
    }
    return FALSE;
}

/* Options (505, 0x10029F1): OK applies to memory; XP's three are saved on exit by fcs_close (XP), the
 * extras (the "Extras" group, v1.1 and v1.2) right away in our own key. */
static INT_PTR CALLBACK options_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
    FcOptions *o = &g_app.s.opts;
    FcExtras *x = &g_app.s.extras;
    switch (m) {
    case WM_INITDIALOG:
        center_dialog(d, g_app.hwnd);
        set_check(d, IDC_MESSAGES, o->messages);
        set_check(d, IDC_QUICKPLAY, o->quick);
        set_check(d, IDC_DBLCLICK, o->dblclick);
        set_check(d, IDC_SHOWTIME, x->show_time_moves);
        set_check(d, IDC_STDSUPERMOVE, x->standard_supermove);
        set_check(d, IDC_FULLRANGE, x->full_range);
        set_check(d, IDC_WARNUNWINNABLE, x->warn_unwinnable);
        set_check(d, IDC_AUTOFINISH, x->auto_finish);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:
            o->messages = checked(d, IDC_MESSAGES);
            o->quick = checked(d, IDC_QUICKPLAY);
            o->dblclick = checked(d, IDC_DBLCLICK);
            x->show_time_moves = checked(d, IDC_SHOWTIME);
            x->standard_supermove = checked(d, IDC_STDSUPERMOVE);
            x->full_range = checked(d, IDC_FULLRANGE);
            x->warn_unwinnable = checked(d, IDC_WARNUNWINNABLE);
            x->auto_finish = checked(d, IDC_AUTOFINISH);
            fc_extras_save(x, &g_app.app_store);
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

static int dialogs_allowed(App *a) { return !a->in_modal && !a->s.busy && !a->s.kbd_peek; }

void dlg_statistics(App *a)
{
    if (dialogs_allowed(a))
        run_dialog(a, RES_DLG_STATS, stats_proc, 0);
    solver_deliver(a);
}

void dlg_options(App *a)
{
    if (dialogs_allowed(a) && run_dialog(a, MAKEINTRESOURCEW(IDD_OPTIONS), options_proc, 0) == 1) {
        fcs_options_changed(&a->s);                   /* a search for the old rule is redone */
        DrawMenuBar(a->hwnd);                         /* "Moves" / "Time" shown or hidden */
        menubar_reset(a);
        menubar_draw(a);
        clock_update(a);
        view_sync(a);
        view_refresh_cursor(a);                       /* the supermove rule changes the cursor */
    }
    solver_deliver(a);
}

/* ---- session callbacks ------------------------------------------------------------------------------- */

static void cb_message(void *ctx, int id, const char *text)
{
    App *a = ctx;
    WCHAR w[512], cap[64];
    to_wide(text, w, 512);
    app_name(a, cap, 64);
    msgbox(a, w, cap, MB_OK | MB_ICONINFORMATION, MB_ICONINFORMATION);
}

static int cb_confirm_resign(void *ctx)
{
    App *a = ctx;
    WCHAR text[128], cap[64];
    load_wstr(a, IDS_RESIGN, text, 128, L"Do you want to resign this game?");
    app_name(a, cap, 64);
    return msgbox(a, text, cap, MB_YESNO | MB_ICONQUESTION, MB_ICONQUESTION) == IDYES;
}

static int cb_ask_move_column(void *ctx)
{
    INT_PTR r = run_dialog(ctx, RES_DLG_MOVECOL, move_col_proc, 0);
    return r == 1 ? FCS_MOVECOL_COLUMN : r == 0 ? FCS_MOVECOL_SINGLE : FCS_MOVECOL_CANCEL;
}

static int cb_ask_game_number(void *ctx, int initial, int *value)
{
    GameNumParam p;
    p.initial = initial;
    p.value = 0;
    if (run_dialog(ctx, RES_DLG_GAMENUM, game_num_proc, (LPARAM)&p) != 1)
        return 0;
    *value = p.value;
    return 1;
}

static int cb_you_win(void *ctx, int *select_game)
{
    CheckParam p;
    INT_PTR r;
    p.check = *select_game;
    p.checkbox = IDC_SELECTGAME;
    r = run_dialog(ctx, RES_DLG_YOUWIN, game_over_proc, (LPARAM)&p);   /* paints the big king first */
    *select_game = p.check;
    return r == 1;
}

static int cb_you_lose(void *ctx, int *same_game)
{
    CheckParam p;
    INT_PTR r;
    p.check = *same_game;
    p.checkbox = IDC_SAMEGAME;
    r = run_dialog(ctx, RES_DLG_YOULOSE, game_over_proc, (LPARAM)&p);
    *same_game = p.check;
    return r == 1;
}

static int cb_cheat_prompt(void *ctx)
{
    App *a = ctx;
    WCHAR text[160], cap[96];
    int r;
    to_wide(fcs_string(FCS_STR_CHEAT_TEXT), text, 160);
    to_wide(fcs_string(FCS_STR_CHEAT_CAPTION), cap, 96);
    r = msgbox(a, text, cap, MB_ABORTRETRYIGNORE | MB_ICONQUESTION, (UINT)-1);
    return r == IDABORT ? FCS_CHEAT_WIN : r == IDRETRY ? FCS_CHEAT_LOSE : FCS_CHEAT_NONE;
}

static void cb_animate_step(void *ctx, const FcStep *st, int forward)
{
    view_animate_step(ctx, st, forward);
}

static void cb_invalidate(void *ctx)
{
    App *a = ctx;
    a->dirty = 1;
    if (!a->sync_posted && a->hwnd) {                 /* safety net; normally synced right away */
        a->sync_posted = 1;
        PostMessageW(a->hwnd, WM_APP_SYNC, 0, 0);
    }
}

static void cb_set_title(void *ctx, const char *title)
{
    App *a = ctx;
    WCHAR w[128];
    to_wide(title, w, 128);
    SetWindowTextW(a->hwnd, w);
    menubar_draw(a);
}

static void cb_cards_left_changed(void *ctx, int n)
{
    App *a = ctx;
    a->cards_left = n;
    menubar_draw(a);                                  /* immediately: it counts down during a replay */
}

static int set_item(App *a, int id, int on, int *cache)
{
    if (*cache == on)
        return 0;
    *cache = on;
    EnableMenuItem(a->menu, (UINT)id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
    return 1;
}

static void cb_menu_state(void *ctx, int undo_enabled, int restart_enabled, int redo_enabled)
{
    App *a = ctx;
    int changed = 0;
    if (!a->menu)
        return;
    changed |= set_item(a, IDM_UNDO, undo_enabled != 0, &a->menu_undo);
    changed |= set_item(a, IDM_REDO, redo_enabled != 0, &a->menu_redo);
    changed |= set_item(a, IDM_RESTART, restart_enabled != 0, &a->menu_restart);
    if (changed) {
        DrawMenuBar(a->hwnd);
        menubar_draw(a);
    }
}

static void cb_set_timer(void *ctx, int id, int ms)
{
    App *a = ctx;
    if (ms > 0)
        SetTimer(a->hwnd, (UINT_PTR)id, (UINT)ms, NULL);
    else
        KillTimer(a->hwnd, (UINT_PTR)id);
}

static void cb_flash(void *ctx, int invert) { FlashWindow(((App *)ctx)->hwnd, invert ? TRUE : FALSE); }

static void cb_post_command(void *ctx, int cmd)
{
    PostMessageW(((App *)ctx)->hwnd, WM_COMMAND, MAKEWPARAM((WORD)cmd, 0), 0);
}

/* Seed for RandomGameNumber: Unix time (as XP's time(NULL)) mixed with the millisecond tick. */
static uint32_t cb_now_seed(void *ctx)
{
    FILETIME ft;
    uint64_t t;
    GetSystemTimeAsFileTime(&ft);
    t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    t = t / 10000000u - 11644473600u;
    return (uint32_t)t * 1000u + GetTickCount() % 1000u;
}

/* Extras: the game clock's time base, and "moves / clock changed". */
static uint32_t cb_now_ms(void *ctx) { return GetTickCount(); }

static void cb_status_changed(void *ctx)
{
    App *a = ctx;
    menubar_draw(a);
    clock_update(a);
}

/* v1.2: the background solver (solve.c) and the Hint / Finish menu items. */
static void cb_solve_start(void *ctx, uint32_t id, const FcBoard *b, int standard)
{
    solver_request(ctx, id, b, standard);
}

static void cb_solve_cancel(void *ctx) { solver_cancel(ctx); }

static void cb_assist_menu(void *ctx, int hint_enabled, int finish_enabled)
{
    App *a = ctx;
    if (!a->menu)
        return;
    set_item(a, IDM_HINT, hint_enabled != 0, &a->menu_hint);       /* popup items: no DrawMenuBar */
    set_item(a, IDM_FINISH, finish_enabled != 0, &a->menu_finish);
}

void ui_make(App *a, FcSessionUI *ui)
{
    memset(ui, 0, sizeof *ui);
    ui->ctx = a;
    ui->message = cb_message;
    ui->confirm_resign = cb_confirm_resign;
    ui->ask_move_column = cb_ask_move_column;
    ui->ask_game_number = cb_ask_game_number;
    ui->you_win = cb_you_win;
    ui->you_lose = cb_you_lose;
    ui->cheat_prompt = cb_cheat_prompt;
    ui->animate_step = cb_animate_step;
    ui->invalidate = cb_invalidate;
    ui->set_title = cb_set_title;
    ui->cards_left_changed = cb_cards_left_changed;
    ui->menu_state = cb_menu_state;
    ui->set_timer = cb_set_timer;
    ui->flash = cb_flash;
    ui->post_command = cb_post_command;
    ui->now_seed = cb_now_seed;
    ui->now_ms = cb_now_ms;
    ui->status_changed = cb_status_changed;
    ui->solve_start = cb_solve_start;
    ui->solve_cancel = cb_solve_cancel;
    ui->assist_menu = cb_assist_menu;
}

/* The menu resource starts with Undo, Redo, Restart, Hint and Finish grayed (XP: Undo and Restart). */
void ui_menu_init(App *a)
{
    a->menu_undo = a->menu_redo = a->menu_restart = 0;
    a->menu_hint = a->menu_finish = 0;
}
