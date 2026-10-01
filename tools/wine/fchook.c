/* fchook.c - in-process helper for fcdrive.exe (see fcipc.h for the protocol).
 *
 * Loaded into the target GUI thread through WH_CALLWNDPROC / WH_GETMESSAGE hooks. It must run there
 * because, under Wine, a GetDC/BitBlt of another process's window returns black, while the same
 * BitBlt inside the owning process reads the real window surface.
 * Build: i686-w64-mingw32-gcc -shared -Wl,--kill-at -o fchook.dll fchook.c -lgdi32 -luser32
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "fcipc.h"

static FcIpc *ipc;
static UINT msg_op, msg_sync;
static HANDLE sync_ev;
static BYTE saved_keys[256];

static FcIpc *get_ipc(void)
{
    if (!ipc) {
        WCHAR name[64];
        swprintf(name, 64, FCIPC_MAP_FMT, GetCurrentProcessId());
        HANDLE m = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
        if (m) {
            FcIpc *p = MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FcIpc));
            if (p && p->magic == FCIPC_MAGIC) ipc = p;
        }
    }
    return ipc;
}

static void set_err(const WCHAR *what)
{
    if (ipc) swprintf(ipc->err, 256, L"%ls failed (error %lu)", what, GetLastError());
}

/* ---- BMP writer (24-bit, bottom-up) ------------------------------------------------------------ */
static int save_dc(HDC src, int x, int y, int w, int h, const WCHAR *path)
{
    if (w <= 0 || h <= 0) { if (ipc) wcscpy(ipc->err, L"empty area"); return 0; }
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 24; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HDC mem = CreateCompatibleDC(src);
    HBITMAP bm = CreateDIBSection(src, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!mem || !bm) { set_err(L"CreateDIBSection"); if (mem) DeleteDC(mem); return 0; }
    HGDIOBJ old = SelectObject(mem, bm);
    BOOL ok = BitBlt(mem, 0, 0, w, h, src, x, y, SRCCOPY);
    GdiFlush();
    SelectObject(mem, old);
    int rc = 0;
    if (!ok) set_err(L"BitBlt");
    else {
        DWORD stride = ((DWORD)w * 3 + 3) & ~3u, size = stride * (DWORD)h;
        BITMAPFILEHEADER fh; memset(&fh, 0, sizeof fh);
        fh.bfType = 0x4d42; fh.bfOffBits = sizeof fh + sizeof bi.bmiHeader; fh.bfSize = fh.bfOffBits + size;
        bi.bmiHeader.biSizeImage = size;
        FILE *f = _wfopen(path, L"wb");
        if (!f) set_err(L"open output file");
        else {
            rc = fwrite(&fh, sizeof fh, 1, f) == 1 && fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f) == 1 &&
                 fwrite(bits, 1, size, f) == size;
            if (fclose(f) || !rc) { rc = 0; set_err(L"write output file"); }
        }
    }
    DeleteObject(bm); DeleteDC(mem);
    return rc;
}

static int capture(HWND hwnd, int whole, int method)
{
    RECT r;
    if (whole) {
        GetWindowRect(hwnd, &r);
        HDC dc = GetWindowDC(hwnd);
        int rc = save_dc(dc, 0, 0, r.right - r.left, r.bottom - r.top, ipc->path);
        ReleaseDC(hwnd, dc);
        ipc->arg[1] = r.right - r.left; ipc->arg[2] = r.bottom - r.top;
        return rc;
    }
    GetClientRect(hwnd, &r);
    ipc->arg[1] = r.right; ipc->arg[2] = r.bottom;
    if (method == 1) {                     /* PrintWindow into a memory DC, then save that */
        HDC sdc = GetDC(hwnd), mem = CreateCompatibleDC(sdc);
        HBITMAP bm = CreateCompatibleBitmap(sdc, r.right, r.bottom);
        HGDIOBJ old = SelectObject(mem, bm);
        int rc = PrintWindow(hwnd, mem, 1 /* PW_CLIENTONLY */) && save_dc(mem, 0, 0, r.right, r.bottom, ipc->path);
        if (!rc && !ipc->err[0]) set_err(L"PrintWindow");
        SelectObject(mem, old); DeleteObject(bm); DeleteDC(mem); ReleaseDC(hwnd, sdc);
        return rc;
    }
    HDC dc = GetDC(hwnd);
    int rc = save_dc(dc, 0, 0, r.right, r.bottom, ipc->path);
    ReleaseDC(hwnd, dc);
    return rc;
}

