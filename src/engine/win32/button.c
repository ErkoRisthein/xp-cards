/*
 * Card engine, Win32 — a push button drawn on the table (see button.h).
 */
#include "button.h"

#include <string.h>

/* uxtheme.dll (XP and later), loaded on first use; absent or without a theme: the classic button. */
typedef HANDLE (WINAPI *OpenThemeData_fn)(HWND, LPCWSTR);
typedef HRESULT (WINAPI *CloseThemeData_fn)(HANDLE);
typedef HRESULT (WINAPI *DrawThemeBackground_fn)(HANDLE, HDC, int, int, const RECT *, const RECT *);
typedef BOOL (WINAPI *IsThemeActive_fn)(void);
typedef BOOL (WINAPI *IsAppThemed_fn)(void);

static int ux_tried;
static OpenThemeData_fn ux_open;
static CloseThemeData_fn ux_close;
static DrawThemeBackground_fn ux_draw;
static IsThemeActive_fn ux_active;
static IsAppThemed_fn ux_app;

static void ux_load(void)
{
    HMODULE m;
    if (ux_tried)
        return;
    ux_tried = 1;
    m = LoadLibraryW(L"uxtheme.dll");
    if (!m)
        return;
    ux_open = (OpenThemeData_fn)(void *)GetProcAddress(m, "OpenThemeData");
    ux_close = (CloseThemeData_fn)(void *)GetProcAddress(m, "CloseThemeData");
    ux_draw = (DrawThemeBackground_fn)(void *)GetProcAddress(m, "DrawThemeBackground");
    ux_active = (IsThemeActive_fn)(void *)GetProcAddress(m, "IsThemeActive");
    ux_app = (IsAppThemed_fn)(void *)GetProcAddress(m, "IsAppThemed");
    if (!ux_open || !ux_close || !ux_draw || !ux_active || !ux_app)
        ux_open = NULL;
}

#define BP_PUSHBUTTON 1
#define PBS_NORMAL    1
#define PBS_HOT       2
#define PBS_PRESSED   3

void ce_tbutton_place(CeTableButton *b, CeRect r, int font_px)
{
    b->r = r.w > 0 && r.h > 0 ? r : ce_rect(0, 0, 0, 0);
    b->font_px = font_px > 0 ? font_px : 8;
}

void ce_tbutton_set_text(CeTableButton *b, const WCHAR *text)
{
    lstrcpynW(b->text, text, (int)(sizeof b->text / sizeof b->text[0]));
}

void ce_tbutton_free(CeTableButton *b)
{
    if (b->font)
        DeleteObject(b->font);
    b->font = NULL;
    b->font_made_px = 0;
}

CeRect ce_tbutton_rect(const CeTableButton *b)
{
    return b->shown && b->r.w > 0 ? b->r : ce_rect(0, 0, 0, 0);
}

static HFONT font(CeTableButton *b)
{
    if (b->font && b->font_made_px == b->font_px)
        return b->font;
    ce_tbutton_free(b);
    b->font = CreateFontW(-b->font_px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
    b->font_made_px = b->font ? b->font_px : 0;
    return b->font;
}

void ce_tbutton_draw(CeTableButton *b, CeBackBuf *bb, HWND hwnd, CeRect clip)
{
    CeRect r = ce_tbutton_rect(b), k;
    RECT rc, cl, tr;
    HDC dc = bb->memdc;
    HRGN rgn;
    HFONT f, old_font = NULL;
    HANDLE th = NULL;
    int x2, y2, down = b->pressed && b->armed;
    if (r.w <= 0 || !dc || !bb->bits)
        return;
    k.x = r.x > clip.x ? r.x : clip.x;
    k.y = r.y > clip.y ? r.y : clip.y;
    x2 = r.x + r.w < clip.x + clip.w ? r.x + r.w : clip.x + clip.w;
    y2 = r.y + r.h < clip.y + clip.h ? r.y + r.h : clip.y + clip.h;
    if (x2 <= k.x || y2 <= k.y)
        return;
    rc.left = r.x;
    rc.top = r.y;
    rc.right = r.x + r.w;
    rc.bottom = r.y + r.h;
    cl.left = k.x;
    cl.top = k.y;
    cl.right = x2;
    cl.bottom = y2;
    GdiFlush();
    rgn = CreateRectRgnIndirect(&cl);
    if (rgn)
        SelectClipRgn(dc, rgn);
    ux_load();
    if (ux_open && ux_active() && ux_app())
        th = ux_open(hwnd, L"Button");
    if (th) {
        ux_draw(th, dc, BP_PUSHBUTTON, down ? PBS_PRESSED : b->hot ? PBS_HOT : PBS_NORMAL, &rc, &cl);
        ux_close(th);
    } else {
        DrawFrameControl(dc, &rc, DFC_BUTTON, DFCS_BUTTONPUSH | (down ? DFCS_PUSHED : 0));
    }
    f = font(b);
    if (f)
        old_font = (HFONT)SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    tr = rc;
    if (down && !th)
        OffsetRect(&tr, 1, 1);                       /* the classic button's text moves with the press */
    DrawTextW(dc, b->text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (f)
        SelectObject(dc, old_font);
    SelectClipRgn(dc, NULL);
    if (rgn)
        DeleteObject(rgn);
    GdiFlush();
}

static int inside(const CeTableButton *b, int x, int y)
{
    CeRect r = ce_tbutton_rect(b);
    return r.w > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static void track_leave(CeTableButton *b, HWND hwnd)
{
    TRACKMOUSEEVENT t;
    if (b->tracking || !hwnd)
        return;
    memset(&t, 0, sizeof t);
    t.cbSize = sizeof t;
    t.dwFlags = TME_LEAVE;
    t.hwndTrack = hwnd;
    b->tracking = TrackMouseEvent(&t) ? 1 : 0;
}

int ce_tbutton_reset(CeTableButton *b, HWND hwnd)
{
    int was = b->hot || (b->pressed && b->armed), armed = b->armed;
    b->hot = b->pressed = b->armed = 0;
    if (armed && hwnd && GetCapture() == hwnd)
        ReleaseCapture();
    return was ? CE_TBUTTON_REDRAW : 0;
}

int ce_tbutton_mouse(CeTableButton *b, HWND hwnd, UINT msg, int x, int y)
{
    int in = inside(b, x, y), r = 0, look = (b->pressed && b->armed) * 2 + b->hot;
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (!in)
            return 0;
        b->armed = b->pressed = b->hot = 1;
        if (hwnd)
            SetCapture(hwnd);
        r = CE_TBUTTON_USED;
        break;
    case WM_MOUSEMOVE:
        if (b->armed) {
            b->pressed = in;
            r = CE_TBUTTON_USED;
        }
        b->hot = in;
        if (in)
            track_leave(b, hwnd);
        break;
    case WM_LBUTTONUP:
        if (!b->armed)
            return 0;
        b->armed = b->pressed = 0;
        b->hot = in;
        r = CE_TBUTTON_USED | (in ? CE_TBUTTON_CLICK : 0);
        if (hwnd && GetCapture() == hwnd)
            ReleaseCapture();
        break;
    case WM_MOUSELEAVE:
        b->tracking = 0;
        if (!b->armed)
            b->hot = 0;
        break;
    case WM_CAPTURECHANGED:
        if (b->armed) {
            b->armed = b->pressed = 0;
            r = CE_TBUTTON_USED;
        }
        break;
    }
    if ((b->pressed && b->armed) * 2 + b->hot != look)
        r |= CE_TBUTTON_REDRAW;
    return r;
}
