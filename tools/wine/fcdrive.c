/* fcdrive.c - script-driven end-to-end driver for Windows GUI applications (Wine or Windows).
 *
 *   fcdrive.exe [-v] [-o OUTDIR] [-t TIMEOUT_MS] [--keep] SCRIPT|-e "COMMAND"...
 *
 * One command per script line; '#' starts a comment; "double quotes" group words (\" and \\ escape
 * inside quotes); ${NAME} expands an environment variable. Unix absolute paths (/...) are mapped to
 * Wine's Z: drive. Relative output paths are relative to OUTDIR. Run "fcdrive.exe -h" for the
 * command list; tools/wine/README.md documents them.
 *
 * Design notes (lessons from the FreeCell research helpers):
 *  - The target is identified by PROCESS ID (from CreateProcess), never by window class or title.
 *  - Captures and pixel reads run INSIDE the target via fchook.dll (WH_CALLWNDPROC): a cross-process
 *    GetDC + BitBlt returns black under Wine.
 *  - Input is posted (PostMessage) to the target's windows, then the driver waits until the target
 *    thread has retrieved a marker message posted after it (WH_GETMESSAGE hook -> named event).
 *  - Every process the driver launched is terminated when the script ends unless --keep is given.
 */
#include <windows.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "fcipc.h"

#define MAXTOK 64
#define LINEMAX 4096

static struct {
    HANDLE proc;
    DWORD pid, tid;
    HWND hwnd;
    HANDLE map, ev;
    FcIpc *ipc;
    HHOOK hk_call, hk_msg;
    LONG log_seen;
    int held;               /* hold: modifier keys set in the target thread */
} T;

static HANDLE launched[32];
static DWORD launched_pid[32];
static int nlaunched;
static HMODULE hookdll;
static UINT msg_op, msg_sync;
static int timeout_ms = 10000, settle_ms = 100, verbose, keep, cap_method, async_input;
static WCHAR outdir[MAX_PATH];
static WCHAR launch_dir[MAX_PATH];
static WPARAM buttons;           /* MK_* flags currently held (for move) */
static int under_wine;
static const WCHAR *cur_src;
static int cur_line;

/* ---- output --------------------------------------------------------------------------------------- */
static void outw(const WCHAR *s)
{
    char buf[16384];
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof buf - 1, NULL, NULL);
    if (n > 0) fwrite(buf, 1, (size_t)n - 1, stdout);
    fflush(stdout);
}

static void say(const WCHAR *fmt, ...)
{
    WCHAR buf[8192];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 8190, fmt, ap);
    va_end(ap);
    buf[8190] = 0;
    wcscat(buf, L"\n");
    outw(buf);
}

static int err(const WCHAR *fmt, ...)
{
    WCHAR buf[4096], line[4400];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 4094, fmt, ap);
    va_end(ap);
    buf[4094] = 0;
    if (cur_src) _snwprintf(line, 4399, L"%ls:%d: ERROR: %ls\n", cur_src, cur_line, buf);
    else _snwprintf(line, 4399, L"ERROR: %ls\n", buf);
    line[4399] = 0;
    outw(line);
    return 1;
}

/* escape control characters so multi-line static texts print on one line */
static void escape(const WCHAR *s, WCHAR *o, int max)
{
    int k = 0;
    for (; *s && k < max - 3; s++) {
        WCHAR c = *s, e = c == L'\n' ? L'n' : c == L'\r' ? L'r' : c == L'\t' ? L't' : c == L'\\' ? L'\\' : 0;
        if (e) { o[k++] = L'\\'; o[k++] = e; }
        else if (c < 32) k += _snwprintf(o + k, max - k - 1, L"\\x%02x", c);
        else o[k++] = c;
    }
    o[k] = 0;
}

/* ---- parsing helpers ----------------------------------------------------------------------------- */
static int parse_int(const WCHAR *s, long *v)
{
    WCHAR *end;
    if (!s || !*s) return 0;
    *v = wcstol(s, &end, 0);
    return *end == 0;
}

static int want_int(const WCHAR *s, long *v, const WCHAR *what)
{
    if (parse_int(s, v)) return 0;
    return err(L"%ls: expected a number, got '%ls'", what, s);
}

/* /unix/path -> Z:\unix\path under Wine; relative output paths go under outdir. */
static void make_path(const WCHAR *in, WCHAR *out, int is_output)
{
    WCHAR tmp[MAX_PATH * 2];
    if (in[0] == L'/' && in[1] != L'/' && under_wine) _snwprintf(tmp, MAX_PATH * 2 - 1, L"Z:%ls", in);
    else if (is_output && !(in[0] && in[1] == L':') && in[0] != L'\\' && in[0] != L'/')
        _snwprintf(tmp, MAX_PATH * 2 - 1, L"%ls\\%ls", outdir, in);
    else wcsncpy(tmp, in, MAX_PATH * 2 - 1);
    tmp[MAX_PATH * 2 - 1] = 0;
    for (WCHAR *p = tmp; *p; p++) if (*p == L'/') *p = L'\\';
    if (!GetFullPathNameW(tmp, MAX_PATH, out, NULL)) wcsncpy(out, tmp, MAX_PATH);
}

static int strip_eq(const WCHAR *a, const WCHAR *b)     /* case-insensitive, ignoring '&' and "..." */
{
    WCHAR x[256], y[256];
    int i = 0, j = 0;
    for (; *a && i < 255; a++) if (*a != L'&') x[i++] = *a;
    for (; *b && j < 255; b++) if (*b != L'&') y[j++] = *b;
    x[i] = y[j] = 0;
    while (i > 0 && (x[i - 1] == L'.' || x[i - 1] == L' ')) x[--i] = 0;
    while (j > 0 && (y[j - 1] == L'.' || y[j - 1] == L' ')) y[--j] = 0;
    return !_wcsicmp(x, y);
}

/* ---- target process / windows ------------------------------------------------------------------- */
typedef struct { DWORD pid; HWND exclude, found; const WCHAR *title; int dialogs; } Find;

static int skip_class(HWND h)
{
    WCHAR cls[64];
    GetClassNameW(h, cls, 64);
    return !wcscmp(cls, L"tooltips_class32") || !wcscmp(cls, L"#32768") || !wcscmp(cls, L"IME") ||
           !wcscmp(cls, L"MSCTFIME UI") || !wcscmp(cls, L"SysShadow");
}

static BOOL CALLBACK find_cb(HWND h, LPARAM lp)
{
    Find *f = (Find *)lp;
    DWORD pid;
    GetWindowThreadProcessId(h, &pid);
    if (pid != f->pid || h == f->exclude || !IsWindowVisible(h) || skip_class(h)) return TRUE;
    if (!f->dialogs) {          /* main window: unowned, prefer one with a caption */
        if (GetWindow(h, GW_OWNER)) return TRUE;
        if (!f->found) f->found = h;
        if (GetWindowLongW(h, GWL_STYLE) & WS_CAPTION) { f->found = h; return FALSE; }
        return TRUE;
    }
    if (f->title) {
        WCHAR t[512];
        GetWindowTextW(h, t, 512);
        if (wcscmp(t, f->title)) return TRUE;
    }
    f->found = h;               /* EnumWindows walks the z-order top-down: first = topmost */
    return FALSE;
}

static HWND find_main(DWORD pid)
{
    Find f = { pid, NULL, NULL, NULL, 0 };
    EnumWindows(find_cb, (LPARAM)&f);
    return f.found;
}

static HWND find_dialog(const WCHAR *title)
{
    Find f = { T.pid, T.hwnd, NULL, title, 1 };
    if (!T.pid) return NULL;
    EnumWindows(find_cb, (LPARAM)&f);
    return f.found;
}

static int alive(void)
{
    return T.proc && WaitForSingleObject(T.proc, 0) == WAIT_TIMEOUT;
}

static int need_target(void)
{
    if (!T.proc) return err(L"no target process (use launch first)");
    if (!alive()) {
        DWORD code = 0;
        GetExitCodeProcess(T.proc, &code);
        return err(L"target process %lu has exited (exit code %lu)", T.pid, code);
    }
    if (!T.hwnd || !IsWindow(T.hwnd)) {
        T.hwnd = find_main(T.pid);
        if (!T.hwnd) return err(L"target process %lu has no visible top-level window", T.pid);
        T.tid = GetWindowThreadProcessId(T.hwnd, NULL);
    }
    return 0;
}

static void unhook(void)
{
    if (T.hk_call) UnhookWindowsHookEx(T.hk_call);
    if (T.hk_msg) UnhookWindowsHookEx(T.hk_msg);
    T.hk_call = T.hk_msg = NULL;
}

static void drop_target(void)
{
    unhook();
    if (T.ipc) UnmapViewOfFile(T.ipc);
    if (T.map) CloseHandle(T.map);
    if (T.ev) CloseHandle(T.ev);
    memset(&T, 0, sizeof T);
}

/* Run an op inside the target thread (window `h` must belong to it). */
static int hook_op(HWND h, int op)
{
    DWORD_PTR r = 0;
    T.ipc->err[0] = 0;
    T.ipc->op = op;
    if (!SendMessageTimeoutW(h, msg_op, 0, 0, SMTO_NORMAL | SMTO_ABORTIFHUNG, (UINT)timeout_ms, &r)) {
        T.ipc->op = 0;
        return err(L"target did not answer within %d ms (hung or busy)", timeout_ms);
    }
    if (T.ipc->op) {
        T.ipc->op = 0;
        return err(L"hook did not run in the target (fchook.dll not loaded?)");
    }
    return 0;
}

