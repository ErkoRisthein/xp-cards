/*
 * Card engine, Win32 — persistence (see regstore.h).
 */
#include "regstore.h"
#include "winutil.h"

#include <shlobj.h>
#include <string.h>

/* ---- a registry key as a CeStore ------------------------------------------------------------------ */

static int reg_get(void *ctx, const char *name, uint32_t *value)
{
    const CeRegStore *rs = ctx;
    WCHAR w[64];
    HKEY k;
    BYTE b[8];
    DWORD type = 0, len = sizeof b;
    LONG r;
    ce_to_wide(name, w, 64);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, rs->key, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return 0;
    r = RegQueryValueExW(k, w, NULL, &type, b, &len);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || len != 4 || (type != REG_BINARY && type != REG_DWORD))
        return 0;
    *value = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return 1;
}

static void reg_set(void *ctx, const char *name, uint32_t v)
{
    const CeRegStore *rs = ctx;
    WCHAR w[64];
    HKEY k;
    BYTE b[4];
    DWORD d = v;
    ce_to_wide(name, w, 64);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, rs->key, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    if (rs->binary) {
        b[0] = (BYTE)v;
        b[1] = (BYTE)(v >> 8);
        b[2] = (BYTE)(v >> 16);
        b[3] = (BYTE)(v >> 24);
        RegSetValueExW(k, w, 0, REG_BINARY, b, 4);
    } else {
        RegSetValueExW(k, w, 0, REG_DWORD, (const BYTE *)&d, sizeof d);
    }
    RegCloseKey(k);
}

static void reg_del(void *ctx, const char *name)
{
    const CeRegStore *rs = ctx;
    WCHAR w[64];
    HKEY k;
    ce_to_wide(name, w, 64);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, rs->key, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(k, w);
    RegCloseKey(k);
}

static void reg_flush(void *ctx)
{
    const CeRegStore *rs = ctx;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, rs->key, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    RegFlushKey(k);
    RegCloseKey(k);
}

/* <ini_file> [<ini_section>] <key>; a sentinel default tells "absent" from a stored value. */
static int reg_legacy(void *ctx, const char *key, uint32_t *value)
{
    const CeRegStore *rs = ctx;
    const INT sentinel = -0x2468ACE;
    WCHAR w[64];
    UINT v;
    ce_to_wide(key, w, 64);
    v = GetPrivateProfileIntW(rs->ini_section, w, sentinel, rs->ini_file);
    if ((INT)v == sentinel)
        return 0;
    *value = v;
    return 1;
}

CeStore ce_reg_store(const CeRegStore *rs)
{
    CeStore s;
    memset(&s, 0, sizeof s);
    s.ctx = (void *)rs;
    s.get = reg_get;
    s.set = reg_set;
    s.del = reg_del;
    s.flush = reg_flush;
    if (rs->ini_file && rs->ini_section)
        s.legacy_get = reg_legacy;
    return s;
}

/* ---- blobs ------------------------------------------------------------------------------------- */

int ce_reg_get_blob(const WCHAR *key, const WCHAR *name, void *buf, DWORD len)
{
    HKEY k;
    DWORD type = 0, got = len;
    LONG r;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return 0;
    r = RegQueryValueExW(k, name, NULL, &type, (BYTE *)buf, &got);
    RegCloseKey(k);
    return r == ERROR_SUCCESS && type == REG_BINARY && got == len;
}

void ce_reg_set_blob(const WCHAR *key, const WCHAR *name, const void *data, DWORD len)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExW(k, name, 0, REG_BINARY, (const BYTE *)data, len);
    RegCloseKey(k);
}

/* ---- a data file in %APPDATA% ------------------------------------------------------------------- */

/* %APPDATA%\xp-cards\<app_dir> (created when create != 0) + "\" + stem + ext (ext: 4 characters). */
static int app_path(const CeAppFile *f, WCHAR *out, const WCHAR *ext, int create)
{
    WCHAR dir[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA | (create ? CSIDL_FLAG_CREATE : 0), NULL,
                                SHGFP_TYPE_CURRENT, dir)))
        return 0;
    /* "\xp-cards" + "\" + app_dir + "\" + stem + ext, and a margin */
    if (lstrlenW(dir) + 9 + 1 + lstrlenW(f->app_dir) + 1 + lstrlenW(f->stem) + lstrlenW(ext) + 5 >= MAX_PATH)
        return 0;
    lstrcatW(dir, L"\\xp-cards");
    if (create)
        CreateDirectoryW(dir, NULL);                  /* fails harmlessly when it exists */
    lstrcatW(dir, L"\\");
    lstrcatW(dir, f->app_dir);
    if (create)
        CreateDirectoryW(dir, NULL);
    lstrcpyW(out, dir);
    lstrcatW(out, L"\\");
    lstrcatW(out, f->stem);
    lstrcatW(out, ext);
    return 1;
}

static long file_read(void *ctx, void *buf, size_t cap)
{
    WCHAR path[MAX_PATH];
    HANDLE h;
    DWORD size, got, total = 0;
    if (!app_path(ctx, path, L".bin", 0))
        return -1;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size > cap) {
        CloseHandle(h);
        return size == INVALID_FILE_SIZE ? -1 : (long)cap + 1;
    }
    while (total < size && ReadFile(h, (BYTE *)buf + total, size - total, &got, NULL) && got > 0)
        total += got;
    CloseHandle(h);
    return (long)total;
}

static int file_write(void *ctx, const void *data, size_t len)
{
    WCHAR tmp[MAX_PATH], path[MAX_PATH];
    HANDLE h;
    DWORD put = 0;
    BOOL ok;
    if (!app_path(ctx, tmp, L".tmp", 1) || !app_path(ctx, path, L".bin", 0))
        return 0;
    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ok = WriteFile(h, data, (DWORD)len, &put, NULL) && put == (DWORD)len;
    ok = FlushFileBuffers(h) && ok;
    CloseHandle(h);
    if (ok)
        ok = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok)
        DeleteFileW(tmp);
    return ok ? 1 : 0;
}

CeBlobIO ce_app_file_io(const CeAppFile *f)
{
    CeBlobIO io;
    memset(&io, 0, sizeof io);
    io.ctx = (void *)f;
    io.read = file_read;
    io.write = file_write;
    return io;
}

void ce_app_file_set_aside(const CeAppFile *f)
{
    WCHAR path[MAX_PATH], bad[MAX_PATH];
    if (app_path(f, path, L".bin", 0) && app_path(f, bad, L".bad", 0))
        MoveFileExW(path, bad, MOVEFILE_REPLACE_EXISTING);
}
