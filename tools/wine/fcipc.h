/* fcipc.h - shared memory protocol between fcdrive.exe (driver) and fchook.dll (loaded into the
 * target GUI thread by WH_CALLWNDPROC / WH_GETMESSAGE hooks).
 *
 * The driver creates the file mapping FCIPC_MAP_FMT (target pid), fills op/arg/path and sends the
 * registered message FCIPC_MSG_OP to a window of the target thread; the CallWndProc hook runs the op
 * inside the target process (where GDI reads of the window surface work under Wine) and writes
 * result/err. FCIPC_MSG_SYNC is posted; the GetMessage hook signals event FCIPC_SYNC_FMT when the
 * target thread retrieves it, i.e. once every message posted before it has been dispatched.
 * The hook also patches the main module's IAT for the GDI/USER text functions and logs every string
 * drawn (ring buffer `log`), which is how text painted outside controls (e.g. a "Cards Left" counter
 * drawn into the menu bar) can be read back.
 */
#ifndef FCIPC_H
#define FCIPC_H
#include <windows.h>

#define FCIPC_MAP_FMT  L"fcdrive_ipc_%lu"
#define FCIPC_SYNC_FMT L"fcdrive_sync_%lu"
#define FCIPC_MSG_OP   L"FCDRIVE_OP"
#define FCIPC_MSG_SYNC L"FCDRIVE_SYNC"
#define FCIPC_MAGIC    0x46434431u /* 'FCD1' */

enum {
    FCOP_PING = 1,          /* result = target pid */
    FCOP_CAPTURE_CLIENT,    /* arg0 = method (0 dc, 1 PrintWindow); path = BMP; arg1/arg2 = w/h out */
    FCOP_CAPTURE_WINDOW,    /* whole window incl. non-client area */
    FCOP_GETPIXEL,          /* arg0,arg1 = client x,y -> result = COLORREF (or CLR_INVALID) */
    FCOP_SETKEYS,           /* arg0 = 1: save state and press arg2..arg7 (VKs, 0 = none); 0: restore */
    FCOP_REDRAW,            /* RedrawWindow incl. frame, synchronously */
    FCOP_MENUSTATE,         /* arg0 = command id -> result = GetMenuState, path = item text */
    FCOP_MENUBAR,           /* -> arg0..3 = menu bar rect in window coordinates (GetMenuBarInfo) */
};

#define FCIPC_LOG_N 512
typedef struct {
    DWORD hwnd;             /* WindowFromDC(hdc) */
    LONG x, y;              /* position relative to the window's top-left corner */
    LONG nc;                /* 1 if drawn into the non-client area (window DC above the client) */
    LONG seq;
    WCHAR text[120];
} FcTextEntry;

typedef struct {
    DWORD magic, driver_pid;
    volatile LONG op, result;
    LONG arg[8];
    WCHAR path[1024];
    WCHAR err[256];
    volatile LONG patched;      /* number of IAT slots patched in the target's main module */
    volatile LONG log_count;    /* total entries ever written; slot = n % FCIPC_LOG_N */
    FcTextEntry log[FCIPC_LOG_N];
} FcIpc;

#endif
