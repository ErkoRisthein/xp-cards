/*
 * FreeCell HD — registry storage.
 *
 * Statistics and options: XP FreeCell's own key and format (rules.md §8, §9), so the statistics are
 * shared with the original game: HKCU\Software\Microsoft\Windows\CurrentVersion\Applets\FreeCell,
 * 4-byte little-endian REG_BINARY values, the key opened per operation (XP used RegCreateKeyW each
 * time). First-run migration source: entpack.ini [FreeCell] via GetPrivateProfileIntW.
 *
 * Window placement (an extra): HKCU\Software\xp-cards\FreeCell HD, value WindowPlacement
 * (REG_BINARY WINDOWPLACEMENT).
 */
#include "app.h"

#include <string.h>

#define KEY_STATS L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\FreeCell"
#define KEY_APP   L"Software\\xp-cards\\FreeCell HD"
#define VAL_PLACEMENT L"WindowPlacement"

static int st_get(void *ctx, const char *name, uint32_t *value)
{
    WCHAR w[64];
    HKEY k;
    BYTE b[8];
    DWORD type = 0, len = sizeof b;
    LONG r;
    to_wide(name, w, 64);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, KEY_STATS, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return 0;
    r = RegQueryValueExW(k, w, NULL, &type, b, &len);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || len != 4 || (type != REG_BINARY && type != REG_DWORD))
        return 0;
    *value = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return 1;
}

static void st_set(void *ctx, const char *name, uint32_t v)
{
    WCHAR w[64];
    HKEY k;
    BYTE b[4];
    to_wide(name, w, 64);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, KEY_STATS, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    b[0] = (BYTE)v;
    b[1] = (BYTE)(v >> 8);
    b[2] = (BYTE)(v >> 16);
    b[3] = (BYTE)(v >> 24);
    RegSetValueExW(k, w, 0, REG_BINARY, b, 4);
    RegCloseKey(k);
}

static void st_del(void *ctx, const char *name)
{
    WCHAR w[64];
    HKEY k;
    to_wide(name, w, 64);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, KEY_STATS, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(k, w);
    RegCloseKey(k);
}

static void st_flush(void *ctx)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, KEY_STATS, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    RegFlushKey(k);
    RegCloseKey(k);
}

/* entpack.ini [FreeCell] <key>; a sentinel default tells "absent" from a stored value. */
static int st_legacy(void *ctx, const char *key, uint32_t *value)
{
    const INT sentinel = -0x2468ACE;
    WCHAR w[64];
    UINT v;
    to_wide(key, w, 64);
    v = GetPrivateProfileIntW(L"FreeCell", w, sentinel, L"entpack.ini");
    if ((INT)v == sentinel)
        return 0;
    *value = v;
    return 1;
}

FcStore storage_store(void)
{
    FcStore s;
    memset(&s, 0, sizeof s);
    s.get = st_get;
    s.set = st_set;
    s.del = st_del;
    s.flush = st_flush;
    s.legacy_get = st_legacy;
    return s;
}

int placement_load(WINDOWPLACEMENT *wp)
{
    HKEY k;
    DWORD type = 0, len = sizeof *wp;
    LONG r;
    memset(wp, 0, sizeof *wp);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, KEY_APP, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return 0;
    r = RegQueryValueExW(k, VAL_PLACEMENT, NULL, &type, (BYTE *)wp, &len);
    RegCloseKey(k);
    return r == ERROR_SUCCESS && type == REG_BINARY && len == sizeof *wp && wp->length == sizeof *wp;
}

void placement_save(HWND hwnd)
{
    WINDOWPLACEMENT wp;
    HKEY k;
    memset(&wp, 0, sizeof wp);
    wp.length = sizeof wp;
    if (!hwnd || !GetWindowPlacement(hwnd, &wp))
        return;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, KEY_APP, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExW(k, VAL_PLACEMENT, 0, REG_BINARY, (const BYTE *)&wp, sizeof wp);
    RegCloseKey(k);
}
