/*
 * Card engine, Win32 — the main window's geometry (see window.h).
 */
#include "window.h"
#include "regstore.h"
#include "winutil.h"

#include <string.h>

void ce_window_outer_size(int cw, int ch, int *w, int *h)
{
    RECT r = { 0, 0, cw, ch };
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, TRUE, 0);
    *w = r.right - r.left;
    *h = r.bottom - r.top;
}

void ce_window_primary_work_area(RECT *w)
{
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, w, 0)) {
        w->left = w->top = 0;
        w->right = GetSystemMetrics(SM_CXSCREEN);
        w->bottom = GetSystemMetrics(SM_CYSCREEN);
    }
}

void ce_window_default_rect(RECT *out, CeClientForScale client_for_scale)
{
    RECT wk;
    int ww, wh, cw = 632, ch = 427, w = 640, h = 480;
    double s;
    ce_window_primary_work_area(&wk);
    ww = wk.right - wk.left;
    wh = wk.bottom - wk.top;
    for (s = 8.0; s >= 0.5; s -= 0.01) {
        client_for_scale(s, &cw, &ch);
        ce_window_outer_size(cw, ch, &w, &h);
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

int ce_placement_fix(WINDOWPLACEMENT *wp, int show, int minw, int minh, RECT *screen)
{
    POINT o = { 0, 0 };
    MONITORINFO pmi, mi;
    HMONITOR pm, m;
    RECT r = wp->rcNormalPosition, wk;
    int w = r.right - r.left, h = r.bottom - r.top, ox, oy, maxed;
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
        return 0;                                     /* the monitor is smaller than the minimum */
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

int ce_placement_load(const WCHAR *key, WINDOWPLACEMENT *wp)
{
    memset(wp, 0, sizeof *wp);
    return ce_reg_get_blob(key, CE_PLACEMENT_VALUE, wp, sizeof *wp) && wp->length == sizeof *wp;
}

void ce_placement_save_wp(const WCHAR *key, const WINDOWPLACEMENT *wp)
{
    ce_reg_set_blob(key, CE_PLACEMENT_VALUE, wp, sizeof *wp);
}

void ce_placement_save(const WCHAR *key, HWND hwnd)
{
    WINDOWPLACEMENT wp;
    memset(&wp, 0, sizeof wp);
    wp.length = sizeof wp;
    if (!hwnd || !GetWindowPlacement(hwnd, &wp))
        return;
    ce_placement_save_wp(key, &wp);
}

/* ---- full screen ------------------------------------------------------------------------------- */

int ce_monitor_rect(HWND h, RECT *r)
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

void ce_fullscreen_fit(const CeFullScreen *fs, HWND h)
{
    RECT r;
    if (fs->on && h && ce_monitor_rect(h, &r))
        SetWindowPos(h, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

int ce_fullscreen_enter(CeFullScreen *fs, HWND h, const WINDOWPLACEMENT *prev)
{
    WINDOWPLACEMENT wp;
    RECT r;
    if (!h || fs->on || IsIconic(h))
        return 0;
    memset(&wp, 0, sizeof wp);
    wp.length = sizeof wp;
    if (prev)
        wp = *prev;
    else if (!GetWindowPlacement(h, &wp))
        return 0;
    if (wp.showCmd != SW_SHOWMAXIMIZED)
        wp.showCmd = SW_SHOWNORMAL;
    wp.flags = 0;
    if (!ce_monitor_rect(h, &r))
        return 0;
    fs->prev = wp;
    fs->style = GetWindowLongW(h, GWL_STYLE);
    fs->on = 1;
    SetWindowLongW(h, GWL_STYLE, fs->style & ~(WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MAXIMIZE));
    ce_fullscreen_fit(fs, h);
    ce_log("full screen on: monitor (%ld,%ld)-(%ld,%ld)", r.left, r.top, r.right, r.bottom);
    return 1;
}

int ce_fullscreen_leave(CeFullScreen *fs, HWND h)
{
    LONG vis;
    if (!h || !fs->on)
        return 0;
    fs->on = 0;
    vis = GetWindowLongW(h, GWL_STYLE) & WS_VISIBLE;
    SetWindowLongW(h, GWL_STYLE, (fs->style & ~(WS_VISIBLE | WS_MAXIMIZE | WS_MINIMIZE)) | vis);
    SetWindowPlacement(h, &fs->prev);
    SetWindowPos(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                 SWP_FRAMECHANGED);
    ce_log("full screen off");
    return 1;
}

void ce_fullscreen_save_placement(const CeFullScreen *fs, HWND h, const WCHAR *key)
{
    if (fs->on)
        ce_placement_save_wp(key, &fs->prev);
    else
        ce_placement_save(key, h);
}
