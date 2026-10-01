/*
 * Card engine, Win32 — persistence: a registry key as a CeStore (engine/store.h), registry blobs,
 * and a data file in %APPDATA% as a CeBlobIO.
 *
 * Each XP game has its own settings format, kept exactly so that the original and the HD game share
 * them: XP FreeCell writes 4-byte REG_BINARY values (HKCU\Software\Microsoft\Windows\CurrentVersion\
 * Applets\FreeCell), XP Solitaire REG_DWORD values (HKCU\Software\Microsoft\Solitaire). The HD extras
 * live in our own key, HKCU\Software\xp-cards\<game> HD (REG_DWORD values, and the window placement).
 * Every key is opened per operation (as XP did), under HKEY_CURRENT_USER.
 */
#ifndef CE_REGSTORE_H
#define CE_REGSTORE_H

#include <windows.h>
#include "engine/store.h"

#define CE_APP_KEY_ROOT L"Software\\xp-cards\\"     /* + "<game> HD": our own key */

typedef struct CeRegStore {
    const WCHAR *key;           /* under HKEY_CURRENT_USER */
    int          binary;        /* 1: write 4-byte little-endian REG_BINARY (XP FreeCell); 0: REG_DWORD.
                                   Either type is read, if it is 4 bytes long. */
    const WCHAR *ini_file;      /* first-run migration source (GetPrivateProfileInt), e.g. L"entpack.ini";
                                   NULL: none */
    const WCHAR *ini_section;   /* e.g. L"FreeCell" */
} CeRegStore;

/* A CeStore over rs (value names are ASCII/UTF-8, converted to UTF-16). rs is referenced, not copied:
 * it must outlive the store (a static). */
CeStore ce_reg_store(const CeRegStore *rs);

/* REG_BINARY value of exactly len bytes: 1 if read. */
int  ce_reg_get_blob(const WCHAR *key, const WCHAR *name, void *buf, DWORD len);
void ce_reg_set_blob(const WCHAR *key, const WCHAR *name, const void *data, DWORD len);

/* %APPDATA%\xp-cards\<app_dir>\<stem>.bin, written atomically (<stem>.tmp, then MoveFileExW over
 * the old file); the directories are created on the first write. */
typedef struct CeAppFile {
    const WCHAR *app_dir;       /* e.g. L"FreeCell HD" */
    const WCHAR *stem;          /* e.g. L"won-deals" */
} CeAppFile;

CeBlobIO ce_app_file_io(const CeAppFile *f);       /* f is referenced, not copied (a static) */
void     ce_app_file_set_aside(const CeAppFile *f);   /* keep a damaged file as <stem>.bad */

#endif