static int ensure_hook(void)
{
    if (need_target()) return 1;
    if (T.hk_call) return 0;
    WCHAR name[64], path[MAX_PATH];
    if (!T.map) {
        _snwprintf(name, 64, FCIPC_MAP_FMT, T.pid);
        T.map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(FcIpc), name);
        if (!T.map) return err(L"CreateFileMapping failed (%lu)", GetLastError());
        T.ipc = MapViewOfFile(T.map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FcIpc));
        if (!T.ipc) return err(L"MapViewOfFile failed (%lu)", GetLastError());
        T.ipc->magic = FCIPC_MAGIC;
        T.ipc->driver_pid = GetCurrentProcessId();
        _snwprintf(name, 64, FCIPC_SYNC_FMT, T.pid);
        T.ev = CreateEventW(NULL, FALSE, FALSE, name);
    }
    if (!hookdll) {
        GetModuleFileNameW(NULL, path, MAX_PATH);
        WCHAR *s = wcsrchr(path, L'\\');
        wcscpy(s ? s + 1 : path, L"fchook.dll");
        hookdll = LoadLibraryW(path);
        if (!hookdll) return err(L"cannot load %ls (%lu)", path, GetLastError());
    }
    HOOKPROC pc = (HOOKPROC)(void *)GetProcAddress(hookdll, "FcCallWndProc");
    HOOKPROC pm = (HOOKPROC)(void *)GetProcAddress(hookdll, "FcGetMsgProc");
    if (!pc || !pm) return err(L"fchook.dll lacks FcCallWndProc/FcGetMsgProc");
    T.hk_call = SetWindowsHookExW(WH_CALLWNDPROC, pc, hookdll, T.tid);
    T.hk_msg = SetWindowsHookExW(WH_GETMESSAGE, pm, hookdll, T.tid);
    if (!T.hk_call || !T.hk_msg) { unhook(); return err(L"SetWindowsHookEx failed (%lu)", GetLastError()); }
    if (hook_op(T.hwnd, FCOP_PING)) { unhook(); return 1; }
    if ((DWORD)T.ipc->result != T.pid) { unhook(); return err(L"hook answered from pid %ld", T.ipc->result); }
    if (verbose) say(L"  hook active in pid %lu (%ld text IAT slots patched)", T.pid, T.ipc->patched);
    return 0;
}

/* Wait until the target thread has dispatched everything posted before now. */
static int sync_target(int quiet)
{
    if (ensure_hook()) return 1;
    HANDLE hs[2] = { T.ev, T.proc };
    ResetEvent(T.ev);
    PostMessageW(T.hwnd, msg_sync, 0, 0);
    DWORD w = WaitForMultipleObjects(2, hs, FALSE, (DWORD)timeout_ms);
    if (w == WAIT_OBJECT_0) return 0;
    if (w == WAIT_OBJECT_0 + 1 || !alive()) return quiet ? 0 : err(L"target exited");
    if (quiet) { say(L"  warning: target did not reach its message loop within %d ms", timeout_ms); return 0; }
    return err(L"target did not reach its message loop within %d ms", timeout_ms);
}

/* After an input: wait until the target has dispatched it (async on: only the settle delay, so a capture
 * can catch what the input started, e.g. a card in flight; the target answers captures while it animates). */
static void settle(void)
{
    if (!async_input) sync_target(1);
    if (settle_ms > 0) Sleep((DWORD)settle_ms);
}

/* ---- dialogs -------------------------------------------------------------------------------------- */
static HWND wait_dialog(const WCHAR *title, int ms)
{
    DWORD t0 = GetTickCount();
    for (;;) {
        HWND d = find_dialog(title);
        if (d || (int)(GetTickCount() - t0) >= ms || !alive()) return d;
        Sleep(30);
    }
}

static void get_text(HWND h, WCHAR *buf, int n)
{
    DWORD_PTR r = 0;
    buf[0] = 0;
    if (!SendMessageTimeoutW(h, WM_GETTEXT, (WPARAM)n, (LPARAM)buf, SMTO_ABORTIFHUNG, 2000, &r))
        GetWindowTextW(h, buf, n);
    buf[n - 1] = 0;
}

typedef struct { WCHAR *acc; int len, max; int print; } DumpCtx;

static void acc_add(DumpCtx *c, const WCHAR *s)
{
    int n = (int)wcslen(s);
    if (c->acc && c->len + n + 2 < c->max) { wcscpy(c->acc + c->len, s); c->len += n; c->acc[c->len++] = L'\n'; c->acc[c->len] = 0; }
}

static BOOL CALLBACK dump_child(HWND h, LPARAM lp)
{
    DumpCtx *c = (DumpCtx *)lp;
    WCHAR cls[64], t[2048], e[4096], line[4400], st[64] = L"";
    GetClassNameW(h, cls, 64);
    get_text(h, t, 2048);
    escape(t, e, 4096);
    LONG style = GetWindowLongW(h, GWL_STYLE);
    if (!_wcsicmp(cls, L"Button")) {
        LONG ty = style & BS_TYPEMASK;
        if (ty == BS_CHECKBOX || ty == BS_AUTOCHECKBOX || ty == BS_RADIOBUTTON || ty == BS_AUTORADIOBUTTON ||
            ty == BS_3STATE || ty == BS_AUTO3STATE) {
            DWORD_PTR r = 0;
            SendMessageTimeoutW(h, BM_GETCHECK, 0, 0, SMTO_ABORTIFHUNG, 2000, &r);
            wcscat(st, r ? L" [x]" : L" [ ]");
        } else if (ty == BS_DEFPUSHBUTTON) wcscat(st, L" (default)");
    }
    if (!IsWindowEnabled(h)) wcscat(st, L" (disabled)");
    if (!IsWindowVisible(h)) wcscat(st, L" (hidden)");
    _snwprintf(line, 4399, L"  [%d] %ls \"%ls\"%ls", GetDlgCtrlID(h), cls, e, st);
    line[4399] = 0;
    if (c->print) say(L"%ls", line);
    acc_add(c, line);
    return TRUE;
}

static void dump_dialog(HWND d, int print, WCHAR *acc, int max)
{
    WCHAR cls[64], t[512], e[1024], line[1200];
    DumpCtx c = { acc, 0, max, print };
    if (acc) acc[0] = 0;
    GetClassNameW(d, cls, 64);
    get_text(d, t, 512);
    escape(t, e, 1024);
    _snwprintf(line, 1199, L"dialog \"%ls\" class=%ls", e, cls);
    line[1199] = 0;
    if (print) say(L"%ls", line);
    acc_add(&c, line);
    EnumChildWindows(d, dump_child, (LPARAM)&c);
}

typedef struct { const WCHAR *text; HWND found; } BtnFind;
static BOOL CALLBACK btn_cb(HWND h, LPARAM lp)
{
    BtnFind *b = (BtnFind *)lp;
    WCHAR cls[64], t[256];
    GetClassNameW(h, cls, 64);
    if (_wcsicmp(cls, L"Button") || !IsWindowVisible(h)) return TRUE;
    get_text(h, t, 256);
    if (strip_eq(t, b->text)) { b->found = h; return FALSE; }
    return TRUE;
}

/* Press a dialog button given by id or caption; waits briefly for the dialog to go away. */
static int press_button(HWND d, const WCHAR *what)
{
    long id;
    HWND b;
    if (parse_int(what, &id)) b = GetDlgItem(d, (int)id);
    else {
        BtnFind f = { what, NULL };
        EnumChildWindows(d, btn_cb, (LPARAM)&f);
        if (!(b = f.found)) { dump_dialog(d, 1, NULL, 0); return err(L"no button '%ls' in the dialog", what); }
        id = GetDlgCtrlID(b);
    }
    if (!PostMessageW(d, WM_COMMAND, MAKEWPARAM((WORD)id, BN_CLICKED), (LPARAM)b))
        return err(L"PostMessage to dialog failed (%lu)", GetLastError());
    /* wait (max 2 s) until the dialog goes away or another dialog opens on top of it */
    for (int i = 0; i < 100 && IsWindow(d) && IsWindowVisible(d) && find_dialog(NULL) == d && alive(); i++) Sleep(20);
    if (alive()) settle();
    return 0;
}

static HWND dialog_or_err(void)
{
    if (need_target()) return NULL;
    HWND d = wait_dialog(NULL, timeout_ms);
    if (!d) err(L"no dialog or message box of pid %lu appeared within %d ms", T.pid, timeout_ms);
    return d;
}

