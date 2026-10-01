/*
 * FreeCell HD — registry storage.
 *
 * Statistics and options: XP FreeCell's own key and format (rules.md §8, §9), so the statistics are
 * shared with the original game: HKCU\Software\Microsoft\Windows\CurrentVersion\Applets\FreeCell,
 * 4-byte little-endian REG_BINARY values, the key opened per operation (XP used RegCreateKeyW each
 * time). First-run migration source: entpack.ini [FreeCell] via GetPrivateProfileIntW.
 *
 * Window placement (an extra): HKCU\Software\xp-cards\FreeCell HD, value WindowPlacement
 * (REG_BINARY WINDOWPLACEMENT). The v1.1 extras live in the same key as REG_DWORD values
 * (ShowTimeMoves StandardSupermove FullRangeDeals FullScreen), never in XP's key.
 *
 * Won deals (an extra): %APPDATA%\xp-cards\FreeCell HD\won-deals.bin (SHGetFolderPathW, CSIDL_APPDATA;
 * format in src/core/wondeals.h), written atomically: won-deals.tmp, then MoveFileExW over the old file.
 */
#include "app.h"

#include <shlobj.h>
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

/* ---- our own key: the extras (REG_DWORD) ------------------------------------------------------ */

static int app_get(void *ctx, const char *name, uint32_t *value)
{
    WCHAR w[64];
    HKEY k;
    BYTE b[8];
    DWORD type = 0, len = sizeof b;
    LONG r;
    to_wide(name, w, 64);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, KEY_APP, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return 0;
    r = RegQueryValueExW(k, w, NULL, &type, b, &len);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || len != 4 || (type != REG_BINARY && type != REG_DWORD))
        return 0;
    *value = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return 1;
}

static void app_set(void *ctx, const char *name, uint32_t v)
{
    WCHAR w[64];
    HKEY k;
    DWORD d = v;
    to_wide(name, w, 64);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, KEY_APP, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExW(k, w, 0, REG_DWORD, (const BYTE *)&d, sizeof d);
    RegCloseKey(k);
}

static void app_del(void *ctx, const char *name)
{
    WCHAR w[64];
    HKEY k;
    to_wide(name, w, 64);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, KEY_APP, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(k, w);
    RegCloseKey(k);
}

static void app_flush(void *ctx)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, KEY_APP, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    RegFlushKey(k);
    RegCloseKey(k);
}

FcStore storage_app_store(void)
{
    FcStore s;
    memset(&s, 0, sizeof s);
    s.get = app_get;
    s.set = app_set;
    s.del = app_del;
    s.flush = app_flush;
    return s;
}

/* ---- won deals file -------------------------------------------------------------------------------- */

/* %APPDATA%\xp-cards\FreeCell HD (created when create != 0) + "\" + name. */
static int won_path(WCHAR *out, const WCHAR *name, int create)
{
    WCHAR dir[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA | (create ? CSIDL_FLAG_CREATE : 0), NULL,
                                SHGFP_TYPE_CURRENT, dir)))
        return 0;
    if (lstrlenW(dir) + 40 >= MAX_PATH)
        return 0;
    lstrcatW(dir, L"\\xp-cards");
    if (create)
        CreateDirectoryW(dir, NULL);                  /* fails harmlessly when it exists */
    lstrcatW(dir, L"\\FreeCell HD");
    if (create)
        CreateDirectoryW(dir, NULL);
    lstrcpyW(out, dir);
    lstrcatW(out, L"\\");
    lstrcatW(out, name);
    return 1;
}

static long won_read(void *ctx, void *buf, size_t cap)
{
    WCHAR path[MAX_PATH];
    HANDLE f;
    DWORD size, got, total = 0;
    if (!won_path(path, L"won-deals.bin", 0))
        return -1;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return -1;
    size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size > cap) {
        CloseHandle(f);
        return size == INVALID_FILE_SIZE ? -1 : (long)cap + 1;
    }
    while (total < size && ReadFile(f, (BYTE *)buf + total, size - total, &got, NULL) && got > 0)
        total += got;
    CloseHandle(f);
    return (long)total;
}

static int won_write(void *ctx, const void *data, size_t len)
{
    WCHAR tmp[MAX_PATH], path[MAX_PATH];
    HANDLE f;
    DWORD put = 0;
    BOOL ok;
    if (!won_path(tmp, L"won-deals.tmp", 1) || !won_path(path, L"won-deals.bin", 0))
        return 0;
    f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    ok = WriteFile(f, data, (DWORD)len, &put, NULL) && put == (DWORD)len;
    ok = FlushFileBuffers(f) && ok;
    CloseHandle(f);
    if (ok)
        ok = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok)
        DeleteFileW(tmp);
    return ok ? 1 : 0;
}

FcBlobIO storage_won_io(void)
{
    FcBlobIO io;
    memset(&io, 0, sizeof io);
    io.read = won_read;
    io.write = won_write;
    return io;
}

/* Keep a damaged file for inspection instead of overwriting it with the next win. */
void storage_won_set_aside(void)
{
    WCHAR path[MAX_PATH], bad[MAX_PATH];
    if (won_path(path, L"won-deals.bin", 0) && won_path(bad, L"won-deals.bad", 0))
        MoveFileExW(path, bad, MOVEFILE_REPLACE_EXISTING);
}

/* ---- window placement ------------------------------------------------------------------------------- */

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

void placement_save_wp(const WINDOWPLACEMENT *wp)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, KEY_APP, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExW(k, VAL_PLACEMENT, 0, REG_BINARY, (const BYTE *)wp, sizeof *wp);
    RegCloseKey(k);
}

void placement_save(HWND hwnd)
{
    WINDOWPLACEMENT wp;
    memset(&wp, 0, sizeof wp);
    wp.length = sizeof wp;
    if (!hwnd || !GetWindowPlacement(hwnd, &wp))
        return;
    placement_save_wp(&wp);
}
