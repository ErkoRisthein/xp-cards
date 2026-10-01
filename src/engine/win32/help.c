/*
 * Card engine, Win32 — Help menu plumbing (see help.h).
 */
#include "help.h"

#include <shellapi.h>

typedef HWND (WINAPI *HtmlHelpW_fn)(HWND, LPCWSTR, UINT, DWORD_PTR);

#define HH_CLOSE_ALL 0x0012

static HMODULE      g_hh;
static int          g_hh_tried;
static HtmlHelpW_fn g_html_help;

static HtmlHelpW_fn get_html_help(void)
{
    if (!g_hh_tried) {
        g_hh_tried = 1;
        g_hh = LoadLibraryW(L"hhctrl.ocx");
        if (g_hh)
            g_html_help = (HtmlHelpW_fn)(void *)GetProcAddress(g_hh, "HtmlHelpW");
    }
    return g_html_help;
}

int ce_help_open_chm(HWND owner, const WCHAR *name, UINT cmd)
{
    HtmlHelpW_fn hh = get_html_help();
    WCHAR path[MAX_PATH + 32];
    UINT n;
    if (!hh)
        return 0;
    if (hh(owner, name, cmd, 0))
        return 1;
    n = GetWindowsDirectoryW(path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return 0;
    lstrcatW(path, L"\\Help\\");
    lstrcatW(path, name);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return 0;
    return hh(owner, path, cmd, 0) != NULL;
}

void ce_help_shutdown(void)
{
    if (g_html_help)
        g_html_help(NULL, NULL, HH_CLOSE_ALL, 0);
}

void ce_shell_about(HWND owner, const WCHAR *title, const WCHAR *other, HICON icon)
{
    WCHAR t[128], o[512];
    lstrcpynW(t, title ? title : L"", 128);
    lstrcpynW(o, other ? other : L"", 512);
    ShellAboutW(owner, t, o, icon);
}