/* ---- registry ------------------------------------------------------------------------------------- */
static HKEY parse_root(const WCHAR *key, const WCHAR **sub)
{
    static const struct { const WCHAR *n; HKEY k; } roots[] = {
        {L"HKCU", HKEY_CURRENT_USER}, {L"HKEY_CURRENT_USER", HKEY_CURRENT_USER},
        {L"HKLM", HKEY_LOCAL_MACHINE}, {L"HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE},
        {L"HKCR", HKEY_CLASSES_ROOT}, {L"HKEY_CLASSES_ROOT", HKEY_CLASSES_ROOT},
        {L"HKU", HKEY_USERS}, {L"HKEY_USERS", HKEY_USERS},
    };
    for (unsigned i = 0; i < sizeof roots / sizeof roots[0]; i++) {
        size_t n = wcslen(roots[i].n);
        if (!_wcsnicmp(key, roots[i].n, n) && (key[n] == L'\\' || key[n] == 0)) {
            *sub = key[n] ? key + n + 1 : key + n;
            return roots[i].k;
        }
    }
    return NULL;
}

static void format_value(DWORD type, const BYTE *d, DWORD len, WCHAR *o, int max)
{
    int k = 0;
    o[0] = 0;
    if (type == REG_DWORD && len == 4) _snwprintf(o, max, L"%lu", *(const DWORD *)d);
    else if (type == REG_SZ || type == REG_EXPAND_SZ) {
        WCHAR t[2048];
        DWORD n = len / 2 < 2047 ? len / 2 : 2047;
        memcpy(t, d, n * 2); t[n] = 0;
        escape(t, o, max);
    } else {
        for (DWORD i = 0; i < len && k < max - 4; i++) k += _snwprintf(o + k, max - k, L"%02x", d[i]);
        if (type == REG_BINARY && len == 4) _snwprintf(o + k, max - k, L" (%ld)", *(const LONG *)d);
    }
}

static const WCHAR *type_name(DWORD t)
{
    switch (t) {
    case REG_SZ: return L"sz"; case REG_EXPAND_SZ: return L"expand_sz"; case REG_BINARY: return L"binary";
    case REG_DWORD: return L"dword"; case REG_MULTI_SZ: return L"multi_sz"; default: return L"other";
    }
}

static int open_key(const WCHAR *key, REGSAM sam, HKEY *k, int create)
{
    const WCHAR *sub;
    HKEY root = parse_root(key, &sub);
    if (!root) return err(L"bad registry key '%ls' (use HKCU\\..., HKLM\\...)", key);
    LONG r = create ? RegCreateKeyExW(root, sub, 0, NULL, 0, sam, NULL, k, NULL) : RegOpenKeyExW(root, sub, 0, sam, k);
    return r ? -1 : 0;
}

static LONG del_tree(HKEY root, const WCHAR *sub)
{
    HKEY k;
    LONG r = RegOpenKeyExW(root, sub, 0, KEY_ALL_ACCESS, &k);
    if (r) return r;
    WCHAR name[256];
    for (;;) {
        DWORD n = 256;
        if (RegEnumKeyExW(k, 0, name, &n, NULL, NULL, NULL, NULL)) break;
        if (del_tree(k, name)) break;
    }
    RegCloseKey(k);
    return RegDeleteKeyW(root, sub);
}

/* ---- commands -------------------------------------------------------------------------------------- */
typedef int (*CmdFn)(int argc, WCHAR **argv);

static int c_launch(int argc, WCHAR **argv)
{
    WCHAR exe[MAX_PATH], cmd[4096], dir[MAX_PATH];
    if (T.proc && alive()) return err(L"a target (pid %lu) is already running", T.pid);
    drop_target();
    make_path(argv[1], exe, 0);
    if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) return err(L"no such file: %ls", exe);
    int k = _snwprintf(cmd, 4095, L"\"%ls\"", exe);
    for (int i = 2; i < argc && k < 4000; i++)
        k += _snwprintf(cmd + k, 4095 - k, wcschr(argv[i], L' ') ? L" \"%ls\"" : L" %ls", argv[i]);
    cmd[4095] = 0;
    if (launch_dir[0]) wcscpy(dir, launch_dir);
    else { wcscpy(dir, exe); WCHAR *s = wcsrchr(dir, L'\\'); if (s) *s = 0; }
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi))
        return err(L"CreateProcess(%ls) failed (%lu)", exe, GetLastError());
    CloseHandle(pi.hThread);
    T.proc = pi.hProcess;
    T.pid = pi.dwProcessId;
    if (nlaunched < 32) { launched_pid[nlaunched] = pi.dwProcessId; launched[nlaunched++] = pi.hProcess; }
    WaitForInputIdle(pi.hProcess, (DWORD)timeout_ms);
    DWORD t0 = GetTickCount();
    while (!(T.hwnd = find_main(T.pid)) && (int)(GetTickCount() - t0) < timeout_ms && alive()) Sleep(50);
    if (!T.hwnd) return err(alive() ? L"pid %lu: no window within %d ms" : L"pid %lu exited during start-up", T.pid, timeout_ms);
    T.tid = GetWindowThreadProcessId(T.hwnd, NULL);
    WCHAR cls[128], t[512];
    GetClassNameW(T.hwnd, cls, 128);
    GetWindowTextW(T.hwnd, t, 512);
    say(L"launched pid=%lu class=%ls title=\"%ls\"", T.pid, cls, t);
    return sync_target(0);
}

static int c_wait(int argc, WCHAR **argv)
{
    long ms;
    if (want_int(argv[1], &ms, L"wait")) return 1;
    Sleep((DWORD)ms);
    return 0;
}

static int c_sync(int argc, WCHAR **argv) { return sync_target(0); }

static int c_set_int(int argc, WCHAR **argv)
{
    long v;
    if (want_int(argv[1], &v, argv[0])) return 1;
    if (!wcscmp(argv[0], L"timeout")) timeout_ms = (int)v;
    else settle_ms = (int)v;
    return 0;
}

static int c_async(int argc, WCHAR **argv)
{
    if (!wcscmp(argv[1], L"on")) async_input = 1;
    else if (!wcscmp(argv[1], L"off")) async_input = 0;
    else return err(L"async: on or off");
    return 0;
}

static int c_capture_method(int argc, WCHAR **argv)
{
    if (!wcscmp(argv[1], L"dc")) cap_method = 0;
    else if (!wcscmp(argv[1], L"print")) cap_method = 1;
    else return err(L"capture_method: dc or print");
    return 0;
}

/* setenv <name> [value]: set (or, without a value, remove) an environment variable for the processes
 * launched from now on. */
static int c_setenv(int argc, WCHAR **argv)
{
    if (!SetEnvironmentVariableW(argv[1], argc > 2 ? argv[2] : NULL) && argc > 2)
        return err(L"SetEnvironmentVariable(%ls) failed (%lu)", argv[1], GetLastError());
    return 0;
}

static int c_launch_dir(int argc, WCHAR **argv)
{
    if (argc < 2 || !wcscmp(argv[1], L"-")) launch_dir[0] = 0;
    else make_path(argv[1], launch_dir, 0);
    return 0;
}

static int c_echo(int argc, WCHAR **argv)
{
    WCHAR buf[2048] = L"";
    for (int i = 1; i < argc; i++) { if (i > 1) wcscat(buf, L" "); wcsncat(buf, argv[i], 2000 - wcslen(buf)); }
    say(L"%ls", buf);
    return 0;
}

static int c_info(int argc, WCHAR **argv)
{
    if (need_target()) return 1;
    RECT wr, cr;
    POINT p = {0, 0};
    WCHAR cls[128], t[512];
    GetWindowRect(T.hwnd, &wr);
    GetClientRect(T.hwnd, &cr);
    ClientToScreen(T.hwnd, &p);
    GetClassNameW(T.hwnd, cls, 128);
    GetWindowTextW(T.hwnd, t, 512);
    say(L"pid=%lu tid=%lu class=%ls title=\"%ls\"", T.pid, T.tid, cls, t);
    say(L"window=(%ld,%ld)-(%ld,%ld) %ldx%ld client=%ldx%ld at (%ld,%ld) %ls style=0x%08lx exstyle=0x%08lx",
        wr.left, wr.top, wr.right, wr.bottom, wr.right - wr.left, wr.bottom - wr.top, cr.right, cr.bottom, p.x, p.y,
        IsZoomed(T.hwnd) ? L"maximized" : IsIconic(T.hwnd) ? L"minimized" : L"normal",
        GetWindowLongW(T.hwnd, GWL_STYLE), GetWindowLongW(T.hwnd, GWL_EXSTYLE));
    return 0;
}

static int c_title(int argc, WCHAR **argv)
{
    WCHAR t[512];
    if (need_target()) return 1;
    GetWindowTextW(T.hwnd, t, 512);
    if (argc > 1) {   /* assert_title */
        if (wcscmp(t, argv[1])) return err(L"title is \"%ls\", expected \"%ls\"", t, argv[1]);
        say(L"ok: title \"%ls\"", t);
    } else say(L"title: \"%ls\"", t);
    return 0;
}

static int show(int cmd, int (*done)(HWND))
{
    if (need_target()) return 1;
    ShowWindowAsync(T.hwnd, cmd);
    DWORD t0 = GetTickCount();
    while (!done(T.hwnd) && (int)(GetTickCount() - t0) < timeout_ms) Sleep(30);
    settle();
    RECT cr;
    GetClientRect(T.hwnd, &cr);
    if (!done(T.hwnd)) return err(L"window did not reach the requested state");
    say(L"client %ldx%ld", cr.right, cr.bottom);
    return 0;
}
static int is_max(HWND h) { return IsZoomed(h); }
static int is_min(HWND h) { return IsIconic(h); }
static int is_normal(HWND h) { return !IsZoomed(h) && !IsIconic(h); }
static int c_maximize(int argc, WCHAR **argv) { return show(SW_MAXIMIZE, is_max); }
static int c_minimize(int argc, WCHAR **argv) { return show(SW_MINIMIZE, is_min); }
static int c_restore(int argc, WCHAR **argv) { return show(SW_RESTORE, is_normal); }

