/*
 * FreeCell HD — where things are kept (the engine's registry store and data file, engine/win32/regstore.h).
 *
 * Statistics and options: XP FreeCell's own key and format (rules.md §8, §9), so the statistics are
 * shared with the original game: HKCU\Software\Microsoft\Windows\CurrentVersion\Applets\FreeCell,
 * 4-byte little-endian REG_BINARY values, the key opened per operation (XP used RegCreateKeyW each
 * time). First-run migration source: entpack.ini [FreeCell] via GetPrivateProfileIntW.
 *
 * Window placement (an extra): HKCU\Software\xp-cards\FreeCell HD, value WindowPlacement
 * (REG_BINARY WINDOWPLACEMENT, engine/win32/window.h). The v1.1/v1.2 extras live in the same key as
 * REG_DWORD values (ShowTimeMoves StandardSupermove FullRangeDeals FullScreen WarnUnwinnable
 * AutoFinish), never in XP's key.
 *
 * Won deals (an extra): %APPDATA%\xp-cards\FreeCell HD\won-deals.bin (format in
 * src/freecell/wondeals.h), written atomically: won-deals.tmp, then MoveFileExW over the old file.
 */
#include "app.h"

const WCHAR FC_APP_KEY[] = CE_APP_KEY_ROOT L"FreeCell HD";

static const CeRegStore stats_key = {
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\FreeCell", 1, L"entpack.ini", L"FreeCell"
};
static const CeRegStore app_key = { FC_APP_KEY, 0, NULL, NULL };
static const CeAppFile won_file = { L"FreeCell HD", L"won-deals" };

CeStore storage_store(void) { return ce_reg_store(&stats_key); }

CeStore storage_app_store(void) { return ce_reg_store(&app_key); }

CeBlobIO storage_won_io(void) { return ce_app_file_io(&won_file); }

/* Keep a damaged file for inspection instead of overwriting it with the next win. */
void storage_won_set_aside(void) { ce_app_file_set_aside(&won_file); }