/* ---- text logging through IAT patches of the target's main module ------------------------------ */
static void log_text(HDC hdc, int x, int y, const WCHAR *s, int n)
{
    if (!ipc || !s) return;
    if (n < 0) n = (int)wcslen(s);
    HWND hwnd = WindowFromDC(hdc);
    POINT org = {0, 0}, pt = {x, y};
    RECT wr = {0, 0, 0, 0};
    LONG nc = 0;
    if (hwnd) {                     /* -> window coordinates; nc = window DC and above/left of client */
        POINT c = {0, 0};
        GetDCOrgEx(hdc, &org);
        LPtoDP(hdc, &pt, 1);
        GetWindowRect(hwnd, &wr);
        ClientToScreen(hwnd, &c);
        POINT s = {org.x + pt.x, org.y + pt.y};
        pt.x = s.x - wr.left; pt.y = s.y - wr.top;
        nc = org.x == wr.left && org.y == wr.top && (s.y < c.y || s.x < c.x);
    }
    LONG i = InterlockedIncrement(&ipc->log_count) - 1;
    FcTextEntry *e = &ipc->log[i % FCIPC_LOG_N];
    e->hwnd = (DWORD)(ULONG_PTR)hwnd; e->x = pt.x; e->y = pt.y; e->nc = nc; e->seq = i;
    if (n > 119) n = 119;
    memcpy(e->text, s, n * sizeof(WCHAR)); e->text[n] = 0;
}

static void log_text_a(HDC hdc, int x, int y, const char *s, int n)
{
    WCHAR w[128];
    if (!s) return;
    if (n < 0) n = (int)strlen(s);
    int k = MultiByteToWideChar(CP_ACP, 0, s, n > 120 ? 120 : n, w, 127);
    log_text(hdc, x, y, w, k);
}

static BOOL (WINAPI *pTextOutW)(HDC, int, int, LPCWSTR, int);
static BOOL (WINAPI *pTextOutA)(HDC, int, int, LPCSTR, int);
static BOOL (WINAPI *pExtTextOutW)(HDC, int, int, UINT, const RECT *, LPCWSTR, UINT, const INT *);
static BOOL (WINAPI *pExtTextOutA)(HDC, int, int, UINT, const RECT *, LPCSTR, UINT, const INT *);
static int (WINAPI *pDrawTextW)(HDC, LPCWSTR, int, LPRECT, UINT);
static int (WINAPI *pDrawTextA)(HDC, LPCSTR, int, LPRECT, UINT);
static int (WINAPI *pDrawTextExW)(HDC, LPWSTR, int, LPRECT, UINT, LPDRAWTEXTPARAMS);

static BOOL WINAPI hTextOutW(HDC dc, int x, int y, LPCWSTR s, int n) { log_text(dc, x, y, s, n); return pTextOutW(dc, x, y, s, n); }
static BOOL WINAPI hTextOutA(HDC dc, int x, int y, LPCSTR s, int n) { log_text_a(dc, x, y, s, n); return pTextOutA(dc, x, y, s, n); }
static BOOL WINAPI hExtTextOutW(HDC dc, int x, int y, UINT o, const RECT *r, LPCWSTR s, UINT n, const INT *dx)
{ if (!(o & ETO_GLYPH_INDEX)) log_text(dc, x, y, s, (int)n); return pExtTextOutW(dc, x, y, o, r, s, n, dx); }
static BOOL WINAPI hExtTextOutA(HDC dc, int x, int y, UINT o, const RECT *r, LPCSTR s, UINT n, const INT *dx)
{ if (!(o & ETO_GLYPH_INDEX)) log_text_a(dc, x, y, s, (int)n); return pExtTextOutA(dc, x, y, o, r, s, n, dx); }
static int WINAPI hDrawTextW(HDC dc, LPCWSTR s, int n, LPRECT r, UINT f)
{ if (!(f & DT_CALCRECT)) log_text(dc, r ? r->left : 0, r ? r->top : 0, s, n); return pDrawTextW(dc, s, n, r, f); }
static int WINAPI hDrawTextA(HDC dc, LPCSTR s, int n, LPRECT r, UINT f)
{ if (!(f & DT_CALCRECT)) log_text_a(dc, r ? r->left : 0, r ? r->top : 0, s, n); return pDrawTextA(dc, s, n, r, f); }
static int WINAPI hDrawTextExW(HDC dc, LPWSTR s, int n, LPRECT r, UINT f, LPDRAWTEXTPARAMS p)
{ if (!(f & DT_CALCRECT)) log_text(dc, r ? r->left : 0, r ? r->top : 0, s, n); return pDrawTextExW(dc, s, n, r, f, p); }

static const struct { const char *dll, *name; void **orig; void *hook; } patches[] = {
    {"gdi32.dll", "TextOutW", (void **)&pTextOutW, (void *)hTextOutW},
    {"gdi32.dll", "TextOutA", (void **)&pTextOutA, (void *)hTextOutA},
    {"gdi32.dll", "ExtTextOutW", (void **)&pExtTextOutW, (void *)hExtTextOutW},
    {"gdi32.dll", "ExtTextOutA", (void **)&pExtTextOutA, (void *)hExtTextOutA},
    {"user32.dll", "DrawTextW", (void **)&pDrawTextW, (void *)hDrawTextW},
    {"user32.dll", "DrawTextA", (void **)&pDrawTextA, (void *)hDrawTextA},
    {"user32.dll", "DrawTextExW", (void **)&pDrawTextExW, (void *)hDrawTextExW},
};

