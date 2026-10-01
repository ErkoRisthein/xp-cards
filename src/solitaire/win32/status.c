/*
 * Solitaire HD — XP's status bar (layout.md §4, resources.md §4.2): a white child window across the
 * bottom of the client, system-sized like the menu bar (height = the system font's height + 2: 18 px
 * at 96 DPI), placed 1 px outside the client on the left, right and bottom so that only its top border
 * line shows. The board scales; the status bar does not (its height comes off the board's height).
 *
 *   left:  the menu help of the highlighted item (WM_MENUSELECT: the string with the command's id) or
 *          the win text, TextOutW at (4, 0) in the system font;
 *   right: right to left, "Time: <s>" (Timed game), the score and a space (red when negative, "$" in
 *          Vegas), "Score: " (no score with scoring None), MS Shell Dlg 9 pt bold, top-aligned, 4 px
 *          from the right edge.
 *
 * Drawn off screen and copied in one BitBlt (XP drew straight to the window; the clock repaints it
 * every 250 ms).
 */
#include "app.h"

#include <string.h>

static LRESULT CALLBACK status_proc(HWND h, UINT m, WPARAM wp, LPARAM lp);

int status_register(HINSTANCE inst)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = status_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = SOL_STATUS_CLASS;
    return RegisterClassExW(&wc) != 0;
}

static int system_font_metrics(TEXTMETRICW *tm)
{
    HDC dc = GetDC(NULL);
    HGDIOBJ old;
    int ok;
    if (!dc)
        return 0;
    old = SelectObject(dc, GetStockObject(SYSTEM_FONT));
    ok = GetTextMetricsW(dc, tm);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    return ok;
}

int status_height(void)
{
    TEXTMETRICW tm;
    return system_font_metrics(&tm) && tm.tmHeight > 0 ? tm.tmHeight + 2 : SOL_XP_STATUS_H;
}

void status_place(App *a)
{
    if (a->status && a->have_layout && a->L.status_h > 0)
        MoveWindow(a->status, a->L.status.x, a->L.status.y, a->L.status.w, a->L.status.h, TRUE);
}

void status_show(App *a, int on)
{
    if (on && !a->status && a->hwnd) {
        a->status = CreateWindowExW(0, SOL_STATUS_CLASS, L"", WS_CHILD | WS_BORDER | WS_VISIBLE, 0, 0, 0, 0,
                                    a->hwnd, NULL, a->inst, NULL);
    } else if (!on && a->status) {
        DestroyWindow(a->status);
        a->status = NULL;
    }
    view_relayout(a);                                 /* the board takes or gives back its height */
}

void status_set_left(App *a, const WCHAR *text)
{
    if (lstrcmpW(a->status_left, text) == 0)
        return;
    lstrcpynW(a->status_left, text, (int)(sizeof a->status_left / sizeof a->status_left[0]));
    if (a->status) {
        InvalidateRect(a->status, NULL, FALSE);
        UpdateWindow(a->status);
    }
}

void status_update(App *a)
{
    if (a->status) {
        InvalidateRect(a->status, NULL, FALSE);
        UpdateWindow(a->status);
    }
}

void status_free(App *a)
{
    if (a->status_font)
        DeleteObject(a->status_font);
    a->status_font = NULL;
}

/* The right part, drawn right to left as XP's DrawStatus (0x1005203). */
static void draw_right(App *a, HDC dc, int w, int h)
{
    const SolSession *s = &a->s;
    WCHAR piece[3][48], label[24];
    COLORREF color[3];
    SIZE sz;
    RECT r;
    TEXTMETRICW sys;
    int n = 0, i, right = w - 4, total = 0, wipe;
    if (s->opts.timed) {
        load_wstr(a, IDS_TIME, label, 24, L"Time: ");
        wsprintfW(piece[n], L"%s%d", label, sol_seconds(s));
        color[n++] = RGB(0, 0, 0);
    }
    if (s->opts.scoring != SOL_SCORING_NONE) {
        char sc[32];
        WCHAR wsc[32];
        sol_score_text(s, sc, sizeof sc);
        ce_to_wide(sc, wsc, 32);
        wsprintfW(piece[n], L"%s ", wsc);
        color[n++] = s->score < 0 ? RGB(255, 0, 0) : RGB(0, 0, 0);
        load_wstr(a, IDS_SCORE, piece[n], 48, L"Score: ");
        color[n++] = RGB(0, 0, 0);
    }
    if (!n)
        return;
    SelectObject(dc, a->status_font ? a->status_font : GetStockObject(DEFAULT_GUI_FONT));
    for (i = 0; i < n; i++)
        if (GetTextExtentPoint32W(dc, piece[i], lstrlenW(piece[i]), &sz))
            total += sz.cx;
    /* XP wipes 4 x tmMaxCharWidth (system font) to the left of the text: over a long menu help text */
    wipe = system_font_metrics(&sys) ? 4 * sys.tmMaxCharWidth : 64;
    r.left = right - total - wipe;
    r.top = 0;
    r.right = w;
    r.bottom = h;
    if (r.left < 0)
        r.left = 0;
    FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    for (i = 0; i < n; i++) {
        r.left = 0;
        r.top = 0;
        r.right = right;
        r.bottom = h;
        SetTextColor(dc, color[i]);
        DrawTextW(dc, piece[i], -1, &r, DT_RIGHT | DT_SINGLELINE | DT_NOCLIP);
        if (GetTextExtentPoint32W(dc, piece[i], lstrlenW(piece[i]), &sz))
            right -= sz.cx;
    }
}

static void paint(App *a, HWND h)
{
    PAINTSTRUCT ps;
    RECT rc;
    HDC dc = BeginPaint(h, &ps), mem;
    HBITMAP bmp;
    HGDIOBJ old_bmp, old_font;
    if (!dc)
        return;
    GetClientRect(h, &rc);
    mem = CreateCompatibleDC(dc);
    bmp = mem ? CreateCompatibleBitmap(dc, rc.right > 0 ? rc.right : 1, rc.bottom > 0 ? rc.bottom : 1) : NULL;
    if (!bmp) {                                       /* out of GDI memory: draw straight to the window */
        if (mem)
            DeleteDC(mem);
        mem = NULL;
    }
    {
        HDC t = mem ? mem : dc;
        old_bmp = mem ? SelectObject(mem, bmp) : NULL;
        FillRect(t, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        SetBkMode(t, TRANSPARENT);
        SetTextColor(t, RGB(0, 0, 0));
        old_font = SelectObject(t, GetStockObject(SYSTEM_FONT));
        if (a->status_left[0])
            TextOutW(t, 4, 0, a->status_left, lstrlenW(a->status_left));
        draw_right(a, t, rc.right, rc.bottom);
        SelectObject(t, old_font);
        if (mem) {
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old_bmp);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
    }
    EndPaint(h, &ps);
}

static LRESULT CALLBACK status_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_PAINT:
        paint(&g_app, h);
        return 0;
    case WM_ERASEBKGND:
        return 1;                                     /* paint() covers everything */
    }
    return DefWindowProcW(h, m, wp, lp);
}
