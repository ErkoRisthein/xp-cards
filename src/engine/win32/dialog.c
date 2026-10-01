/*
 * Card engine, Win32 — placing dialogs (see dialog.h).
 */
#include "dialog.h"
#include "winutil.h"

#include <string.h>

void ce_clamp_to_work_area(HWND near_wnd, RECT *r)
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

void ce_dialog_move(HWND dlg, RECT r, HWND owner, const char *what)
{
    POINT o = { 0, 0 };
    ce_clamp_to_work_area(owner, &r);
    SetWindowPos(dlg, NULL, r.left, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (owner)
        ClientToScreen(owner, &o);
    ce_log("%s dialog at %ld,%ld size %ldx%ld (client origin %ld,%ld)", what, r.left, r.top,
           r.right - r.left, r.bottom - r.top, o.x, o.y);
}

void ce_dialog_center(HWND dlg, HWND owner)
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
    ce_dialog_move(dlg, r, owner, "centred");
}