static void patch_iat(void)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dd->VirtualAddress) return;
    for (unsigned k = 0; k < sizeof patches / sizeof patches[0]; k++)
        if (!*patches[k].orig) *patches[k].orig = (void *)GetProcAddress(GetModuleHandleA(patches[k].dll), patches[k].name);
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dd->VirtualAddress); d->Name; d++) {
        const char *dll = (const char *)(base + d->Name);
        if (!d->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA *in = (IMAGE_THUNK_DATA *)(base + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; in->u1.AddressOfData; in++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(in->u1.Ordinal)) continue;
            const char *fn = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + in->u1.AddressOfData))->Name;
            for (unsigned k = 0; k < sizeof patches / sizeof patches[0]; k++) {
                if (lstrcmpiA(dll, patches[k].dll) || strcmp(fn, patches[k].name) || !*patches[k].orig) continue;
                DWORD old;
                if (VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) {
                    iat->u1.Function = (ULONG_PTR)patches[k].hook;
                    VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
                    ipc->patched++;
                }
            }
        }
    }
}

/* ---- ops ---------------------------------------------------------------------------------------- */
static void run_op(HWND hwnd)
{
    LONG op = ipc->op;
    ipc->err[0] = 0;
    ipc->result = 0;
    switch (op) {
    case FCOP_PING: ipc->result = (LONG)GetCurrentProcessId(); break;
    case FCOP_CAPTURE_CLIENT: ipc->result = capture(hwnd, 0, ipc->arg[0]); break;
    case FCOP_CAPTURE_WINDOW: ipc->result = capture(hwnd, 1, 0); break;
    case FCOP_GETPIXEL: {
        HDC dc = GetDC(hwnd);
        ipc->result = (LONG)GetPixel(dc, ipc->arg[0], ipc->arg[1]);
        ReleaseDC(hwnd, dc);
        break;
    }
    case FCOP_SETKEYS:
        if (ipc->arg[0]) {
            BYTE k[256];
            GetKeyboardState(saved_keys);
            memcpy(k, saved_keys, 256);
            for (int i = 2; i < 8; i++) if (ipc->arg[i] > 0 && ipc->arg[i] < 256) k[ipc->arg[i]] |= 0x80;
            ipc->result = SetKeyboardState(k);
        } else ipc->result = SetKeyboardState(saved_keys);
        break;
    case FCOP_REDRAW:
        ipc->result = RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        break;
    case FCOP_MENUSTATE: {
        HMENU m = GetMenu(hwnd);
        ipc->path[0] = 0;
        ipc->result = m ? (LONG)GetMenuState(m, (UINT)ipc->arg[0], MF_BYCOMMAND) : -1;
        if (m) GetMenuStringW(m, (UINT)ipc->arg[0], ipc->path, 1024, MF_BYCOMMAND);
        break;
    }
    case FCOP_MENUBAR: {
        MENUBARINFO mbi; memset(&mbi, 0, sizeof mbi); mbi.cbSize = sizeof mbi;
        RECT wr; GetWindowRect(hwnd, &wr);
        ipc->result = GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi);
        ipc->arg[0] = mbi.rcBar.left - wr.left; ipc->arg[1] = mbi.rcBar.top - wr.top;
        ipc->arg[2] = mbi.rcBar.right - wr.left; ipc->arg[3] = mbi.rcBar.bottom - wr.top;
        break;
    }
    default: wcscpy(ipc->err, L"unknown op");
    }
    ipc->op = 0;
}

__declspec(dllexport) LRESULT CALLBACK FcCallWndProc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && msg_op) {
        CWPSTRUCT *c = (CWPSTRUCT *)lp;
        if (c->message == msg_op && get_ipc() && ipc->op) run_op(c->hwnd);
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

__declspec(dllexport) LRESULT CALLBACK FcGetMsgProc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && wp == PM_REMOVE && msg_sync) {
        MSG *m = (MSG *)lp;
        if (m->message == msg_sync) {
            if (!sync_ev) {
                WCHAR name[64];
                swprintf(name, 64, FCIPC_SYNC_FMT, GetCurrentProcessId());
                sync_ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
            }
            if (sync_ev) SetEvent(sync_ev);
            m->message = WM_NULL;              /* nothing else needs to see it */
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        msg_op = RegisterWindowMessageW(FCIPC_MSG_OP);
        msg_sync = RegisterWindowMessageW(FCIPC_MSG_SYNC);
        if (get_ipc() && ipc->driver_pid != GetCurrentProcessId()) {
            /* We are in the target: keep the DLL loaded for the life of the process, because the
             * patched IAT points into it even after the driver unhooks. */
            HMODULE self;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               (LPCWSTR)(void *)DllMain, &self);
            patch_iat();
        }
    }
    return TRUE;
}
