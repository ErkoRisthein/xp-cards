/*
 * Card engine, Win32 — small utilities (see winutil.h).
 */
#include "winutil.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static HANDLE        g_log;
static LARGE_INTEGER g_qpf;

/* ---- timing log ---------------------------------------------------------------------------------- */

void ce_log_open(const WCHAR *env_var)
{
    WCHAR path[MAX_PATH];
    QueryPerformanceFrequency(&g_qpf);
    if (env_var && GetEnvironmentVariableW(env_var, path, MAX_PATH) - 1u < MAX_PATH - 1u)
        g_log = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
}

void ce_log_close(void)
{
    if (g_log && g_log != INVALID_HANDLE_VALUE)
        CloseHandle(g_log);
    g_log = NULL;
}

double ce_now_ms(void)
{
    LARGE_INTEGER t;
    if (!g_qpf.QuadPart || !QueryPerformanceCounter(&t))
        return (double)GetTickCount();
    return (double)t.QuadPart * 1000.0 / (double)g_qpf.QuadPart;
}

void ce_log(const char *fmt, ...)
{
    char buf[300];
    va_list ap;
    int n;
    DWORD w;
    if (!g_log || g_log == INVALID_HANDLE_VALUE)
        return;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > (int)sizeof buf - 3)
        n = (int)sizeof buf - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';
    WriteFile(g_log, buf, (DWORD)n, &w, NULL);
}

/* ---- strings ----------------------------------------------------------------------------------- */

int ce_to_wide(const char *s, WCHAR *out, int n)
{
    int k = MultiByteToWideChar(CP_UTF8, 0, s ? s : "", -1, out, n);
    if (k <= 0) {
        if (n > 0)
            out[0] = 0;
        return 0;
    }
    return k - 1;
}

int ce_load_wstr(HINSTANCE inst, UINT id, WCHAR *out, int n, const WCHAR *fallback)
{
    int k = LoadStringW(inst, id, out, n);
    if (k <= 0) {
        lstrcpynW(out, fallback, n);
        k = lstrlenW(out);
    }
    return k;
}

/* ---- resources --------------------------------------------------------------------------------- */

const void *ce_rcdata_loader(int id, size_t *len, void *ctx)
{
    HINSTANCE inst = (HINSTANCE)ctx;
    HRSRC r = FindResourceW(inst, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    HGLOBAL g;
    if (!r || !(g = LoadResource(inst, r)))
        return NULL;
    *len = SizeofResource(inst, r);
    return LockResource(g);
}

/* ---- time -------------------------------------------------------------------------------------- */

uint32_t ce_unix_time(void)
{
    FILETIME ft;
    uint64_t t;
    GetSystemTimeAsFileTime(&ft);
    t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (uint32_t)(t / 10000000u - 11644473600u);
}

uint32_t ce_time_seed(void)
{
    return ce_unix_time() * 1000u + GetTickCount() % 1000u;
}

/* ---- mouse ------------------------------------------------------------------------------------- */

int ce_mouse_over(HWND hwnd, POINT *pt)
{
    POINT p;
    RECT cr;
    if (!hwnd || !GetCursorPos(&p) || WindowFromPoint(p) != hwnd)
        return 0;
    ScreenToClient(hwnd, &p);
    GetClientRect(hwnd, &cr);
    if (!PtInRect(&cr, p))
        return 0;
    if (pt)
        *pt = p;
    return 1;
}

/* ---- message loop ------------------------------------------------------------------------------ */

int ce_message_loop(HWND *main, HACCEL accel)
{
    MSG msg;
    memset(&msg, 0, sizeof msg);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (*main && accel && (msg.hwnd == *main || IsChild(*main, msg.hwnd)) &&
            TranslateAcceleratorW(*main, accel, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
