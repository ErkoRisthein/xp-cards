/*
 * Card engine, Win32 — text at the right end of the menu bar (see menubar.h).
 */
#include "menubar.h"

#include <limits.h>
#include <string.h>

void ce_menubar_init(CeMenuBarText *mt)
{
    mt->font = NULL;
    mt->prev_left = INT_MAX;
    ce_menubar_font_update(mt);
}

void ce_menubar_free(CeMenuBarText *mt)
{
    if (mt->font)
        DeleteObject(mt->font);
    mt->font = NULL;
}

void ce_menubar_font_update(CeMenuBarText *mt)
{
    NONCLIENTMETRICSW ncm;
    HFONT f = NULL;
    memset(&ncm, 0, sizeof ncm);
    ncm.cbSize = sizeof ncm;                          /* XP size: built with WINVER 0x0501 */
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0))
        f = CreateFontIndirectW(&ncm.lfMenuFont);
    if (mt->font)
        DeleteObject(mt->font);
    mt->font = f;
}

void ce_menubar_reset(CeMenuBarText *mt)
{
    mt->prev_left = INT_MAX;
}

void ce_menubar_draw(CeMenuBarText *mt, HWND h, HMENU menu, int fullscreen, const WCHAR *const *texts, int nt)
{
    MENUBARINFO mbi;
    RECT wr, cr, bar, er;
    const WCHAR *text = NULL;
    SIZE sz;
    TEXTMETRICW tm;
    HDC dc;
    HGDIOBJ of;
    int right, x = 0, y, len = 0, items, i, n;
    BOOL flat = FALSE;
    if (!h || !menu || IsIconic(h) || !IsWindowVisible(h) || nt < 1)
        return;
    memset(&mbi, 0, sizeof mbi);
    mbi.cbSize = sizeof mbi;
    if (!GetMenuBarInfo(h, OBJID_MENU, 0, &mbi) || !GetWindowRect(h, &wr) || !GetClientRect(h, &cr))
        return;
    bar = mbi.rcBar;
    OffsetRect(&bar, -wr.left, -wr.top);              /* window-DC coordinates */
    if (bar.bottom <= bar.top)
        return;
    right = cr.right;                                 /* XP: x = clientWidth - textWidth, window DC */
    if (right > bar.right)
        right = bar.right;
    dc = GetWindowDC(h);
    if (!dc)
        return;
    of = SelectObject(dc, mt->font ? (HGDIOBJ)mt->font : GetStockObject(DEFAULT_GUI_FONT));
    GetTextExtentPoint32W(dc, texts[nt - 1], lstrlenW(texts[nt - 1]), &sz);
    if (!GetTextMetricsW(dc, &tm))
        tm.tmHeight = sz.cy;
    /* XP: y = SM_CYFRAME + SM_CYCAPTION + (SM_CYMENU - tmHeight) / 2, i.e. from the top of the menu
     * bar of a captioned window; in full screen (no frame, no caption) the same from the bar's top;
     * else centred in the bar */
    if (fullscreen)
        y = bar.top + (GetSystemMetrics(SM_CYMENU) - tm.tmHeight) / 2;
    else
        y = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CYCAPTION) +
            (GetSystemMetrics(SM_CYMENU) - tm.tmHeight) / 2;
    if (y < bar.top - 1 || y + sz.cy > bar.bottom)
        y = bar.top + (bar.bottom - bar.top - sz.cy) / 2;
    /* never overlap the menu items ("Game", "Help") on the text's row */
    items = bar.left;
    n = GetMenuItemCount(menu);
    for (i = 0; i < n; i++) {
        RECT ir;
        if (!GetMenuItemRect(h, menu, (UINT)i, &ir))
            continue;
        OffsetRect(&ir, -wr.left, -wr.top);
        if (ir.top < y + sz.cy && ir.bottom > y && ir.right > items)
            items = ir.right;
    }
    for (i = 0; i < nt && !text; i++) {               /* the longest text that fits */
        SIZE ts;
        int l = lstrlenW(texts[i]);
        if (!GetTextExtentPoint32W(dc, texts[i], l, &ts))
            continue;
        if (right - ts.cx >= items + sz.cy) {         /* a gap of about two characters */
            text = texts[i];
            len = l;
            x = right - ts.cx;
        }
    }
    if (text) {
        SystemParametersInfoW(SPI_GETFLATMENU, 0, &flat, 0);
        er.left = x < mt->prev_left ? x : mt->prev_left;   /* erase a longer previous text */
        if (er.left < items + 1)
            er.left = items + 1;
        er.right = right;
        er.top = y > bar.top ? y : bar.top;
        er.bottom = y + sz.cy < bar.bottom ? y + sz.cy : bar.bottom;
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, GetSysColor(flat ? COLOR_MENUBAR : COLOR_MENU));
        SetTextColor(dc, GetSysColor(COLOR_MENUTEXT));
        ExtTextOutW(dc, x, y, ETO_OPAQUE | ETO_CLIPPED, &er, text, (UINT)len, NULL);
        mt->prev_left = x;
    }
    SelectObject(dc, of);
    ReleaseDC(h, dc);
}
