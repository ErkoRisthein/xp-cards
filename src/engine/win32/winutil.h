/*
 * Card engine, Win32 — small utilities: timing log, wide strings, resource access, the clock seed,
 * the mouse, the message loop.
 *
 * XP rules for everything under src/engine/win32: XP SP2 APIs only (tools/xp_imports_check.py), no
 * assert() (it imports _wassert, absent from XP's msvcrt), and never a string literal passed to an API
 * that may write into it (ShellAboutW does: see help.h).
 */
#ifndef CE_WINUTIL_H
#define CE_WINUTIL_H

#include <windows.h>
#include <stddef.h>
#include <stdint.h>

/* ---- timing log -----------------------------------------------------------------------------------
 * If the environment variable env_var names a file, every ce_log line is appended to it (CRLF). The
 * e2e scripts read it (e.g. dialog positions). ce_log_open also starts the high-resolution clock. */
void   ce_log_open(const WCHAR *env_var);
void   ce_log_close(void);
void   ce_log(const char *fmt, ...);
double ce_now_ms(void);                          /* QueryPerformanceCounter, in ms (GetTickCount before ce_log_open) */

/* ---- strings -------------------------------------------------------------------------------------- */
int    ce_to_wide(const char *s, WCHAR *out, int n);   /* UTF-8 -> UTF-16; returns the length */
/* LoadStringW, or the fallback if the string is missing; returns the length. */
int    ce_load_wstr(HINSTANCE inst, UINT id, WCHAR *out, int n, const WCHAR *fallback);

/* ---- resources ------------------------------------------------------------------------------------
 * A CeAssetLoader (engine/cardset.h) over RT_RCDATA resources: ctx is the HINSTANCE, the asset id is
 * the resource id. */
const void *ce_rcdata_loader(int id, size_t *len, void *ctx);

/* ---- time ----------------------------------------------------------------------------------------- */
uint32_t ce_unix_time(void);                     /* seconds since 1970 (as msvcrt time(NULL)) */
uint32_t ce_time_seed(void);                     /* Unix time * 1000 + the millisecond tick % 1000 */

/* ---- mouse ---------------------------------------------------------------------------------------- */
/* Is the mouse pointer over hwnd's client area (and hwnd the window under it)? Client coordinates in
 * *pt (may be NULL). */
int    ce_mouse_over(HWND hwnd, POINT *pt);

/* ---- message loop --------------------------------------------------------------------------------- */
/* GetMessage / TranslateAccelerator (for *main and its children, while *main is not NULL) / Translate
 * / Dispatch until WM_QUIT; returns its wParam. main is read on every message (it becomes NULL when
 * the window is destroyed). */
int    ce_message_loop(HWND *main, HACCEL accel);

#endif