static int c_resize_client(int argc, WCHAR **argv)
{
    long w, h;
    if (want_int(argv[1], &w, argv[0]) || want_int(argv[2], &h, argv[0]) || need_target()) return 1;
    if (!is_normal(T.hwnd)) show(SW_RESTORE, is_normal);
    for (int pass = 0; pass < 3; pass++) {
        RECT cr, wr;
        GetClientRect(T.hwnd, &cr);
        GetWindowRect(T.hwnd, &wr);
        if (cr.right == w && cr.bottom == h) break;
        /* grow/shrink the outer size by the client error (handles menu-bar wrapping) */
        int ow = wr.right - wr.left + (int)w - cr.right, oh = wr.bottom - wr.top + (int)h - cr.bottom;
        SetWindowPos(T.hwnd, NULL, 0, 0, ow, oh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        settle();
    }
    RECT cr;
    GetClientRect(T.hwnd, &cr);
    say(L"client %ldx%ld", cr.right, cr.bottom);
    if (cr.right != w || cr.bottom != h) return err(L"client is %ldx%ld, wanted %ldx%ld (min/max track size?)", cr.right, cr.bottom, w, h);
    return 0;
}

static int c_move_window(int argc, WCHAR **argv)
{
    long x, y;
    if (want_int(argv[1], &x, argv[0]) || want_int(argv[2], &y, argv[0]) || need_target()) return 1;
    SetWindowPos(T.hwnd, NULL, (int)x, (int)y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    settle();
    return 0;
}

/* hold <shift|ctrl|alt>...: keep modifier keys down in the target thread's key state (GetKeyState sees
 * them) for the mouse input that follows, until release. */
static int c_hold(int argc, WCHAR **argv)
{
    int k = 2;
    if (need_target() || ensure_hook()) return 1;
    memset(T.ipc->arg, 0, sizeof T.ipc->arg);
    T.ipc->arg[0] = 1;
    for (int i = 1; i < argc; i++) {
        if (!_wcsicmp(argv[i], L"shift")) { T.ipc->arg[k++] = VK_SHIFT; T.ipc->arg[k++] = VK_LSHIFT; }
        else if (!_wcsicmp(argv[i], L"ctrl")) { T.ipc->arg[k++] = VK_CONTROL; T.ipc->arg[k++] = VK_LCONTROL; }
        else if (!_wcsicmp(argv[i], L"alt")) T.ipc->arg[k++] = VK_MENU;
        else return err(L"hold: unknown modifier '%ls' (shift, ctrl, alt)", argv[i]);
        if (k > 7) return err(L"hold: too many keys");
    }
    if (hook_op(T.hwnd, FCOP_SETKEYS)) return 1;
    T.held = 1;
    return 0;
}

static int c_release(int argc, WCHAR **argv)
{
    if (need_target() || ensure_hook()) return 1;
    if (!T.held) return 0;
    sync_target(1);                         /* the held keys apply to everything posted before */
    T.ipc->arg[0] = 0;
    T.held = 0;
    return hook_op(T.hwnd, FCOP_SETKEYS);
}

static int c_command(int argc, WCHAR **argv)
{
    long id;
    if (want_int(argv[1], &id, argv[0]) || need_target()) return 1;
    PostMessageW(T.hwnd, WM_COMMAND, MAKEWPARAM((WORD)id, 0), 0);
    settle();
    return 0;
}

static int post_mouse(const WCHAR *what, int argc, WCHAR **argv)
{
    long x, y;
    if (want_int(argv[1], &x, argv[0]) || want_int(argv[2], &y, argv[0]) || need_target()) return 1;
    LPARAM l = MAKELPARAM((WORD)(short)x, (WORD)(short)y);
    HWND h = T.hwnd;
    PostMessageW(h, WM_MOUSEMOVE, buttons, l);
    for (const WCHAR *p = what; *p; p++) {
        switch (*p) {
        case 'd': buttons |= MK_LBUTTON; PostMessageW(h, WM_LBUTTONDOWN, buttons, l); break;
        case 'u': buttons &= ~MK_LBUTTON; PostMessageW(h, WM_LBUTTONUP, buttons, l); break;
        case 'D': buttons |= MK_LBUTTON; PostMessageW(h, WM_LBUTTONDBLCLK, buttons, l); break;
        case 'r': buttons |= MK_RBUTTON; PostMessageW(h, WM_RBUTTONDOWN, buttons, l); break;
        case 'R': buttons &= ~MK_RBUTTON; PostMessageW(h, WM_RBUTTONUP, buttons, l); break;
        }
    }
    settle();
    return 0;
}
static int c_click(int argc, WCHAR **argv) { return post_mouse(L"du", argc, argv); }
static int c_dblclick(int argc, WCHAR **argv) { return post_mouse(L"duDu", argc, argv); }
static int c_ldblclk(int argc, WCHAR **argv) { return post_mouse(L"Du", argc, argv); }
static int c_ldown(int argc, WCHAR **argv) { return post_mouse(L"d", argc, argv); }
static int c_lup(int argc, WCHAR **argv) { return post_mouse(L"u", argc, argv); }
static int c_rdown(int argc, WCHAR **argv) { return post_mouse(L"r", argc, argv); }
static int c_rup(int argc, WCHAR **argv) { return post_mouse(L"R", argc, argv); }
static int c_rclick(int argc, WCHAR **argv) { return post_mouse(L"rR", argc, argv); }
static int c_move(int argc, WCHAR **argv) { return post_mouse(L"", argc, argv); }

/* drag <x0> <y0> <x1> <y1> [steps]: button down at (x0, y0), `steps` mouse moves (default 8) along the
 * straight line with the button held, the last one at (x1, y1), then the button up there; one sync at the
 * end. The target sees exactly the messages a real drag sends (posted, so GetCursorPos does not follow). */
static int c_drag(int argc, WCHAR **argv)
{
    long x0, y0, x1, y1, steps = 8;
    if (want_int(argv[1], &x0, argv[0]) || want_int(argv[2], &y0, argv[0]) || want_int(argv[3], &x1, argv[0]) ||
        want_int(argv[4], &y1, argv[0]) || (argc > 5 && want_int(argv[5], &steps, argv[0])) || need_target())
        return 1;
    if (steps < 1) steps = 1;
    HWND h = T.hwnd;
    LPARAM l = MAKELPARAM((WORD)(short)x0, (WORD)(short)y0);
    PostMessageW(h, WM_MOUSEMOVE, buttons, l);
    buttons |= MK_LBUTTON;
    PostMessageW(h, WM_LBUTTONDOWN, buttons, l);
    for (long i = 1; i <= steps; i++) {
        long x = x0 + (x1 - x0) * i / steps, y = y0 + (y1 - y0) * i / steps;
        PostMessageW(h, WM_MOUSEMOVE, buttons, MAKELPARAM((WORD)(short)x, (WORD)(short)y));
    }
    buttons &= ~MK_LBUTTON;
    PostMessageW(h, WM_LBUTTONUP, buttons, MAKELPARAM((WORD)(short)x1, (WORD)(short)y1));
    settle();
    return 0;
}

/* sendmsg <msg> <wparam> <lparam>: SendMessage to the main window (numbers; 0x.. hex allowed), e.g. a
 * WM_MENUSELECT as the menu loop would send it. Prints the result. */
static int c_sendmsg(int argc, WCHAR **argv)
{
    long msg, wp, lp;
    DWORD_PTR r = 0;
    if (want_int(argv[1], &msg, argv[0]) || want_int(argv[2], &wp, argv[0]) || want_int(argv[3], &lp, argv[0]) ||
        need_target())
        return 1;
    if (!SendMessageTimeoutW(T.hwnd, (UINT)msg, (WPARAM)(DWORD)wp, (LPARAM)lp, SMTO_NORMAL, (UINT)timeout_ms, &r))
        return err(L"SendMessage(0x%lx) timed out", msg);
    say(L"sendmsg 0x%lx -> %ld", msg, (long)r);
    settle();
    return 0;
}

/* Posted input never activates the window, so the system's WM_MOUSEACTIVATE is sent by hand. */
static int c_mouse_activate(int argc, WCHAR **argv)
{
    long hit, msg;
    DWORD_PTR r = 0;
    if (want_int(argv[1], &hit, argv[0]) || want_int(argv[2], &msg, argv[0]) || need_target()) return 1;
    if (!SendMessageTimeoutW(T.hwnd, WM_MOUSEACTIVATE, (WPARAM)T.hwnd, MAKELPARAM((WORD)hit, (WORD)msg),
                             SMTO_NORMAL, (UINT)timeout_ms, &r))
        return err(L"WM_MOUSEACTIVATE timed out");
    say(L"WM_MOUSEACTIVATE(hit %ld, msg 0x%lx) -> %ld", hit, msg, (long)r);
    settle();
    return 0;
}

static HWND key_target(void)
{
    GUITHREADINFO gi;
    memset(&gi, 0, sizeof gi);
    gi.cbSize = sizeof gi;
    if (GetGUIThreadInfo(T.tid, &gi)) {
        if (gi.hwndFocus) return gi.hwndFocus;
        if (gi.hwndActive) return gi.hwndActive;
    }
    return T.hwnd;
}

static int c_key(int argc, WCHAR **argv)
{
    if (need_target()) return 1;
    HWND h = key_target();
    for (const WCHAR *p = argv[1]; *p; p++) PostMessageW(h, WM_CHAR, (WPARAM)*p, 1);
    settle();
    return 0;
}

static int parse_vk(const WCHAR *s, long *vk)
{
    static const struct { const WCHAR *n; int vk; } names[] = {
        {L"ESC", VK_ESCAPE}, {L"ESCAPE", VK_ESCAPE}, {L"ENTER", VK_RETURN}, {L"RETURN", VK_RETURN},
        {L"TAB", VK_TAB}, {L"SPACE", VK_SPACE}, {L"BACK", VK_BACK}, {L"BACKSPACE", VK_BACK},
        {L"LEFT", VK_LEFT}, {L"RIGHT", VK_RIGHT}, {L"UP", VK_UP}, {L"DOWN", VK_DOWN}, {L"HOME", VK_HOME},
        {L"END", VK_END}, {L"DELETE", VK_DELETE}, {L"INSERT", VK_INSERT}, {L"PGUP", VK_PRIOR}, {L"PGDN", VK_NEXT},
    };
    if (parse_int(s, vk)) return 1;
    if ((s[0] == L'F' || s[0] == L'f') && s[1]) {
        long n;
        if (parse_int(s + 1, &n) && n >= 1 && n <= 24) { *vk = VK_F1 + n - 1; return 1; }
    }
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!_wcsicmp(s, names[i].n)) { *vk = names[i].vk; return 1; }
    if (s[0] && !s[1] && iswalnum(s[0])) { *vk = towupper(s[0]); return 1; }
    return 0;
}

static int c_vkey(int argc, WCHAR **argv)
{
    long vk, repeat = 1;
    int shift = 0, ctrl = 0, alt = 0;
    if (!parse_vk(argv[1], &vk)) return err(L"vkey: unknown key '%ls' (number, F1..F24, ESC, ENTER, ...)", argv[1]);
    for (int i = 2; i < argc; i++) {
        if (!_wcsicmp(argv[i], L"shift")) shift = 1;
        else if (!_wcsicmp(argv[i], L"ctrl")) ctrl = 1;
        else if (!_wcsicmp(argv[i], L"alt")) alt = 1;
        else if (!_wcsicmp(argv[i], L"repeat") && i + 1 < argc) {   /* the key held: auto-repeat key-downs */
            if (want_int(argv[++i], &repeat, argv[0])) return 1;
            if (repeat < 1 || repeat > 100) return err(L"vkey: repeat 1..100");
        }
        else return err(L"vkey: unknown modifier '%ls'", argv[i]);
    }
    if (ensure_hook()) return 1;
    HWND h = key_target();
    if (shift || ctrl || alt) {      /* modifiers are read with GetKeyState: set them in the target */
        int k = 2;
        memset(T.ipc->arg, 0, sizeof T.ipc->arg);
        T.ipc->arg[0] = 1;
        if (shift) { T.ipc->arg[k++] = VK_SHIFT; T.ipc->arg[k++] = VK_LSHIFT; }
        if (ctrl) { T.ipc->arg[k++] = VK_CONTROL; T.ipc->arg[k++] = VK_LCONTROL; }
        if (alt) T.ipc->arg[k++] = VK_MENU;
        if (hook_op(h, FCOP_SETKEYS)) return 1;
    }
    UINT sc = MapVirtualKeyW((UINT)vk, 0);
    LPARAM down = 1 | (sc << 16) | (alt ? 1 << 29 : 0), up = down | 0xC0000000;
    PostMessageW(h, alt ? WM_SYSKEYDOWN : WM_KEYDOWN, (WPARAM)vk, down);
    for (long r = 1; r < repeat; r++)               /* auto-repeat: the previous-state bit (30) set */
        PostMessageW(h, alt ? WM_SYSKEYDOWN : WM_KEYDOWN, (WPARAM)vk, down | 0x40000000);
    PostMessageW(h, alt ? WM_SYSKEYUP : WM_KEYUP, (WPARAM)vk, up);
    if (async_input && (shift || ctrl || alt)) return err(L"vkey: modifiers need async off");
    int rc = async_input ? 0 : sync_target(0);
    if (shift || ctrl || alt) {
        T.ipc->arg[0] = 0;
        HWND h2 = IsWindow(h) ? h : T.hwnd;
        if (alive() && IsWindow(h2)) hook_op(h2, FCOP_SETKEYS);
    }
    if (settle_ms > 0) Sleep((DWORD)settle_ms);
    return rc;
}

static int do_capture(HWND h, const WCHAR *file, int op)
{
    WCHAR path[MAX_PATH];
    if (ensure_hook()) return 1;
    make_path(file, path, 1);
    size_t n = wcslen(path);
    if (n < 4 || _wcsicmp(path + n - 4, L".bmp")) {
        WCHAR *dot = wcsrchr(path, L'.'), *sl = wcsrchr(path, L'\\');
        if (dot && (!sl || dot > sl)) *dot = 0;
        wcscat(path, L".bmp");
    }
    WCHAR dir[MAX_PATH];
    wcscpy(dir, path);
    WCHAR *s = wcsrchr(dir, L'\\');
    if (s) { *s = 0; CreateDirectoryW(dir, NULL); }
    wcsncpy(T.ipc->path, path, 1023);
    T.ipc->arg[0] = cap_method;
    if (hook_op(h, op)) return 1;
    if (!T.ipc->result) return err(L"capture failed: %ls", T.ipc->err);
    say(L"captured %ls (%ldx%ld)", path, T.ipc->arg[1], T.ipc->arg[2]);
    return 0;
}
static int c_capture(int argc, WCHAR **argv) { return need_target() ? 1 : do_capture(T.hwnd, argv[1], FCOP_CAPTURE_CLIENT); }
static int c_capture_window(int argc, WCHAR **argv) { return need_target() ? 1 : do_capture(T.hwnd, argv[1], FCOP_CAPTURE_WINDOW); }
static int c_capture_dialog(int argc, WCHAR **argv)
{
    HWND d = dialog_or_err();
    return d ? do_capture(d, argv[1], FCOP_CAPTURE_WINDOW) : 1;
}

/* The client pixel (x, y) as RRGGBB in *rgb (read inside the target). */
static int read_pixel(long x, long y, long *rgb)
{
    T.ipc->arg[0] = x; T.ipc->arg[1] = y;
    if (hook_op(T.hwnd, FCOP_GETPIXEL)) return 1;
    COLORREF c = (COLORREF)T.ipc->result;
    if (c == CLR_INVALID) return err(L"pixel (%ld,%ld) is outside the client area", x, y);
    *rgb = (long)(GetRValue(c) << 16 | GetGValue(c) << 8 | GetBValue(c));
    return 0;
}

static int pixel_near(long rgb, long want, long tol)
{
    for (int sh = 0; sh < 24; sh += 8)
        if (labs(((rgb >> sh) & 255) - ((want >> sh) & 255)) > tol) return 0;
    return 1;
}

/* pixel / assert_pixel <x> <y> [RRGGBB [tol]]; wait_pixel <x> <y> <RRGGBB> [tol] [ms] polls until the
 * pixel matches (for timer-driven changes such as a flashing card). */
static int c_pixel(int argc, WCHAR **argv)
{
    long x, y, want = -1, tol = 0, rgb, ms = timeout_ms;
    int poll = !wcscmp(argv[0], L"wait_pixel");
    if (want_int(argv[1], &x, argv[0]) || want_int(argv[2], &y, argv[0]) || ensure_hook()) return 1;
    if (argc > 3) {
        WCHAR *end;
        want = wcstol(argv[3][0] == L'#' ? argv[3] + 1 : argv[3], &end, 16);
        if (*end) return err(L"expected RRGGBB, got '%ls'", argv[3]);
        if (argc > 4 && want_int(argv[4], &tol, argv[0])) return 1;
        if (argc > 5 && want_int(argv[5], &ms, argv[0])) return 1;
    }
    DWORD t0 = GetTickCount();
    if (read_pixel(x, y, &rgb)) return 1;
    if (want < 0) { say(L"pixel %ld,%ld = %06lX", x, y, rgb); return 0; }
    while (poll && !pixel_near(rgb, want, tol) && (long)(GetTickCount() - t0) < ms) {
        Sleep(5);
        if (read_pixel(x, y, &rgb)) return 1;
    }
    if (!pixel_near(rgb, want, tol)) {
        if (poll) return err(L"pixel %ld,%ld = %06lX, expected %06lX within %ld ms", x, y, rgb, want, ms);
        return err(L"pixel %ld,%ld = %06lX, expected %06lX", x, y, rgb, want);
    }
    if (poll) say(L"ok: pixel %ld,%ld = %06lX after %lu ms", x, y, rgb, (unsigned long)(GetTickCount() - t0));
    else say(L"ok: pixel %ld,%ld = %06lX", x, y, rgb);
    return 0;
}

static int c_menu_state(int argc, WCHAR **argv)
{
    long id;
    if (want_int(argv[1], &id, argv[0]) || ensure_hook()) return 1;
    T.ipc->arg[0] = id;
    if (hook_op(T.hwnd, FCOP_MENUSTATE)) return 1;
    LONG st = T.ipc->result;
    if (st == -1) return err(L"menu item %ld not found", id);
    int gray = (st & (MF_GRAYED | MF_DISABLED)) != 0, chk = (st & MF_CHECKED) != 0;
    WCHAR e[2100];
    escape(T.ipc->path, e, 2100);
    if (argc > 2) {
        const WCHAR *w = argv[2];
        int ok = !_wcsicmp(w, L"enabled") ? !gray : !_wcsicmp(w, L"grayed") ? gray :
                 !_wcsicmp(w, L"checked") ? chk : !_wcsicmp(w, L"unchecked") ? !chk : -1;
        if (ok < 0) return err(L"state must be enabled, grayed, checked or unchecked");
        if (!ok) return err(L"menu %ld \"%ls\" is %ls%ls, expected %ls", id, e, gray ? L"grayed" : L"enabled", chk ? L" checked" : L"", w);
        say(L"ok: menu %ld \"%ls\" %ls", id, e, w);
    } else say(L"menu %ld \"%ls\" %ls%ls", id, e, gray ? L"grayed" : L"enabled", chk ? L" checked" : L"");
    return 0;
}

static int c_menubar_text(int argc, WCHAR **argv)
{
    if (ensure_hook()) return 1;
    LONG start = T.ipc->log_count;
    if (hook_op(T.hwnd, FCOP_REDRAW)) return 1;
    sync_target(1);
    WCHAR all[2048] = L"";
    LONG end = T.ipc->log_count;
    if (end - start > FCIPC_LOG_N) start = end - FCIPC_LOG_N;
    for (LONG i = start; i < end; i++) {
        FcTextEntry *e = &T.ipc->log[i % FCIPC_LOG_N];
        if (!e->nc || (HWND)(ULONG_PTR)e->hwnd != T.hwnd || !e->text[0]) continue;
        if (wcsstr(all, e->text)) continue;
        if (all[0]) wcsncat(all, L" | ", 2040 - wcslen(all));
        wcsncat(all, e->text, 2040 - wcslen(all));
    }
    T.log_seen = end;
    if (argc > 1) {
        if (!wcsstr(all, argv[1])) return err(L"menu bar text \"%ls\" does not contain \"%ls\"", all, argv[1]);
        say(L"ok: menubar_text \"%ls\"", all);
        return 0;
    }
    if (!all[0]) say(L"menubar_text: (nothing drawn into the menu bar by the application; skipped)");
    else say(L"menubar_text: \"%ls\"", all);
    return 0;
}

static int c_drawn_text(int argc, WCHAR **argv)
{
    if (ensure_hook()) return 1;
    LONG end = T.ipc->log_count, start = T.log_seen;
    if (end - start > FCIPC_LOG_N) start = end - FCIPC_LOG_N;
    say(L"drawn_text: %ld strings (IAT slots patched: %ld)", end - start, T.ipc->patched);
    for (LONG i = start; i < end; i++) {
        FcTextEntry *e = &T.ipc->log[i % FCIPC_LOG_N];
        WCHAR t[300];
        escape(e->text, t, 300);
        say(L"  #%ld hwnd=%08lx %ls (%ld,%ld) \"%ls\"", e->seq, e->hwnd, e->nc ? L"nonclient" : L"client", e->x, e->y, t);
    }
    T.log_seen = end;
    return 0;
}

/* window_text [substring]: repaint the main window and its children (e.g. a status bar) at once and
 * print every string drawn while doing so (client area and children; off-screen drawing included). */
static int c_window_text(int argc, WCHAR **argv)
{
    if (ensure_hook()) return 1;
    sync_target(1);
    LONG start = T.ipc->log_count;
    if (hook_op(T.hwnd, FCOP_REDRAW)) return 1;
    sync_target(1);
    WCHAR all[2048] = L"";
    LONG end = T.ipc->log_count;
    if (end - start > FCIPC_LOG_N) start = end - FCIPC_LOG_N;
    for (LONG i = start; i < end; i++) {
        FcTextEntry *e = &T.ipc->log[i % FCIPC_LOG_N];
        if (e->nc || !e->text[0]) continue;
        if (all[0]) wcsncat(all, L" | ", 2040 - wcslen(all));
        wcsncat(all, e->text, 2040 - wcslen(all));
    }
    T.log_seen = end;
    if (argc > 1) {
        if (!wcsstr(all, argv[1])) return err(L"window text \"%ls\" does not contain \"%ls\"", all, argv[1]);
        say(L"ok: window_text \"%ls\"", all);
        return 0;
    }
    say(L"window_text: \"%ls\"", all);
    return 0;
}

static int c_dialog_text(int argc, WCHAR **argv)
{
    HWND d = dialog_or_err();
    if (!d) return 1;
    static WCHAR acc[65536];
    dump_dialog(d, argc < 2, acc, 65536);
    if (argc > 1) {           /* assert_dialog_text */
        if (!wcsstr(acc, argv[1])) { outw(acc); return err(L"dialog text does not contain \"%ls\"", argv[1]); }
        say(L"ok: dialog contains \"%ls\"", argv[1]);
    }
    return 0;
}

static int c_wait_dialog(int argc, WCHAR **argv)
{
    long ms = timeout_ms;
    if (need_target() || (argc > 2 && want_int(argv[2], &ms, argv[0]))) return 1;
    const WCHAR *title = argc > 1 && wcscmp(argv[1], L"*") ? argv[1] : NULL;
    HWND d = wait_dialog(title, (int)ms);
    if (!d) return err(L"no dialog%ls%ls%ls within %ld ms", title ? L" \"" : L"", title ? title : L"", title ? L"\"" : L"", ms);
    WCHAR t[512];
    get_text(d, t, 512);
    say(L"dialog \"%ls\"", t);
    return 0;
}

static int c_no_dialog(int argc, WCHAR **argv)
{
    if (need_target()) return 1;
    DWORD t0 = GetTickCount();
    HWND d;
    while ((d = find_dialog(NULL)) && (int)(GetTickCount() - t0) < timeout_ms) Sleep(30);
    if (d) { dump_dialog(d, 1, NULL, 0); return err(L"a dialog is still open"); }
    return 0;
}

static int c_dialog_click(int argc, WCHAR **argv)
{
    HWND d = dialog_or_err();
    return d ? press_button(d, argv[1]) : 1;
}

static int c_dialog_set_text(int argc, WCHAR **argv)
{
    long id;
    HWND d = dialog_or_err();
    if (!d || want_int(argv[1], &id, argv[0])) return 1;
    HWND c = GetDlgItem(d, (int)id);
    if (!c) return err(L"dialog has no control %ld", id);
    DWORD_PTR r = 0;
    SendMessageTimeoutW(c, WM_SETTEXT, 0, (LPARAM)argv[2], SMTO_ABORTIFHUNG, (UINT)timeout_ms, &r);
    WCHAR t[1024];
    get_text(c, t, 1024);
    if (wcscmp(t, argv[2])) return err(L"control %ld text is \"%ls\" after setting \"%ls\"", id, t, argv[2]);
    return 0;
}

static int c_dialog_check(int argc, WCHAR **argv)
{
    long id, want;
    HWND d = dialog_or_err();
    if (!d || want_int(argv[1], &id, argv[0]) || want_int(argv[2], &want, argv[0])) return 1;
    HWND c = GetDlgItem(d, (int)id);
    if (!c) return err(L"dialog has no control %ld", id);
    DWORD_PTR r = 0;
    SendMessageTimeoutW(c, BM_GETCHECK, 0, 0, SMTO_ABORTIFHUNG, 2000, &r);
    if ((r == BST_CHECKED) != (want != 0)) {
        LONG ty = GetWindowLongW(c, GWL_STYLE) & BS_TYPEMASK;
        if (ty == BS_AUTOCHECKBOX || ty == BS_AUTORADIOBUTTON || ty == BS_AUTO3STATE)
            SendMessageTimeoutW(c, BM_CLICK, 0, 0, SMTO_ABORTIFHUNG, (UINT)timeout_ms, &r);   /* like a user click */
        else PostMessageW(d, WM_COMMAND, MAKEWPARAM((WORD)id, BN_CLICKED), (LPARAM)c);       /* app toggles it */
        settle();
        SendMessageTimeoutW(c, BM_GETCHECK, 0, 0, SMTO_ABORTIFHUNG, 2000, &r);
        if ((r == BST_CHECKED) != (want != 0)) {
            SendMessageTimeoutW(c, BM_SETCHECK, want ? BST_CHECKED : BST_UNCHECKED, 0, SMTO_ABORTIFHUNG, 2000, &r);
            say(L"  warning: control %ld did not toggle on click; forced with BM_SETCHECK", id);
        }
    }
    return 0;
}

static int c_close(int argc, WCHAR **argv)
{
    const WCHAR *answer = argc > 1 ? argv[1] : L"Yes";
    if (need_target()) return 1;
    PostMessageW(T.hwnd, WM_CLOSE, 0, 0);
    DWORD t0 = GetTickCount();
    int answered = 0;
    for (;;) {
        int expired = (int)(GetTickCount() - t0) >= timeout_ms;
        if (WaitForSingleObject(T.proc, 50) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(T.proc, &code);
            say(L"closed: pid %lu exited with code %lu", T.pid, code);
            unhook();
            return 0;
        }
        HWND d = find_dialog(NULL);
        if (d && answered < 5) {
            dump_dialog(d, 1, NULL, 0);
            if (!_wcsicmp(answer, L"none")) { say(L"close: leaving the dialog open"); return 0; }
            if (press_button(d, answer)) return 1;
            answered++;
            /* "No"/"Cancel" refuse the close: success means the target is still running, dialog-free */
            if (strip_eq(answer, L"No") || strip_eq(answer, L"Cancel") || !wcscmp(answer, L"7") || !wcscmp(answer, L"2")) {
                if (!alive()) return err(L"target exited although the close was refused");
                if (find_dialog(NULL)) continue;
                say(L"close: refused with '%ls'; pid %lu keeps running", answer, T.pid);
                return 0;
            }
        } else if (expired) break;
    }
    return err(L"pid %lu still running %d ms after WM_CLOSE", T.pid, timeout_ms);
}

static int c_kill(int argc, WCHAR **argv)
{
    if (!T.proc) return err(L"no target");
    TerminateProcess(T.proc, 1);
    WaitForSingleObject(T.proc, 5000);
    say(L"killed pid %lu", T.pid);
    unhook();
    return 0;
}

static int c_wait_exit(int argc, WCHAR **argv)
{
    long ms = timeout_ms;
    if (!T.proc) return err(L"no target");
    if (argc > 1 && want_int(argv[1], &ms, argv[0])) return 1;
    if (WaitForSingleObject(T.proc, (DWORD)ms) != WAIT_OBJECT_0) return err(L"pid %lu still running after %ld ms", T.pid, ms);
    DWORD code = 0;
    GetExitCodeProcess(T.proc, &code);
    say(L"exited: pid %lu code %lu", T.pid, code);
    return 0;
}

static int c_assert_running(int argc, WCHAR **argv)
{
    if (!alive()) return err(L"target is not running");
    return 0;
}

static int c_assert_client(int argc, WCHAR **argv)
{
    long w, h;
    RECT cr;
    if (want_int(argv[1], &w, argv[0]) || want_int(argv[2], &h, argv[0]) || need_target()) return 1;
    GetClientRect(T.hwnd, &cr);
    if (cr.right != w || cr.bottom != h) return err(L"client is %ldx%ld, expected %ldx%ld", cr.right, cr.bottom, w, h);
    say(L"ok: client %ldx%ld", w, h);
    return 0;
}

static int c_regdump(int argc, WCHAR **argv)
{
    HKEY k;
    int r = open_key(argv[1], KEY_READ, &k, 0);
    if (r > 0) return 1;
    if (r < 0) { say(L"reg %ls: (key absent)", argv[1]); return 0; }
    say(L"reg %ls:", argv[1]);
    for (DWORD i = 0;; i++) {
        WCHAR name[256], v[4200];
        BYTE data[4096];
        DWORD nl = 256, dl = sizeof data, type;
        if (RegEnumValueW(k, i, name, &nl, NULL, &type, data, &dl)) break;
        format_value(type, data, dl, v, 4200);
        say(L"  %ls %ls %ls", name[0] ? name : L"(default)", type_name(type), v);
    }
    for (DWORD i = 0;; i++) {
        WCHAR name[256];
        DWORD nl = 256;
        if (RegEnumKeyExW(k, i, name, &nl, NULL, NULL, NULL, NULL)) break;
        say(L"  [%ls]", name);
    }
    RegCloseKey(k);
    return 0;
}

static int c_regset(int argc, WCHAR **argv)
{
    HKEY k;
    const WCHAR *ty = argv[3], *val = argv[4];
    BYTE data[4096];
    DWORD len = 0, type;
    long n;
    if (!_wcsicmp(ty, L"dword") || !_wcsicmp(ty, L"bin32")) {
        if (want_int(val, &n, argv[0])) return 1;
        *(DWORD *)data = (DWORD)n; len = 4;
        type = !_wcsicmp(ty, L"dword") ? REG_DWORD : REG_BINARY;
    } else if (!_wcsicmp(ty, L"binary")) {
        type = REG_BINARY;
        for (const WCHAR *p = val; p[0] && p[1] && len < sizeof data; p += 2) {
            WCHAR hx[3] = {p[0], p[1], 0}, *end;
            data[len++] = (BYTE)wcstoul(hx, &end, 16);
            if (*end) return err(L"bad hex '%ls'", val);
        }
    } else if (!_wcsicmp(ty, L"sz")) {
        type = REG_SZ;
        len = (DWORD)((wcslen(val) + 1) * sizeof(WCHAR));
        if (len > sizeof data) return err(L"string too long");
        memcpy(data, val, len);
    } else return err(L"regset type must be dword, bin32, binary or sz");
    if (open_key(argv[1], KEY_WRITE, &k, 1)) return err(L"cannot create key %ls", argv[1]);
    LONG r = RegSetValueExW(k, argv[2], 0, type, data, len);
    RegCloseKey(k);
    return r ? err(L"RegSetValueEx failed (%ld)", r) : 0;
}

static int c_regdel(int argc, WCHAR **argv)
{
    const WCHAR *sub;
    HKEY root = parse_root(argv[1], &sub), k;
    if (!root) return err(L"bad registry key '%ls'", argv[1]);
    if (argc > 2) {
        if (RegOpenKeyExW(root, sub, 0, KEY_SET_VALUE, &k)) return 0;
        RegDeleteValueW(k, argv[2]);
        RegCloseKey(k);
        return 0;
    }
    if (!*sub) return err(L"refusing to delete a registry root");
    del_tree(root, sub);
    return 0;
}

static int c_assert_reg(int argc, WCHAR **argv)
{
    HKEY k;
    BYTE data[4096];
    DWORD dl = sizeof data, type;
    WCHAR v[4200];
    int r = open_key(argv[1], KEY_READ, &k, 0);
    if (r > 0) return 1;
    if (r < 0 || RegQueryValueExW(k, argv[2], NULL, &type, data, &dl)) {
        if (r == 0) RegCloseKey(k);
        if (!_wcsicmp(argv[3], L"absent")) { say(L"ok: %ls absent", argv[2]); return 0; }
        return err(L"registry value %ls\\%ls is absent", argv[1], argv[2]);
    }
    RegCloseKey(k);
    if ((type == REG_DWORD || type == REG_BINARY) && dl == 4) _snwprintf(v, 4200, L"%ld", *(LONG *)data);
    else format_value(type, data, dl, v, 4200);
    if (wcscmp(v, argv[3])) return err(L"registry %ls = %ls, expected %ls", argv[2], v, argv[3]);
    say(L"ok: %ls = %ls", argv[2], v);
    return 0;
}

static int c_fail(int argc, WCHAR **argv) { return err(L"%ls", argc > 1 ? argv[1] : L"fail"); }

static const struct cmd {
    const WCHAR *name;
    int min, max;
    CmdFn fn;
    const WCHAR *help;
} cmds[] = {
    {L"launch", 1, 30, c_launch, L"<exe> [args...]  start a process (cwd = exe dir) and wait for its window"},
    {L"setenv", 1, 2, c_setenv, L"<name> [value]  environment of later launches (no value: remove it)"},
    {L"launch_dir", 0, 1, c_launch_dir, L"[dir|-]  working directory for later launches (- = exe dir)"},
    {L"wait", 1, 1, c_wait, L"<ms>  sleep"},
    {L"sync", 0, 0, c_sync, L"wait until the target has processed all posted input"},
    {L"timeout", 1, 1, c_set_int, L"<ms>  timeout for waits (default 10000)"},
    {L"settle", 1, 1, c_set_int, L"<ms>  extra delay after each input (default 100)"},
    {L"async", 1, 1, c_async, L"on|off  inputs return without waiting for the target (captures mid-animation; sync waits)"},
    {L"echo", 0, 30, c_echo, L"<text...>  print text"},
    {L"info", 0, 0, c_info, L"print window/client geometry, class, title, state"},
    {L"title", 0, 0, c_title, L"print the main window title"},
    {L"assert_title", 1, 1, c_title, L"<text>  fail unless the title equals text"},
    {L"maximize", 0, 0, c_maximize, L"maximize the main window"},
    {L"minimize", 0, 0, c_minimize, L"minimize the main window"},
    {L"restore", 0, 0, c_restore, L"restore the main window"},
    {L"resize_client", 2, 2, c_resize_client, L"<w> <h>  size the window so its client area is w x h"},
    {L"move_window", 2, 2, c_move_window, L"<x> <y>  move the window (screen coordinates)"},
    {L"assert_client_size", 2, 2, c_assert_client, L"<w> <h>"},
    {L"command", 1, 1, c_command, L"<id>  post WM_COMMAND id (menu item / accelerator)"},
    {L"click", 2, 2, c_click, L"<x> <y>  left click at client coordinates"},
    {L"dblclick", 2, 2, c_dblclick, L"<x> <y>  left double click"},
    {L"ldblclk", 2, 2, c_ldblclk, L"<x> <y>  a double click's second press alone (WM_LBUTTONDBLCLK, then up)"},
    {L"ldown", 2, 2, c_ldown, L"<x> <y>  left button down"},
    {L"lup", 2, 2, c_lup, L"<x> <y>  left button up"},
    {L"rclick", 2, 2, c_rclick, L"<x> <y>  right button down + up"},
    {L"rclick_down", 2, 2, c_rdown, L"<x> <y>  right button down"},
    {L"rclick_up", 2, 2, c_rup, L"<x> <y>  right button up"},
    {L"move", 2, 2, c_move, L"<x> <y>  mouse move (with the buttons currently held)"},
    {L"drag", 4, 5, c_drag, L"<x0> <y0> <x1> <y1> [steps]  left button down, moves (default 8) with it held, up"},
    {L"hold", 1, 3, c_hold, L"<shift|ctrl|alt>...  keep modifier keys down for the input that follows"},
    {L"release", 0, 0, c_release, L"let go of the keys of hold"},
    {L"sendmsg", 3, 3, c_sendmsg, L"<msg> <wparam> <lparam>  SendMessage to the main window (e.g. 0x11F WM_MENUSELECT)"},
    {L"mouse_activate", 2, 2, c_mouse_activate, L"<hittest> <mouse msg>  send WM_MOUSEACTIVATE (e.g. 1 516 = HTCLIENT, WM_RBUTTONDOWN)"},
    {L"key", 1, 1, c_key, L"<chars>  post WM_CHAR for each character to the focus window"},
    {L"vkey", 1, 6, c_vkey, L"<code|F1..F24|ESC|ENTER|..> [shift] [ctrl] [alt] [repeat N]  key down (N times: held) + up"},
    {L"capture", 1, 1, c_capture, L"<out.bmp>  capture the client area (in-process)"},
    {L"capture_window", 1, 1, c_capture_window, L"<out.bmp>  capture the whole window incl. menu bar"},
    {L"capture_dialog", 1, 1, c_capture_dialog, L"<out.bmp>  capture the topmost dialog"},
    {L"capture_method", 1, 1, c_capture_method, L"dc|print  client capture via GetDC+BitBlt (default) or PrintWindow"},
    {L"pixel", 2, 2, c_pixel, L"<x> <y>  print the client pixel colour RRGGBB"},
    {L"assert_pixel", 3, 4, c_pixel, L"<x> <y> <RRGGBB> [tolerance]"},
    {L"wait_pixel", 3, 5, c_pixel, L"<x> <y> <RRGGBB> [tolerance] [ms]  poll until the pixel matches (default: timeout)"},
    {L"menu_state", 1, 1, c_menu_state, L"<id>  print a menu item's text and state"},
    {L"assert_menu", 2, 2, c_menu_state, L"<id> enabled|grayed|checked|unchecked"},
    {L"menubar_text", 0, 0, c_menubar_text, L"print text the app draws into its menu bar (e.g. Cards Left)"},
    {L"assert_menubar_text", 1, 1, c_menubar_text, L"<substring>"},
    {L"window_text", 0, 0, c_window_text, L"repaint the window and its children, print the strings drawn"},
    {L"assert_window_text", 1, 1, c_window_text, L"<substring>  (e.g. a status bar text)"},
    {L"drawn_text", 0, 0, c_drawn_text, L"print strings drawn by the app (TextOut/ExtTextOut/DrawText) since last call"},
    {L"wait_dialog", 0, 2, c_wait_dialog, L"[title|*] [ms]  wait for a dialog / message box"},
    {L"assert_no_dialog", 0, 0, c_no_dialog, L"fail if a dialog stays open (waits up to timeout)"},
    {L"dialog_text", 0, 0, c_dialog_text, L"print title and controls (id, class, text, state) of the topmost dialog"},
    {L"assert_dialog_text", 1, 1, c_dialog_text, L"<substring>  (texts are escaped: \\n \\t)"},
    {L"dialog_click", 1, 1, c_dialog_click, L"<id|caption>  press a dialog button"},
    {L"dialog_set_text", 2, 2, c_dialog_set_text, L"<ctrl id> <text>"},
    {L"dialog_check", 2, 2, c_dialog_check, L"<ctrl id> <0|1>  set a check box / radio button"},
    {L"close", 0, 1, c_close, L"[answer|none]  WM_CLOSE, pressing <answer> (default Yes) in confirmations; No/Cancel expect the app to stay"},
    {L"kill", 0, 0, c_kill, L"terminate the target"},
    {L"wait_exit", 0, 1, c_wait_exit, L"[ms]  wait for the target to exit"},
    {L"assert_running", 0, 0, c_assert_running, L""},
    {L"regdump", 1, 1, c_regdump, L"<HKCU\\key>  print values and subkeys"},
    {L"regset", 4, 4, c_regset, L"<key> <value> dword|bin32|binary|sz <data>  (bin32 = 4-byte REG_BINARY)"},
    {L"regdel", 1, 2, c_regdel, L"<key> [value]  delete a value, or the whole key tree"},
    {L"assert_reg", 3, 3, c_assert_reg, L"<key> <value> <expected|absent>  (4-byte values compare as numbers)"},
    {L"fail", 0, 1, c_fail, L"[message]  fail the script"},
};

/* ---- script runner ---------------------------------------------------------------------------- */
/* ${NAME} = environment variable; ${NAME:-default} uses default (itself expanded) when unset/empty */
static int expand_vars(const WCHAR *in, WCHAR *out, int max)
{
    int k = 0;
    while (*in && k < max - 1) {
        if (in[0] == L'$' && in[1] == L'{') {
            const WCHAR *e = in + 2;
            for (int depth = 1; *e && (depth > 1 || *e != L'}'); e++)
                depth += (e[0] == L'$' && e[1] == L'{') ? 1 : (*e == L'}') ? -1 : 0;
            if (!*e) return err(L"unterminated ${");
            WCHAR body[LINEMAX], val[LINEMAX];
            int n = (int)(e - in - 2);
            wcsncpy(body, in + 2, n); body[n] = 0;
            WCHAR *def = wcsstr(body, L":-");
            if (def) { *def = 0; def += 2; }
            if (!body[0]) return err(L"bad variable name");
            DWORD vl = GetEnvironmentVariableW(body, val, LINEMAX);
            if (vl >= LINEMAX) return err(L"environment variable %ls is too long", body);
            if (!vl) {
                if (!def) return err(L"environment variable %ls is not set", body);
                if (expand_vars(def, val, LINEMAX)) return 1;
            }
            for (WCHAR *p = val; *p && k < max - 1; p++) out[k++] = *p;
            in = e + 1;
        } else out[k++] = *in++;
    }
    out[k] = 0;
    return 0;
}

static int tokenize(WCHAR *s, WCHAR **tok)
{
    int n = 0;
    while (*s) {
        while (*s == L' ' || *s == L'\t' || *s == L'\r' || *s == L'\n') s++;
        if (!*s || *s == L'#') break;
        if (n == MAXTOK) return -1;
        if (*s == L'"') {
            WCHAR *o = ++s;
            tok[n++] = o;
            while (*s && *s != L'"') {
                if (*s == L'\\' && (s[1] == L'"' || s[1] == L'\\')) s++;
                *o++ = *s++;
            }
            if (*s) s++;
            *o = 0;
        } else {
            tok[n++] = s;
            while (*s && *s != L' ' && *s != L'\t' && *s != L'\r' && *s != L'\n') s++;
            if (*s) *s++ = 0;
        }
    }
    return n;
}

static int run_line(const WCHAR *raw)
{
    static WCHAR buf[LINEMAX];
    WCHAR *tok[MAXTOK];
    if (expand_vars(raw, buf, LINEMAX)) return 1;
    int n = tokenize(buf, tok);
    if (n < 0) return err(L"too many arguments");
    if (n == 0) return 0;
    for (unsigned i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        if (wcscmp(tok[0], cmds[i].name)) continue;
        if (n - 1 < cmds[i].min || n - 1 > cmds[i].max)
            return err(L"usage: %ls %ls", cmds[i].name, cmds[i].help);
        WCHAR echo[LINEMAX];
        const WCHAR *b = raw;
        while (*b == L' ' || *b == L'\t') b++;
        wcsncpy(echo, b, LINEMAX - 1);
        echo[LINEMAX - 1] = 0;
        for (size_t e = wcslen(echo); e > 0 && (echo[e - 1] == L'\n' || echo[e - 1] == L'\r' || echo[e - 1] == L' '); e--) echo[e - 1] = 0;
        say(L"> %ls", echo);
        return cmds[i].fn(n, tok);
    }
    return err(L"unknown command '%ls' (fcdrive.exe -h lists commands)", tok[0]);
}

static int run_script(const WCHAR *file)
{
    WCHAR path[MAX_PATH];
    make_path(file, path, 0);
    FILE *f = _wfopen(path, L"rb");
    if (!f) return err(L"cannot open script %ls", path);
    cur_src = file;
    cur_line = 0;
    char line[LINEMAX];
    int rc = 0;
    while (!rc && fgets(line, sizeof line, f)) {
        WCHAR w[LINEMAX];
        cur_line++;
        char *p = line;
        if (cur_line == 1 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
        if (!MultiByteToWideChar(CP_UTF8, 0, p, -1, w, LINEMAX)) { rc = err(L"line is not valid UTF-8"); break; }
        rc = run_line(w);
    }
    fclose(f);
    return rc;
}

static void cleanup(void)
{
    unhook();
    for (int i = 0; i < nlaunched; i++) {
        if (WaitForSingleObject(launched[i], 0) != WAIT_TIMEOUT) continue;
        if (keep) { say(L"cleanup: leaving pid %lu running (--keep)", launched_pid[i]); continue; }
        TerminateProcess(launched[i], 1);
        WaitForSingleObject(launched[i], 5000);
        say(L"cleanup: WARNING: pid %lu was still running at the end; terminated", launched_pid[i]);
    }
}

static void usage(void)
{
    say(L"usage: fcdrive.exe [-v] [-o OUTDIR] [-t TIMEOUT_MS] [--keep] SCRIPT... | -e \"COMMAND\"...");
    say(L"commands (client coordinates in pixels; paths may be Unix paths under Wine):");
    for (unsigned i = 0; i < sizeof cmds / sizeof cmds[0]; i++) say(L"  %-20ls %ls", cmds[i].name, cmds[i].help);
}

int wmain(int argc, WCHAR **argv)
{
    under_wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != NULL;
    msg_op = RegisterWindowMessageW(FCIPC_MSG_OP);
    msg_sync = RegisterWindowMessageW(FCIPC_MSG_SYNC);
    GetCurrentDirectoryW(MAX_PATH, outdir);
    int rc = 0, ran = 0;
    for (int i = 1; i < argc && !rc; i++) {
        const WCHAR *a = argv[i];
        if (!wcscmp(a, L"-h") || !wcscmp(a, L"--help")) { usage(); return 0; }
        else if (!wcscmp(a, L"-v")) verbose = 1;
        else if (!wcscmp(a, L"--keep")) keep = 1;
        else if (!wcscmp(a, L"-o") && i + 1 < argc) {
            make_path(argv[++i], outdir, 0);
            CreateDirectoryW(outdir, NULL);
        } else if (!wcscmp(a, L"-t") && i + 1 < argc) timeout_ms = _wtoi(argv[++i]);
        else if (!wcscmp(a, L"-e") && i + 1 < argc) {
            cur_src = L"-e";
            cur_line = ++ran;
            rc = run_line(argv[++i]);
        } else if (a[0] == L'-' && a[1]) { usage(); return 2; }
        else { ran++; rc = run_script(a); }
    }
    if (!ran) { usage(); return 2; }
    cur_src = NULL;
    cleanup();
    say(rc ? L"fcdrive: FAILED" : L"fcdrive: OK");
    return rc ? 1 : 0;
}
