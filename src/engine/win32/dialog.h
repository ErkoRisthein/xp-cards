/*
 * Card engine, Win32 — placing dialogs.
 *
 * XP FreeCell centres its dialogs over the main window's client area (helper 0x1002411); XP Solitaire
 * leaves them at their template position relative to the client area. Either way the HD games keep
 * every dialog on the monitor's work area, since the window may be maximized or full screen. Each
 * placement is logged (ce_log: "<what> dialog at X,Y size WxH (client origin CX,CY)"), which the e2e
 * scripts use to find a dialog in a window capture.
 */
#ifndef CE_DIALOG_H
#define CE_DIALOG_H

#include <windows.h>

/* Move r (screen coordinates) into the work area of the monitor it is on (else the one nearest
 * near_wnd). */
void ce_clamp_to_work_area(HWND near_wnd, RECT *r);

/* Move dlg to r's top-left (clamped to the work area), logging it as `what`. */
void ce_dialog_move(HWND dlg, RECT r, HWND owner, const char *what);

/* Centre dlg over owner's client area (over the work area if owner is missing or minimized). */
void ce_dialog_center(HWND dlg, HWND owner);

#endif
